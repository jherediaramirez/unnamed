#if WITH_DEV_AUTOMATION_TESTS
#include "Async/ParallelFor.h"
#include "Blocks/VoxelBlockTable.h"
#include "Foundation/FoundationBlockRegistry.h"
#include "Misc/AutomationTest.h"
#include "Storage/VoxelColumn.h"
#include "Terrain/VoxelFlatTerrain.h"
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
        AddError(TEXT("DA_Blocks is missing: run ImportBlocks.py"));
        return false;
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

    // Still lava comes from the registry now: a fluid in the opaque layer (for its light), untinted, its animated
    // still texture on every face.
    const FVoxelStateInfo& Lava = State(TEXT("lava"));
    const int32 LavaStill = Registry->GetAssets()->Textures.IndexOfByPredicate(
        [](const FFoundationBlockTextureAsset& Texture) { return Texture.Name == TEXT("lava_still"); });
    bool bLavaFaces = LavaStill > 0;
    for (int32 Face = 0; Face < VoxelFaces::Count; ++Face)
        bLavaFaces &= Lava.Faces[Face] == uint32(LavaStill);
    TestTrue(TEXT("Lava: a full source in the opaque layer, glowing, not solid"),
        Lava.IsFluid() && Lava.FluidAmount == 8 && Lava.Layer == EVoxelLayer::Opaque && Lava.LightEmission == 15 &&
            !Lava.bSolid);
    TestTrue(TEXT("Lava draws lava_still on every face, untinted"), bLavaFaces);
    TestEqual(TEXT("Lava keeps 26.3's state id"), Table->FindState(TEXT("lava")), uint16(105));
    // Snow layers: k/8 boxes that cover their bottom face; one layer has no collision (26.3), eight are a cube.
    const uint16 Snow = Table->FindState(TEXT("snow"));
    const FVoxelStateInfo& OneLayer = Table->Get(Snow);
    const FVoxelStateInfo& FourLayers = Table->Get(uint16(Snow + 3));
    TestTrue(TEXT("One snow layer: 1/8 high, covers its bottom, not solid"),
        OneLayer.Shape == EVoxelShape::Layers && OneLayer.Layers == 1 &&
            OneLayer.OpaqueFaces == (1 << int32(EVoxelFace::Down)) && !OneLayer.bSolid && !OneLayer.bCastsOcclusion);
    TestTrue(TEXT("Four snow layers: 4/8 high and solid"),
        FourLayers.Shape == EVoxelShape::Layers && FourLayers.Layers == 4 && FourLayers.bSolid);
    TestTrue(TEXT("Eight snow layers are a full cube"), Table->Get(uint16(Snow + 7)).Shape == EVoxelShape::Full);
    TestTrue(TEXT("The top of a snow layer never culls"),
        (OneLayer.Faces[int32(EVoxelFace::Up)] & VoxelFaces::CullBit) == 0);
    const FVoxelStateInfo& Powder = State(TEXT("powder_snow"));
    TestTrue(TEXT("Powder snow: a solid opaque cube for now"),
        Powder.Shape == EVoxelShape::Full && Powder.bSolid && Powder.Layer == EVoxelLayer::Opaque);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVoxelBlocksLookStatesTest, "Unnamed.Voxel.Blocks.LookStates",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FVoxelBlocksLookStatesTest::RunTest(const FString& Parameters)
{
    // A synthetic table set out of order and then changed: the look state follows.
    {
        FVoxelBlockTable Table;
        FVoxelStateInfo Info;
        Info.Shape = EVoxelShape::Full;
        Info.Block = 1;
        Info.Faces[0] = 7;
        Table.SetState(5, Info);
        Table.SetState(3, Info);
        Table.SetState(4, Info);
        TestEqual(TEXT("Set out of order, alike states take the lowest"), Table.GetLookState(5), uint16(3));
        TestEqual(TEXT("A state between them takes the lowest"), Table.GetLookState(4), uint16(3));
        FVoxelStateInfo Other = Info;
        Other.Faces[0] = 8;
        Table.SetState(3, Other);
        TestEqual(TEXT("The lowest changed: the others take the next alike"), Table.GetLookState(5), uint16(4));
        TestEqual(TEXT("The changed state is its own look"), Table.GetLookState(3), uint16(3));
        Info.Block = 2;
        Table.SetState(6, Info);
        TestEqual(TEXT("Another block never shares a look"), Table.GetLookState(6), uint16(6));
        Table.SetState(4, Info);
        TestEqual(TEXT("A state moved to another block leaves its old block"), Table.GetLookState(5), uint16(5));
        TestEqual(TEXT("and joins its new block's look"), Table.GetLookState(6), uint16(4));
    }

    const TSharedRef<const FFoundationBlockRegistry> Registry = FFoundationBlockRegistry::GetDefault();
    if (!Registry->GetAssets())
    {
        AddError(TEXT("DA_Blocks is missing: run ImportBlocks.py"));
        return false;
    }
    const TSharedRef<const FVoxelBlockTable> Table = VoxelBlocks::GetDefault();
    // Every state's look state is of its block, no higher, and draws alike.
    int32 Bad = 0, Shared = 0;
    for (int32 State = 1; State < Table->GetStateCount(); ++State)
    {
        const uint16 Look = Table->GetLookState(uint16(State));
        const FVoxelStateInfo &Info = Table->Get(uint16(State)), &LookInfo = Table->Get(Look);
        Shared += Look != State;
        Bad += Look > State || LookInfo.Block != Info.Block || LookInfo.Shape != Info.Shape ||
            LookInfo.Layer != Info.Layer || LookInfo.Tint != Info.Tint || LookInfo.HeldFluid != Info.HeldFluid ||
            LookInfo.LightEmission != Info.LightEmission ||
            FMemory::Memcmp(LookInfo.Faces, Info.Faces, sizeof(Info.Faces)) != 0;
    }
    TestEqual(TEXT("Look states of the same block, no higher, drawing alike"), Bad, 0);
    AddInfo(FString::Printf(TEXT("%d of %d states share an earlier state's look"), Shared, Table->GetStateCount()));
    // Leaves of every distance and persistence draw alike; snowy grass does not draw as grass.
    const uint16 Leaves = Table->FindState(TEXT("oak_leaves"));
    bool bLeavesShare = true;
    for (const TCHAR* Distance : {TEXT("1"), TEXT("4"), TEXT("7")})
        for (const TCHAR* Persistent : {TEXT("true"), TEXT("false")})
        {
            const int32 State =
                Table->MakeState(TEXT("oak_leaves"), {{TEXT("distance"), Distance}, {TEXT("persistent"), Persistent}});
            bLeavesShare &= State != INDEX_NONE && Table->GetLookState(uint16(State)) == Table->GetLookState(Leaves);
        }
    TestTrue(TEXT("Oak leaves of every distance and persistence share one look"), bLeavesShare);
    const uint16 Grass = Table->FindState(TEXT("grass_block"));
    TestNotEqual(TEXT("Snowy grass has its own look"), Table->GetLookState(Table->GetSnowyState(Grass)),
        Table->GetLookState(Grass));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVoxelTerrainTest, "Unnamed.Voxel.Terrain.Generate",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FVoxelTerrainTest::RunTest(const FString& Parameters)
{
    const TSharedRef<const FVoxelBlockTable> Table = VoxelBlocks::GetDefault();
    if (!Table->FindState(TEXT("stone")))
    {
        AddError(TEXT("DA_Blocks is missing: run ImportBlocks.py"));
        return false;
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

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVoxelFlatTerrainTest, "Unnamed.Voxel.Terrain.Flat",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FVoxelFlatTerrainTest::RunTest(const FString& Parameters)
{
    const TSharedRef<const FVoxelBlockTable> Table = VoxelBlocks::GetDefault();
    const uint16 Bedrock = Table->FindState(TEXT("bedrock")), Dirt = Table->FindState(TEXT("dirt")),
                 Grass = Table->FindState(TEXT("grass_block")), Water = Table->FindState(TEXT("water"));
    if (!Bedrock || !Grass || !Water)
    {
        AddError(TEXT("DA_Blocks is missing: run ImportBlocks.py"));
        return false;
    }
    constexpr int32 Floor = VoxelConstants::MinZ;

    // The classic preset, bottom up: bedrock, two dirt, grass. Every column is the same stack from the floor.
    const FVoxelFlatLayer Classic[] = {{Bedrock, 1}, {Dirt, 2}, {Grass, 1}};
    const FVoxelFlatTerrain Terrain(Classic, *Table);
    TestEqual(TEXT("Classic: the surface is the grass, 3 above the floor"), Terrain.GetSurfaceHeight(5, -9), Floor + 3);
    TestTrue(TEXT("Classic: dry land"), Terrain.IsDry(0, 0));
    for (const FIntPoint Position : {FIntPoint(0, 0), FIntPoint(-7, 123456)})
    {
        FVoxelColumn Column;
        TArray<uint16> Dense;
        Terrain.Generate(Position, Column, Dense);
        bool bStack = true;
        for (int32 Y = 0; Y < 16; ++Y)
            for (int32 X = 0; X < 16; ++X)
            {
                bStack &= Column.GetState(X, Y, Floor) == Bedrock && Column.GetState(X, Y, Floor + 1) == Dirt &&
                    Column.GetState(X, Y, Floor + 2) == Dirt && Column.GetState(X, Y, Floor + 3) == Grass &&
                    Column.GetState(X, Y, Floor + 4) == 0;
                bStack &= Column.GetHeight(EVoxelHeightmap::WorldSurface, X, Y) == Floor + 3;
            }
        TestTrue(
            FString::Printf(TEXT("Column %s holds the stack, with the surface on the grass"), *Position.ToString()),
            bStack);
    }

    // Water on top: the surface is the ground under it, and it is not dry.
    const FVoxelFlatLayer Flooded[] = {{Bedrock, 1}, {Dirt, 5}, {Water, 4}};
    const FVoxelFlatTerrain Sea(Flooded, *Table);
    TestEqual(TEXT("Flooded: the surface is the dirt under the water"), Sea.GetSurfaceHeight(0, 0), Floor + 5);
    TestFalse(TEXT("Flooded: not dry"), Sea.IsDry(0, 0));

    // Layers past the world's top are cut off; trailing air adds nothing.
    const FVoxelFlatLayer Tall[] = {{Bedrock, 1}, {Dirt, 1000}, {0, 3}};
    const FVoxelFlatTerrain Tower(Tall, *Table);
    TestEqual(TEXT("A stack taller than the world fills it"), Tower.GetStack().Num(), VoxelConstants::Height);
    TestEqual(TEXT("...and its surface is the top block"), Tower.GetSurfaceHeight(0, 0), VoxelConstants::MaxZ);
    const FVoxelFlatLayer Airy[] = {{Bedrock, 2}, {0, 5}};
    TestEqual(TEXT("Trailing air is dropped"), FVoxelFlatTerrain(Airy, *Table).GetStack().Num(), 2);
    TestEqual(TEXT("No layers: nothing to stand on, one below the floor"),
        FVoxelFlatTerrain({}, *Table).GetSurfaceHeight(0, 0), Floor - 1);
    return true;
}
#endif
