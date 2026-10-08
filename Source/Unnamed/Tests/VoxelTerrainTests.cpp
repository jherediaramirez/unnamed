#if WITH_DEV_AUTOMATION_TESTS
#include "Async/ParallelFor.h"
#include "Blocks/VoxelBlockTable.h"
#include "Foundation/FoundationBlockRegistry.h"
#include "Misc/AutomationTest.h"
#include "Storage/VoxelColumn.h"
#include "Terrain/VoxelHeightTerrain.h"
#include "Voxel/VoxelBlocks.h"

namespace
{
    // Every state of every section, in order.
    TArray<uint16> FlattenColumn(const FVoxelColumn& Column)
    {
        TArray<uint16> States;
        States.SetNumUninitialized(FVoxelColumn::DenseVolume);
        for (int32 Section = 0; Section < VoxelConstants::SectionCount; ++Section)
            Column.GetSection(Section).Expand(
                TArrayView<uint16>(States).Slice(Section * FVoxelSection::Volume, FVoxelSection::Volume));
        return States;
    }
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVoxelBlocksTableTest, "Unnamed.Voxel.Blocks.Table",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FVoxelBlocksTableTest::RunTest(const FString& Parameters)
{
    const TSharedRef<const FFoundationBlockRegistry> Registry = FFoundationBlockRegistry::GetDefault();
    if (!Registry->GetAssets())
    {
        AddWarning(TEXT("DA_Blocks is missing: run ImportBlocks.py"));
        return true;
    }
    const TSharedRef<const FVoxelBlockTable> Table = VoxelBlocks::GetDefault();
    TestEqual(TEXT("One entry per state id"), Table->GetStateCount(), Registry->GetStateLimit());
    auto State = [&Table](const TCHAR* Id) { return Table->Get(Table->FindState(Id)); };

    const FVoxelStateInfo& Stone = State(TEXT("stone"));
    TestTrue(TEXT("Stone: full, opaque, covers every face, solid, shades corners"),
        Stone.Shape == EVoxelShape::Full && Stone.Layer == EVoxelLayer::Opaque && Stone.OpaqueFaces == 0x3F &&
            Stone.bSolid && Stone.bCastsOcclusion);
    bool bStoneCulls = true;
    for (int32 Face = 0; Face < VoxelFaces::Count; ++Face)
        bStoneCulls &= (Stone.Faces[Face] & VoxelFaces::CullBit) != 0;
    TestTrue(TEXT("Stone's faces cull against full neighbours"), bStoneCulls);

    const FVoxelStateInfo& Glass = State(TEXT("glass"));
    TestTrue(TEXT("Glass: translucent, hides nothing, casts no shade"),
        Glass.Layer == EVoxelLayer::Translucent && Glass.OpaqueFaces == 0 && !Glass.bCastsOcclusion && Glass.bSolid);
    const FVoxelStateInfo& Leaves = State(TEXT("oak_leaves"));
    TestTrue(TEXT("Leaves: cutout, tagged as leaves"),
        Leaves.Layer == EVoxelLayer::Cutout && Leaves.bLeaves && Leaves.OpaqueFaces == 0);
    const FVoxelStateInfo& Slab = State(TEXT("oak_slab"));
    TestTrue(TEXT("A bottom slab covers only its bottom face"),
        Slab.Shape == EVoxelShape::SlabBottom && Slab.OpaqueFaces == (1 << int32(EVoxelFace::Down)));
    const FVoxelStateInfo& Water = State(TEXT("water"));
    TestTrue(TEXT("Water: a full source, its own layer, tinted, not solid"),
        Water.IsFluid() && Water.FluidAmount == 8 && Water.Layer == EVoxelLayer::Water && !Water.bSolid &&
            (Water.Faces[int32(EVoxelFace::Up)] & FoundationBlockFaces::TintBit) && Water.Tint != 0);
    TestNotEqual(TEXT("Water's sides use the flow texture, its top the still one"),
        Water.Faces[int32(EVoxelFace::North)] & FoundationBlockFaces::TextureMask,
        Water.Faces[int32(EVoxelFace::Up)] & FoundationBlockFaces::TextureMask);
    const FVoxelStateInfo& Grass = State(TEXT("grass_block"));
    TestTrue(TEXT("Grass has its side overlay"),
        (Grass.Faces[VoxelFaces::OverlayIndex] & FoundationBlockFaces::OverlayBit) != 0);
    TestTrue(TEXT("Glowstone glows"), State(TEXT("glowstone")).LightEmission == 15);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVoxelTerrainTest, "Unnamed.Voxel.Terrain.Generate",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FVoxelTerrainTest::RunTest(const FString& Parameters)
{
    const TSharedRef<const FVoxelBlockTable> Table = VoxelBlocks::GetDefault();
    if (!Table->FindState(TEXT("stone")))
    {
        AddWarning(TEXT("DA_Blocks is missing: run ImportBlocks.py"));
        return true;
    }
    constexpr int64 Seed = -3224043261468865476ll;
    const FVoxelHeightTerrain Terrain(Seed, *Table);
    const FIntPoint Positions[] = {{0, 0}, {-1, 0}, {5, -7}, {-1000, 2000}, {123456, -654321}};
    constexpr int32 Count = UE_ARRAY_COUNT(Positions);

    // Once in order on this thread, once in reverse on the workers: identical blocks.
    TArray<TArray<uint16>> Forward, Backward;
    Forward.SetNum(Count);
    Backward.SetNum(Count);
    TArray<SIZE_T> Bytes;
    Bytes.SetNum(Count);
    for (int32 Index = 0; Index < Count; ++Index)
    {
        FVoxelColumn Column;
        TArray<uint16> Dense;
        Terrain.Generate(Positions[Index], Column, Dense);
        Forward[Index] = FlattenColumn(Column);
        Bytes[Index] = Column.GetAllocatedSize();

        // The heightmaps agree with the terrain function; bedrock floors the world.
        bool bSurface = true, bBedrock = true;
        for (int32 Y = 0; Y < 16; ++Y)
            for (int32 X = 0; X < 16; ++X)
            {
                const int32 Surface =
                    Terrain.GetSurfaceHeight(Positions[Index].X * 16 + X, Positions[Index].Y * 16 + Y);
                bSurface &= Column.GetHeight(EVoxelHeightmap::OceanFloor, X, Y) == Surface;
                bSurface &= Column.GetHeight(EVoxelHeightmap::WorldSurface, X, Y) ==
                    FMath::Max(Surface, VoxelConstants::SeaLevel - 1);
                bBedrock &= Column.GetState(X, Y, VoxelConstants::MinZ) == Table->FindState(TEXT("bedrock"));
                bBedrock &= Column.GetState(X, Y, -30) == Table->FindState(TEXT("deepslate"));
            }
        TestTrue(
            FString::Printf(TEXT("Column %s: heightmaps follow the terrain"), *Positions[Index].ToString()), bSurface);
        TestTrue(FString::Printf(TEXT("Column %s: bedrock floor, deepslate below 0"), *Positions[Index].ToString()),
            bBedrock);
    }
    ParallelFor(Count,
        [&](int32 Job)
        {
            const int32 Index = Count - 1 - Job;
            FVoxelColumn Column;
            TArray<uint16> Dense;
            Terrain.Generate(Positions[Index], Column, Dense);
            Backward[Index] = FlattenColumn(Column);
        });
    for (int32 Index = 0; Index < Count; ++Index)
    {
        TestTrue(FString::Printf(TEXT("Column %s is the same in any order and thread"), *Positions[Index].ToString()),
            Forward[Index] == Backward[Index]);
        TestTrue(FString::Printf(
                     TEXT("Column %s fits in 64 KB (%.1f KB)"), *Positions[Index].ToString(), Bytes[Index] / 1024.),
            Bytes[Index] < 64 * 1024);
    }
    // A different seed is a different world.
    const FVoxelHeightTerrain Other(Seed + 1, *Table);
    int32 Different = 0;
    for (int32 X = 0; X < 64; ++X)
        Different += Terrain.GetSurfaceHeight(X, 7) != Other.GetSurfaceHeight(X, 7);
    TestTrue(FString::Printf(TEXT("Another seed changes the land (%d of 64 heights)"), Different), Different > 32);
    return true;
}
#endif
