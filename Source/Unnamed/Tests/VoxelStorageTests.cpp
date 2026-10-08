#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "Random/VoxelRandom.h"
#include "Storage/VoxelColumn.h"
#include "Storage/VoxelSection.h"
#include "Tests/VoxelTestBlocks.h"

namespace
{
    // A section of Distinct states (1..Distinct), scattered with a fixed stream.
    TArray<uint16> MakeSectionStates(int32 Distinct, uint64 Seed)
    {
        TArray<uint16> States;
        States.SetNumUninitialized(FVoxelSection::Volume);
        FVoxelXoroshiro Stream(Seed);
        for (int32 Index = 0; Index < FVoxelSection::Volume; ++Index)
            States[Index] = uint16(Index < Distinct ? Index + 1 : 1 + Stream.NextInt(Distinct));
        return States;
    }

    bool SectionMatches(const FVoxelSection& Section, TConstArrayView<uint16> States)
    {
        TArray<uint16> Expanded;
        Expanded.SetNumUninitialized(FVoxelSection::Volume);
        Section.Expand(Expanded);
        for (int32 Index = 0; Index < FVoxelSection::Volume; ++Index)
            if (Expanded[Index] != States[Index] || Section.Get(Index) != States[Index]) return false;
        return true;
    }
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVoxelSectionTest, "Unnamed.Voxel.Storage.Section",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FVoxelSectionTest::RunTest(const FString& Parameters)
{
    const FVoxelSection Air;
    TestTrue(TEXT("A new section is empty air"), Air.IsEmpty() && Air.Get(1234) == 0 && Air.GetAllocatedSize() == 0);
    const FVoxelSection Stone(7);
    TestTrue(TEXT("A filled section stores one state and no indices"),
        Stone.IsUniform() && Stone.Get(4095) == 7 && Stone.GetNonAirCount() == FVoxelSection::Volume &&
            Stone.GetAllocatedSize() == 0);

    // Each palette size lands on the expected width and round-trips.
    struct FCase
    {
        int32 Distinct, Bits;
    };
    for (const FCase Case :
        {FCase{1, 0}, FCase{2, 4}, FCase{16, 4}, FCase{17, 8}, FCase{256, 8}, FCase{257, 16}, FCase{4096, 16}})
    {
        const TArray<uint16> States = MakeSectionStates(Case.Distinct, uint64(Case.Distinct));
        const FVoxelSection Section = FVoxelSection::FromStates(States);
        TestEqual(FString::Printf(TEXT("%d states use %d-bit indices"), Case.Distinct, Case.Bits),
            Section.GetBitsPerIndex(), Case.Bits);
        TestTrue(FString::Printf(TEXT("%d states round-trip"), Case.Distinct), SectionMatches(Section, States));
        // Indices plus the palette (none for a single state or direct 16-bit ids).
        const int32 PaletteBytes = Case.Bits == 0 || Case.Bits == 16 ? 0 : Case.Distinct * int32(sizeof(uint16));
        TestEqual(FString::Printf(TEXT("%d states: bytes"), Case.Distinct), int32(Section.GetAllocatedSize()),
            FVoxelSection::Volume * Case.Bits / 8 + PaletteBytes);
    }

    // Edits widen the indices as the palette fills, and every cell keeps its value.
    FVoxelSection Edited;
    TArray<uint16> Reference;
    Reference.SetNumZeroed(FVoxelSection::Volume);
    FVoxelXoroshiro Stream(uint64(42));
    TArray<int32> Widths;
    for (int32 Edit = 0; Edit < 3000; ++Edit)
    {
        const int32 Index = Stream.NextInt(FVoxelSection::Volume);
        const uint16 State = uint16(Edit % 400);
        Edited.Set(Index, State);
        Reference[Index] = State;
        Widths.AddUnique(Edited.GetBitsPerIndex());
    }
    TestTrue(TEXT("Edits keep every cell"), SectionMatches(Edited, Reference));
    TestTrue(
        TEXT("Edits widened 0 -> 4 -> 8 -> 16 bits"), Widths.Contains(4) && Widths.Contains(8) && Widths.Contains(16));
    int32 NonAir = 0;
    for (const uint16 State : Reference)
        NonAir += State != 0;
    TestEqual(TEXT("Non-air count follows edits"), Edited.GetNonAirCount(), NonAir);

    // Clearing back to one state and compacting returns to the single-state form.
    FVoxelSection Cleared = FVoxelSection::FromStates(MakeSectionStates(20, 3));
    for (int32 Index = 0; Index < FVoxelSection::Volume; ++Index)
        Cleared.Set(Index, 5);
    Cleared.Compact();
    TestTrue(TEXT("Compact drops unused states"), Cleared.IsUniform() && Cleared.GetUniformState() == 5);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVoxelColumnTest, "Unnamed.Voxel.Storage.Column",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FVoxelColumnTest::RunTest(const FString& Parameters)
{
    using namespace VoxelTestBlocks;
    const FVoxelBlockTable Table = VoxelTestBlocks::Make();
    // Stone to Z 60, water at 61 and 62, a leaf block floating at Z 70 over (3, 3), glass at Z 80 over (5, 5).
    const TSharedRef<FVoxelColumn> Column = MakeColumn(Table, FIntPoint(2, -3),
        [](int32 X, int32 Y, int32 Z) -> uint16
        {
            if (Z <= 60) return Stone;
            if (Z <= 62) return WaterSource;
            if (X == 3 && Y == 3 && Z == 70) return Leaves;
            if (X == 5 && Y == 5 && Z == 80) return Glass;
            return Air;
        });
    auto Height = [&Column](EVoxelHeightmap Map, int32 X, int32 Y) { return Column->GetHeight(Map, X, Y); };
    TestEqual(TEXT("World surface: the water"), Height(EVoxelHeightmap::WorldSurface, 0, 0), 62);
    TestEqual(TEXT("Ocean floor ignores water"), Height(EVoxelHeightmap::OceanFloor, 0, 0), 60);
    TestEqual(TEXT("Motion blocking includes water"), Height(EVoxelHeightmap::MotionBlocking, 0, 0), 62);
    TestEqual(TEXT("Leaves are the surface"), Height(EVoxelHeightmap::WorldSurface, 3, 3), 70);
    TestEqual(TEXT("Leaves block motion"), Height(EVoxelHeightmap::MotionBlocking, 3, 3), 70);
    TestEqual(TEXT("The no-leaves map looks through them"), Height(EVoxelHeightmap::MotionBlockingNoLeaves, 3, 3), 62);
    TestEqual(TEXT("Glass is solid"), Height(EVoxelHeightmap::OceanFloor, 5, 5), 80);
    TestEqual(TEXT("States read back"), int32(Column->GetState(3, 3, 70)), int32(Leaves));
    TestEqual(TEXT("Above the world is air"), int32(Column->GetState(0, 0, VoxelConstants::MaxZ + 1)), 0);
    TestEqual(TEXT("Below the world is air"), int32(Column->GetState(0, 0, VoxelConstants::MinZ - 1)), 0);
    TestEqual(TEXT("Position"), Column->GetPosition(), FIntPoint(2, -3));
    TestTrue(TEXT("The sky sections are single-state"), Column->GetSection(VoxelConstants::SectionCount - 1).IsEmpty());

    const TSharedRef<FVoxelColumn> Empty =
        MakeColumn(Table, FIntPoint::ZeroValue, [](int32, int32, int32) { return 0; });
    TestEqual(TEXT("An empty column has no surface"), Empty->GetHeight(EVoxelHeightmap::WorldSurface, 7, 7),
        VoxelConstants::MinZ - 1);
    AddInfo(FString::Printf(TEXT("Column memory: %llu bytes"), uint64(Column->GetAllocatedSize())));
    return true;
}
#endif
