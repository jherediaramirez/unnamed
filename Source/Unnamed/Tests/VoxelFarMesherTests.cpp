#include "Biome/VoxelBiomeSource.h"
#include "Biome/VoxelBiomeTable.h"
#include "Biome/VoxelBiomeTints.h"
#include "Blocks/VoxelBlockTable.h"
#include "Decoration/VoxelDecorator.h"
#include "Decoration/VoxelFeatureGraph.h"
#include "Generation/VoxelArea.h"
#include "Hash/CityHash.h"
#include "Math/RandomStream.h"
#include "Meshing/VoxelFarMesher.h"
#include "Meshing/VoxelMesher.h"
#include "Misc/AutomationTest.h"
#include "Storage/VoxelColumn.h"
#include "Terrain/VoxelDensityTerrain.h"
#include "Tests/VoxelTestBlocks.h"
#include "Voxel/VoxelBlocks.h"
#include "Voxel/VoxelTerrainFactory.h"
#include "Voxel/VoxelWorldgenReader.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace
{
    // Neighbour steps in EVoxelFace order: West, East, North, South.
    constexpr int32 FarMesherTestStepX[4] = {-1, 1, 0, 0};
    constexpr int32 FarMesherTestStepY[4] = {0, 0, -1, 1};
    // The digest's decorated area: radius 2 around the origin, so columns -1..1 have every neighbour final.
    constexpr int32 FarMesherTestRadius = 2;
    constexpr int32 FarMesherTestWidth = 2 * FarMesherTestRadius + 1;

    // One unit wall face of a center cell: side, local X and Y, world Z.
    int64 FarMesherTestUnitKey(int32 Side, int32 X, int32 Y, int32 Z)
    {
        return ((int64(Side) * 16 + X) * 16 + Y) * 4096 + (Z + 2048);
    }

    // The cell across Side of center cell (X, Y): inside the column, the neighbour's edge cell, or null.
    const FVoxelFarCell* FarMesherTestNeighborCell(
        const FVoxelFarSummary& Center, const FVoxelFarSummary* const Neighbors[4], int32 X, int32 Y, int32 Side)
    {
        const int32 NX = X + FarMesherTestStepX[Side], NY = Y + FarMesherTestStepY[Side];
        if (NX >= 0 && NX < 16 && NY >= 0 && NY < 16) return &Center.Get(NX, NY);
        return Neighbors[Side] ? &Neighbors[Side]->Get((NX + 16) % 16, (NY + 16) % 16) : nullptr;
    }

    // Every vertical quad of a layer as unit faces of the center column's cells: Visit(Quad, Side, X, Y, Z), with X
    // and Y possibly outside 0-15 when a quad strays out of the column.
    void FarMesherTestRasterize(
        const FVoxelMeshLayer& Layer, TFunctionRef<void(int32 Quad, int32 Side, int32 X, int32 Y, int32 Z)> Visit)
    {
        for (int32 Quad = 0; Quad < Layer.GetQuadCount(); ++Quad)
        {
            const int32 Side = int32(Layer.TintFaces[Quad * 4].Y);
            if (Side >= 4) continue;
            FVector3f Min = Layer.Positions[Quad * 4], Max = Min;
            for (int32 Corner = 1; Corner < 4; ++Corner)
            {
                Min = Min.ComponentMin(Layer.Positions[Quad * 4 + Corner]);
                Max = Max.ComponentMax(Layer.Positions[Quad * 4 + Corner]);
            }
            const FIntVector Low(FMath::RoundToInt(Min.X), FMath::RoundToInt(Min.Y), FMath::RoundToInt(Min.Z));
            const FIntVector High(FMath::RoundToInt(Max.X), FMath::RoundToInt(Max.Y), FMath::RoundToInt(Max.Z));
            const bool bAlongY = Side < 2;
            // The plane is the cell's own side: its min for a negative face, its min + 1 for a positive one.
            const int32 Plane = (bAlongY ? Low.X : Low.Y) - (Side & 1);
            for (int32 Z = Low.Z; Z < High.Z; ++Z)
                for (int32 Along = bAlongY ? Low.Y : Low.X; Along < (bAlongY ? High.Y : High.X); ++Along)
                    Visit(Quad, Side, bAlongY ? Plane : Along, bAlongY ? Along : Plane, Z);
        }
    }

    // A random summary with the test blocks: dirt, stone or snow tops in three tints, dirt sides, stone fill, some
    // leaves canopies (leaves or snow tops, in two tints), some water (source or flowing, in two tints) one or two
    // cells over the ground, a few cells with no ground. Terraced fields repeat heights along rows, so walls merge, and
    // take tops, tints and water by 4 x 4 patches with a few cells off, so tops merge next to cells that must not.
    void FarMesherTestRandomSummary(FRandomStream& Stream, FIntPoint Position, bool bTerraced, FVoxelFarSummary& Out)
    {
        constexpr uint16 TopStates[3] = {VoxelTestBlocks::Dirt, VoxelTestBlocks::Stone, VoxelTestBlocks::Snow3};
        constexpr uint32 Tints[3] = {0x48B518, 0x6A7039, 0x3F76E4};
        Out.Position = Position;
        const int32 Offset = Stream.RandRange(0, 11);
        int32 Lowest = MAX_int32;
        for (int32 Y = 0; Y < 16; ++Y)
            for (int32 X = 0; X < 16; ++X)
            {
                FVoxelFarCell& Cell = Out.Get(X, Y);
                Cell = FVoxelFarCell();
                if (Stream.FRand() < 0.04f) continue;
                const int32 Height = bTerraced ? (X / 5 * 3 + Y / 6 * 2 + Offset) % 12 : Stream.RandRange(0, 11);
                Cell.GroundZ = int16(60 + Height);
                const int32 Patch = (X / 4 + Y / 4 * 2 + Offset) % 3;
                const bool bOff = !bTerraced || Stream.FRand() < 0.15f;
                Cell.TopState = TopStates[bOff ? Stream.RandRange(0, 2) : Patch];
                Cell.TopTint = Tints[bOff ? Stream.RandRange(0, 2) : (Patch + 1) % 3];
                Cell.SideState = VoxelTestBlocks::Dirt;
                Cell.FillState = VoxelTestBlocks::Stone;
                if (Stream.FRand() < 0.25f)
                {
                    Cell.CanopyBottom = int16(Cell.GroundZ + Stream.RandRange(1, 4));
                    Cell.CanopyTop = int16(Cell.CanopyBottom + Stream.RandRange(0, 3));
                    Cell.CanopyState = VoxelTestBlocks::Leaves;
                    Cell.CanopyTopState = Stream.FRand() < 0.3f ? VoxelTestBlocks::Snow3 : VoxelTestBlocks::Leaves;
                    Cell.CanopyTopTint = Tints[bOff ? Stream.RandRange(0, 1) : Patch % 2];
                }
                if (bOff ? Stream.FRand() < 0.2f : Patch == 2)
                {
                    Cell.FluidZ = int16(Cell.GroundZ + (bOff ? Stream.RandRange(1, 2) : 1));
                    Cell.FluidState =
                        bOff && Stream.FRand() < 0.4f ? VoxelTestBlocks::WaterFlowing : VoxelTestBlocks::WaterSource;
                    Cell.FluidTint = Tints[bOff ? Stream.RandRange(1, 2) : 2];
                }
                Lowest = FMath::Min(Lowest, int32(Cell.GroundZ));
            }
        Out.LowestGround = Lowest == MAX_int32 ? FVoxelFarCell::NoZ : int16(Lowest);
    }

    // The far mesh's horizontal parts, as FarMesherTestCheckTops checks them: ground tops, canopy tops, canopy bottoms
    // and fluid tops.
    constexpr int32 FarMesherTestParts = 4;
    const TCHAR* const FarMesherTestPartNames[FarMesherTestParts] = {
        TEXT("ground top"), TEXT("canopy top"), TEXT("canopy bottom"), TEXT("fluid top")};

    // Part's height (the face's plane), state and tint at Cell; false when the cell draws no such face.
    bool FarMesherTestPartOf(
        const FVoxelBlockTable& Table, const FVoxelFarCell& Cell, int32 Part, float& Plane, uint16& State, uint32& Tint)
    {
        switch (Part)
        {
        case 0:
            Plane = float(Cell.GroundZ + 1), State = Cell.TopState, Tint = Cell.TopTint;
            return Cell.HasGround();
        case 1:
            Plane = float(Cell.CanopyTop + 1), State = Cell.CanopyTopState, Tint = Cell.CanopyTopTint;
            return Cell.HasCanopy();
        case 2:
            Plane = float(Cell.CanopyBottom), State = Cell.CanopyState, Tint = Cell.CanopyTint;
            return Cell.HasCanopy() && !(Cell.HasGround() && Cell.GroundZ == Cell.CanopyBottom - 1);
        default:
            State = Cell.FluidState, Tint = Cell.FluidTint;
            Plane = float(Cell.FluidZ) + (Cell.HasFluid() ? Table.Get(State).GetFluidHeight() : 0.f);
            return Cell.HasFluid();
        }
    }

    // Every Face quad of Layer is Part of the cells under it: each of its four vertices carries every covered cell's
    // face code (Codes.X) and tint (TintFaces.X) for that part, and the quad lies flat at the part's plane, so a quad
    // never merges cells of different heights, states or tints. Counts the quads over each cell in Covered (16 x 16).
    // Returns the cells under a wrong quad; the first is described in FirstWrong.
    int32 FarMesherTestCheckTops(const FVoxelBlockTable& Table, const FVoxelFarSummary& Center,
        const FVoxelMeshLayer& Layer, EVoxelFace Face, int32 Part, int32* Covered, int32& Quads, int32& Merged,
        FString& FirstWrong)
    {
        int32 Wrong = 0;
        for (int32 Quad = 0; Quad < Layer.GetQuadCount(); ++Quad)
        {
            if (int32(Layer.TintFaces[Quad * 4].Y) != int32(Face)) continue;
            FVector3f Min = Layer.Positions[Quad * 4], Max = Min;
            for (int32 Corner = 1; Corner < 4; ++Corner)
            {
                Min = Min.ComponentMin(Layer.Positions[Quad * 4 + Corner]);
                Max = Max.ComponentMax(Layer.Positions[Quad * 4 + Corner]);
            }
            const FIntPoint Low(FMath::RoundToInt(Min.X), FMath::RoundToInt(Min.Y));
            const FIntPoint High(FMath::RoundToInt(Max.X), FMath::RoundToInt(Max.Y));
            ++Quads;
            Merged += (High.X - Low.X) * (High.Y - Low.Y) > 1;
            for (int32 Y = Low.Y; Y < High.Y; ++Y)
                for (int32 X = Low.X; X < High.X; ++X)
                {
                    const bool bInside = X >= 0 && X < 16 && Y >= 0 && Y < 16;
                    float Plane = 0.f;
                    uint16 State = 0;
                    uint32 Tint = 0;
                    const bool bHas = bInside && FarMesherTestPartOf(Table, Center.Get(X, Y), Part, Plane, State, Tint);
                    if (bInside) ++Covered[Y * 16 + X];
                    bool bSame = bHas && Min.Z == Max.Z && FMath::IsNearlyEqual(Min.Z, Plane, 1e-3f);
                    const uint32 Code = bHas ? Table.Get(State).Faces[int32(Face)] : 0;
                    for (int32 Corner = 0; Corner < 4; ++Corner)
                        bSame = bSame && uint32(Layer.Codes[Quad * 4 + Corner].X) == Code &&
                            uint32(Layer.TintFaces[Quad * 4 + Corner].X) == Tint;
                    if (bSame) continue;
                    if (!Wrong++)
                        FirstWrong = FString::Printf(
                            TEXT("%s quad %d at (%d, %d): z %.3f, code %u, tint 0x%06x; the cell's %s: z %.3f, state "
                                 "%d (code %u), tint 0x%06x"),
                            FarMesherTestPartNames[Part], Quad, X, Y, Min.Z, uint32(Layer.Codes[Quad * 4].X),
                            uint32(Layer.TintFaces[Quad * 4].X), bHas ? TEXT("part") : TEXT("part (none)"), Plane,
                            int32(State), Code, Tint);
                }
        }
        return Wrong;
    }

    // Every byte of a column's far mesh, as the mesh digest hashes a section's: each non-empty layer's index and
    // column, then its arrays.
    void FarMesherTestHash(const FVoxelMeshData& Mesh, int32 ColumnIndex, uint64& Hash)
    {
        auto Add = [&Hash](const void* Data, SIZE_T Bytes)
        { Hash = CityHash64WithSeed(static_cast<const char*>(Data), uint32(Bytes), Hash); };
        for (int32 LayerIndex = 0; LayerIndex < int32(EVoxelLayer::Count); ++LayerIndex)
        {
            const FVoxelMeshLayer& MeshLayer = Mesh.Layers[LayerIndex];
            if (MeshLayer.IsEmpty()) continue;
            const int32 Place[2] = {LayerIndex, ColumnIndex};
            Add(Place, sizeof(Place));
            Add(MeshLayer.Positions.GetData(), MeshLayer.Positions.Num() * sizeof(FVector3f));
            Add(MeshLayer.BlockUVs.GetData(), MeshLayer.BlockUVs.Num() * sizeof(FVector2f));
            Add(MeshLayer.Codes.GetData(), MeshLayer.Codes.Num() * sizeof(FVector2f));
            Add(MeshLayer.TintFaces.GetData(), MeshLayer.TintFaces.Num() * sizeof(FVector2f));
            Add(MeshLayer.Colors.GetData(), MeshLayer.Colors.Num() * sizeof(FColor));
            Add(MeshLayer.Indices.GetData(), MeshLayer.Indices.Num() * sizeof(uint32));
        }
    }

    // The first quad of a layer that is an up face at height Z, or INDEX_NONE.
    int32 FarMesherTestFindTop(const FVoxelMeshLayer& Layer, float Z)
    {
        for (int32 Quad = 0; Quad < Layer.GetQuadCount(); ++Quad)
            if (int32(Layer.TintFaces[Quad * 4].Y) == int32(EVoxelFace::Up) && Layer.Positions[Quad * 4].Z == Z)
                return Quad;
        return INDEX_NONE;
    }

    // Case 1: the summary's rules on hand-made columns of the real 26.3 blocks, and a Superflat-like column's tops.
    void FarMesherTestSummaryRules(FAutomationTestBase& Test, const FVoxelBlockTable& Table)
    {
        const FVoxelFarClasses Classes(Table);
        auto Find = [&Table, &Test](const TCHAR* Id)
        {
            const uint16 State = Table.FindState(Id);
            Test.TestTrue(FString::Printf(TEXT("the block table has %s"), Id), State != 0);
            return State;
        };
        const uint16 Stone = Find(TEXT("stone")), Dirt = Find(TEXT("dirt")), Grass = Find(TEXT("grass_block")),
                     ShortGrass = Find(TEXT("short_grass")), Snow = Find(TEXT("snow")), OakLog = Find(TEXT("oak_log")),
                     OakLeaves = Find(TEXT("oak_leaves")), Water = Find(TEXT("water")), Sand = Find(TEXT("sand"));

        auto ClassOf = [&Classes](uint16 State) { return int32(Classes.Get(State)); };
        Test.TestEqual(TEXT("air is skipped"), ClassOf(0), int32(EVoxelFarClass::Skip));
        Test.TestEqual(TEXT("short grass is skipped"), ClassOf(ShortGrass), int32(EVoxelFarClass::Skip));
        Test.TestEqual(TEXT("oak_log is skipped"), ClassOf(OakLog), int32(EVoxelFarClass::Skip));
        const int32 OakLogX = Table.MakeState(TEXT("oak_log"), {{FName(TEXT("axis")), FName(TEXT("x"))}});
        if (OakLogX != INDEX_NONE)
            Test.TestEqual(
                TEXT("every oak_log state is skipped"), ClassOf(uint16(OakLogX)), int32(EVoxelFarClass::Skip));
        Test.TestEqual(TEXT("oak_leaves are leaves"), ClassOf(OakLeaves), int32(EVoxelFarClass::Leaves));
        Test.TestEqual(TEXT("water is fluid"), ClassOf(Water), int32(EVoxelFarClass::Fluid));
        Test.TestEqual(TEXT("a snow layer is a snow layer"), ClassOf(Snow), int32(EVoxelFarClass::SnowLayer));
        Test.TestEqual(TEXT("grass_block is ground"), ClassOf(Grass), int32(EVoxelFarClass::Ground));
        Test.TestEqual(TEXT("sand is ground"), ClassOf(Sand), int32(EVoxelFarClass::Ground));
        Test.TestTrue(TEXT("minecraft:stripped_warped_hyphae is a log id"),
            FVoxelFarClasses::IsLogId(TEXT("minecraft:stripped_warped_hyphae")));
        Test.TestTrue(TEXT("mushroom_stem is a log id"), FVoxelFarClasses::IsLogId(TEXT("mushroom_stem")));
        Test.TestFalse(TEXT("oak_leaves is not a log id"), FVoxelFarClasses::IsLogId(TEXT("oak_leaves")));
        Test.TestFalse(TEXT("oak_planks is not a log id"), FVoxelFarClasses::IsLogId(TEXT("oak_planks")));
        Test.TestTrue(TEXT("oak_log is a log id"), FVoxelFarClasses::IsLogId(TEXT("oak_log")));
        Test.TestTrue(TEXT("stripped_oak_wood is a log id"), FVoxelFarClasses::IsLogId(TEXT("stripped_oak_wood")));
        Test.TestTrue(TEXT("crimson_hyphae is a log id"), FVoxelFarClasses::IsLogId(TEXT("crimson_hyphae")));
        Test.TestFalse(TEXT("log_cabin_x is not a log id"), FVoxelFarClasses::IsLogId(TEXT("log_cabin_x")));
        Test.TestFalse(TEXT("woodland is not a log id"), FVoxelFarClasses::IsLogId(TEXT("woodland")));
        for (const TCHAR* Axis : {TEXT("x"), TEXT("y"), TEXT("z")})
        {
            const int32 Stripped = Table.MakeState(TEXT("stripped_oak_wood"), {{FName(TEXT("axis")), FName(Axis)}});
            if (Test.TestTrue(FString::Printf(TEXT("the block table has stripped_oak_wood axis=%s"), Axis),
                    Stripped != INDEX_NONE && Stripped != 0))
                Test.TestEqual(FString::Printf(TEXT("stripped_oak_wood axis=%s is skipped"), Axis),
                    ClassOf(uint16(Stripped)), int32(EVoxelFarClass::Skip));
        }
        // A model holding a fluid: seagrass, else the table's first model state with a held fluid.
        uint16 HeldModel = Table.FindState(TEXT("seagrass"));
        auto IsHeldModel = [&Table](uint16 State)
        { return State && Table.Get(State).Shape == EVoxelShape::Model && Table.Get(State).HeldFluid != 0; };
        for (int32 State = 1; !IsHeldModel(HeldModel) && State < Table.GetStateCount(); ++State)
            HeldModel = uint16(State);
        if (Test.TestTrue(TEXT("the block table has a model state holding a fluid"), IsHeldModel(HeldModel)))
            Test.TestEqual(FString::Printf(TEXT("a model holding a fluid (state %d) is fluid"), int32(HeldModel)),
                ClassOf(HeldModel), int32(EVoxelFarClass::Fluid));
        else
            HeldModel = 0;

        // Grass land at 64 over dirt and stone. (1, 1): short grass. (3, 3): a snow layer. An oak at (8, 8): trunk
        // 65-69, leaves 68-70 over (6-10, 6-10), snow on the canopy at (6, 6). (12, 3): a bare trunk 65-67. X >= 13 and
        // Y >= 12: a pond, water 61-63 over sand 59-60, with the fluid-holding model on top of the water at (15, 15).
        const auto Fill = [=](int32 X, int32 Y, int32 Z) -> uint16
        {
            if (X >= 13 && Y >= 12)
                return Z <= 58                      ? Stone
                    : Z <= 60                       ? Sand
                    : Z <= 63                       ? Water
                    : X == 15 && Y == 15 && Z == 64 ? HeldModel
                                                    : uint16(0);
            if (Z < 60) return Stone;
            if (Z < 64) return Dirt;
            if (Z == 64) return Grass;
            if (X == 1 && Y == 1 && Z == 65) return ShortGrass;
            if (X == 3 && Y == 3 && Z == 65) return Snow;
            if (X == 12 && Y == 3 && Z <= 67) return OakLog;
            if (X == 8 && Y == 8 && Z <= 69) return OakLog;
            if (FMath::Abs(X - 8) <= 2 && FMath::Abs(Y - 8) <= 2 && Z >= 68 && Z <= 70) return OakLeaves;
            if (X == 6 && Y == 6 && Z == 71) return Snow;
            return 0;
        };
        const TSharedRef<FVoxelColumn> Land = VoxelTestBlocks::MakeColumn(Table, FIntPoint(2, -3), Fill);
        TUniquePtr<FVoxelFarSummary> Summary = MakeUnique<FVoxelFarSummary>();
        VoxelFarMesher::Summarize(Classes, Table, *Land, *Summary);
        auto CellAt = [&Summary](int32 X, int32 Y) -> const FVoxelFarCell& { return Summary->Get(X, Y); };
        Test.TestTrue(TEXT("the summary takes the column's position"), Summary->Position == FIntPoint(2, -3));
        Test.TestEqual(TEXT("the lowest ground is the pond's sand"), int32(Summary->LowestGround), 60);

        const FVoxelFarCell& Plant = CellAt(1, 1);
        Test.TestEqual(TEXT("short grass: the ground is the grass cell"), int32(Plant.GroundZ), 64);
        Test.TestEqual(TEXT("short grass: the top is the grass block's"), int32(Plant.TopState), int32(Grass));
        Test.TestEqual(TEXT("short grass: the side is the grass block's"), int32(Plant.SideState), int32(Grass));
        Test.TestEqual(TEXT("short grass: the fill is dirt"), int32(Plant.FillState), int32(Dirt));
        Test.TestTrue(TEXT("short grass: no canopy, no fluid"), !Plant.HasCanopy() && !Plant.HasFluid());
        Test.TestTrue(
            TEXT("short grass: untinted summaries take the plains tint"), Plant.TopTint == Table.Get(Grass).Tint);

        const FVoxelFarCell& Snowy = CellAt(3, 3);
        Test.TestEqual(TEXT("snow layer: the ground is the grass cell"), int32(Snowy.GroundZ), 64);
        Test.TestEqual(TEXT("snow layer: the top is the snow layer's"), int32(Snowy.TopState), int32(Snow));
        Test.TestEqual(TEXT("snow layer: the side is the grass block's"), int32(Snowy.SideState), int32(Grass));

        const FVoxelFarCell& Leafy = CellAt(7, 8);
        Test.TestEqual(TEXT("oak: the ground under the canopy is the grass"), int32(Leafy.GroundZ), 64);
        Test.TestEqual(TEXT("oak: the canopy's top"), int32(Leafy.CanopyTop), 70);
        Test.TestEqual(TEXT("oak: the canopy's bottom"), int32(Leafy.CanopyBottom), 68);
        Test.TestEqual(TEXT("oak: the canopy's leaves"), int32(Leafy.CanopyState), int32(OakLeaves));
        Test.TestEqual(TEXT("oak: the canopy's top face"), int32(Leafy.CanopyTopState), int32(OakLeaves));

        const FVoxelFarCell& Trunk = CellAt(8, 8);
        Test.TestEqual(TEXT("oak trunk: the ground is the grass, not the log"), int32(Trunk.GroundZ), 64);
        Test.TestEqual(TEXT("oak trunk: the canopy is the leaves above the trunk"), int32(Trunk.CanopyTop), 70);
        Test.TestEqual(TEXT("oak trunk: the log ends the leaves run"), int32(Trunk.CanopyBottom), 70);

        const FVoxelFarCell& SnowyCanopy = CellAt(6, 6);
        Test.TestEqual(
            TEXT("snow on the canopy: its top is the snow layer's"), int32(SnowyCanopy.CanopyTopState), int32(Snow));
        Test.TestEqual(
            TEXT("snow on the canopy: its sides are the leaves'"), int32(SnowyCanopy.CanopyState), int32(OakLeaves));

        const FVoxelFarCell& Bare = CellAt(12, 3);
        Test.TestEqual(TEXT("bare trunk: the ground is the grass, not the log top"), int32(Bare.GroundZ), 64);
        Test.TestFalse(TEXT("bare trunk: no canopy"), Bare.HasCanopy());
        Test.TestEqual(TEXT("bare trunk: the top is the grass block's"), int32(Bare.TopState), int32(Grass));

        const FVoxelFarCell& Pond = CellAt(14, 13);
        Test.TestEqual(TEXT("pond: the fluid's top cell"), int32(Pond.FluidZ), 63);
        Test.TestEqual(TEXT("pond: the fluid is water"), int32(Pond.FluidState), int32(Water));
        Test.TestEqual(TEXT("pond: the ground is the sand"), int32(Pond.GroundZ), 60);
        Test.TestEqual(TEXT("pond: the side is sand"), int32(Pond.SideState), int32(Sand));
        Test.TestEqual(TEXT("pond: the fill is sand"), int32(Pond.FillState), int32(Sand));

        const FVoxelFarCell& Held = CellAt(15, 15);
        Test.TestEqual(
            TEXT("a fluid-holding model on the pond: the fluid's top cell is the model's"), int32(Held.FluidZ), 64);
        Test.TestEqual(TEXT("a fluid-holding model on the pond: the fluid is the model's held fluid"),
            int32(Held.FluidState), HeldModel ? int32(Table.Get(HeldModel).HeldFluid) : -1);
        Test.TestEqual(TEXT("a fluid-holding model on the pond: the ground is the sand"), int32(Held.GroundZ), 60);

        // The land's mesh, alone: canopy on the cutout layer, the pond on the water's own layer, nothing on models.
        const FVoxelFarSummary* const NoNeighbors[4] = {};
        FVoxelMeshData LandMesh;
        VoxelFarMesher::Mesh(Table, *Summary, NoNeighbors, VoxelFarMesher::FOptions(), LandMesh);
        Test.TestTrue(
            TEXT("the canopy draws on the cutout layer"), !LandMesh.Layers[int32(EVoxelLayer::Cutout)].IsEmpty());
        Test.TestTrue(
            TEXT("the pond draws on the water's layer"), !LandMesh.Layers[int32(Table.Get(Water).Layer)].IsEmpty());
        Test.TestTrue(TEXT("nothing draws on the model layer"), LandMesh.Layers[int32(EVoxelLayer::Model)].IsEmpty());

        // Canopy bottoms: a bush whose leaves (65-66) sit right on the grass at 64 draws no down face; a floating
        // canopy (67-68) draws one, at its bottom.
        for (const bool bOnGround : {true, false})
        {
            TUniquePtr<FVoxelFarSummary> Bush = MakeUnique<FVoxelFarSummary>();
            FVoxelFarCell& Cell = Bush->Get(5, 4);
            Cell.GroundZ = 64;
            Cell.TopState = Cell.SideState = Grass;
            Cell.FillState = Dirt;
            Cell.TopTint = Cell.SideTint = Table.Get(Grass).Tint;
            Cell.FillTint = Table.Get(Dirt).Tint;
            Cell.CanopyBottom = int16(bOnGround ? 65 : 67);
            Cell.CanopyTop = int16(Cell.CanopyBottom + 1);
            Cell.CanopyState = Cell.CanopyTopState = OakLeaves;
            Cell.CanopyTint = Cell.CanopyTopTint = Table.Get(OakLeaves).Tint;
            Bush->LowestGround = 64;
            FVoxelMeshData BushMesh;
            VoxelFarMesher::Mesh(Table, *Bush, NoNeighbors, VoxelFarMesher::FOptions(), BushMesh);
            const FVoxelMeshLayer& BushLeaves = BushMesh.Layers[int32(EVoxelLayer::Cutout)];
            int32 Downs = 0, Under = 0;
            for (int32 Quad = 0; Quad < BushLeaves.GetQuadCount(); ++Quad)
            {
                if (int32(BushLeaves.TintFaces[Quad * 4].Y) != int32(EVoxelFace::Down)) continue;
                ++Downs;
                FVector3f Min = BushLeaves.Positions[Quad * 4], Max = Min;
                for (int32 Corner = 1; Corner < 4; ++Corner)
                {
                    Min = Min.ComponentMin(BushLeaves.Positions[Quad * 4 + Corner]);
                    Max = Max.ComponentMax(BushLeaves.Positions[Quad * 4 + Corner]);
                }
                Under += Min == FVector3f(5.f, 4.f, 67.f) && Max == FVector3f(6.f, 5.f, 67.f);
            }
            const TCHAR* const What = bOnGround ? TEXT("a canopy right on the ground") : TEXT("a floating canopy");
            Test.TestEqual(FString::Printf(TEXT("%s: down faces"), What), Downs, bOnGround ? 0 : 1);
            Test.TestEqual(FString::Printf(TEXT("%s: down faces under its bottom cell"), What), Under, Downs);
        }

        // Superflat: every cell the same layers, with its own summary on all four sides: one top quad per top state and
        // no walls.
        for (const bool bHalfSnow : {false, true})
        {
            const TSharedRef<FVoxelColumn> Flat = VoxelTestBlocks::MakeColumn(Table, FIntPoint::ZeroValue,
                [=](int32 X, int32, int32 Z) -> uint16
                {
                    if (Z == -64) return Stone;
                    if (Z < -61) return Dirt;
                    if (Z == -61) return Grass;
                    return Z == -60 && bHalfSnow && X < 8 ? Snow : uint16(0);
                });
            TUniquePtr<FVoxelFarSummary> FlatSummary = MakeUnique<FVoxelFarSummary>();
            VoxelFarMesher::Summarize(Classes, Table, *Flat, *FlatSummary);
            const FVoxelFarSummary* const Same[4] = {
                FlatSummary.Get(), FlatSummary.Get(), FlatSummary.Get(), FlatSummary.Get()};
            FVoxelMeshData FlatMesh;
            VoxelFarMesher::Mesh(Table, *FlatSummary, Same, VoxelFarMesher::FOptions(), FlatMesh);
            const int32 Want = bHalfSnow ? 2 : 1;
            const FVoxelMeshLayer& FlatOpaque = FlatMesh.Layers[int32(EVoxelLayer::Opaque)];
            Test.TestEqual(FString::Printf(
                               TEXT("Superflat%s: one quad per top state"), bHalfSnow ? TEXT(", half snow") : TEXT("")),
                FlatMesh.GetQuadCount(), Want);
            Test.TestEqual(TEXT("Superflat: the tops are on the opaque layer"), FlatOpaque.GetQuadCount(), Want);
            for (int32 Quad = 0; Quad < FlatOpaque.GetQuadCount(); ++Quad)
                Test.TestTrue(TEXT("Superflat: every quad is an up face on the grass's top"),
                    int32(FlatOpaque.TintFaces[Quad * 4].Y) == int32(EVoxelFace::Up) &&
                        FlatOpaque.Positions[Quad * 4].Z == -60.f);
        }
    }

    // Case 2: random height fields' walls cover every exposed unit face exactly once, with the right state, across
    // column borders and against missing neighbours; canopy sides cover exactly what their rule leaves open.
    void FarMesherTestWatertight(FAutomationTestBase& Test)
    {
        const FVoxelBlockTable Table = VoxelTestBlocks::Make();
        TArray<FVoxelFarSummary> Summaries;
        Summaries.SetNum(5);
        int32 Failures = 0, TopFailures = 0;
        int32 TopQuads[FarMesherTestParts] = {}, MergedTops[FarMesherTestParts] = {};
        int64 AllUnits = 0;
        constexpr int32 Runs = 24;
        for (int32 Run = 0; Run < Runs; ++Run)
        {
            FRandomStream Stream(7919 * (Run + 1));
            for (int32 Index = 0; Index < 5; ++Index)
            {
                const FIntPoint Position = Index == 0
                    ? FIntPoint::ZeroValue
                    : FIntPoint(FarMesherTestStepX[Index - 1], FarMesherTestStepY[Index - 1]);
                FarMesherTestRandomSummary(Stream, Position, (Run + Index) % 2 == 1, Summaries[Index]);
            }
            // Some runs miss some neighbours; the last misses all four.
            const int32 NullMask = Run == Runs - 1 ? 15 : Run % 3 == 2 ? (Run * 5 + 1) % 16 : 0;
            const FVoxelFarSummary* Neighbors[4];
            for (int32 Side = 0; Side < 4; ++Side)
                Neighbors[Side] = NullMask & (1 << Side) ? nullptr : &Summaries[Side + 1];
            const FVoxelFarSummary& Center = Summaries[0];
            FVoxelMeshData Mesh;
            VoxelFarMesher::Mesh(Table, Center, Neighbors, VoxelFarMesher::FOptions(), Mesh);

            // Ground walls: the opaque layer's vertical quads.
            const FVoxelMeshLayer& Opaque = Mesh.Layers[int32(EVoxelLayer::Opaque)];
            TMap<int64, int32> Walls;
            int64 WallUnits = 0;
            int32 Outside = 0, WrongStates = 0;
            FarMesherTestRasterize(Opaque,
                [&](int32 Quad, int32 Side, int32 X, int32 Y, int32 Z)
                {
                    if (X < 0 || X >= 16 || Y < 0 || Y >= 16)
                    {
                        ++Outside;
                        return;
                    }
                    ++Walls.FindOrAdd(FarMesherTestUnitKey(Side, X, Y, Z));
                    ++WallUnits;
                    const FVoxelFarCell& Cell = Center.Get(X, Y);
                    const uint16 Want = Z == Cell.GroundZ ? Cell.SideState : Cell.FillState;
                    if (uint32(Opaque.Codes[Quad * 4].X) != Table.Get(Want).Faces[Side]) ++WrongStates;
                });
            // Canopy sides: the cutout layer's vertical quads.
            TMap<int64, int32> Canopies;
            int64 CanopyUnits = 0;
            FarMesherTestRasterize(Mesh.Layers[int32(EVoxelLayer::Cutout)],
                [&](int32, int32 Side, int32 X, int32 Y, int32 Z)
                {
                    if (X < 0 || X >= 16 || Y < 0 || Y >= 16)
                    {
                        ++Outside;
                        return;
                    }
                    ++Canopies.FindOrAdd(FarMesherTestUnitKey(Side, X, Y, Z));
                    ++CanopyUnits;
                });

            int64 WallsWanted = 0, CanopiesWanted = 0;
            int32 Missing = 0, Doubled = 0;
            auto Expect = [&Missing, &Doubled](const TMap<int64, int32>& Drawn, int64 Key)
            {
                const int32* Count = Drawn.Find(Key);
                if (!Count)
                    ++Missing;
                else if (*Count != 1)
                    ++Doubled;
            };
            for (int32 Y = 0; Y < 16; ++Y)
                for (int32 X = 0; X < 16; ++X)
                {
                    const FVoxelFarCell& Cell = Center.Get(X, Y);
                    for (int32 Side = 0; Side < 4; ++Side)
                    {
                        const FVoxelFarCell* Next = FarMesherTestNeighborCell(Center, Neighbors, X, Y, Side);
                        if (Cell.HasGround())
                        {
                            const int32 NextZ =
                                Next && Next->HasGround() ? int32(Next->GroundZ) : int32(Center.LowestGround) - 1;
                            for (int32 Z = NextZ + 1; Z <= Cell.GroundZ; ++Z, ++WallsWanted)
                                Expect(Walls, FarMesherTestUnitKey(Side, X, Y, Z));
                        }
                        if (Cell.HasCanopy())
                            for (int32 Z = Cell.CanopyBottom; Z <= Cell.CanopyTop; ++Z)
                            {
                                const bool bUnderGround = Next && Next->HasGround() && Z <= Next->GroundZ;
                                const bool bInCanopy =
                                    Next && Next->HasCanopy() && Z >= Next->CanopyBottom && Z <= Next->CanopyTop;
                                if (bUnderGround || bInCanopy) continue;
                                ++CanopiesWanted;
                                Expect(Canopies, FarMesherTestUnitKey(Side, X, Y, Z));
                            }
                    }
                }
            // Horizontal parts: ground tops on the opaque layer, canopy tops and bottoms on the canopy layer, fluid
            // tops on the water's layer. No quad merges cells of another height, state or tint, and each cell's part
            // has exactly one quad over it (no canopy bottom where ground lies right under it).
            const FVoxelMeshLayer& CanopyLayer = Mesh.Layers[int32(EVoxelLayer::Cutout)];
            const FVoxelMeshLayer& WaterLayer = Mesh.Layers[int32(Table.Get(VoxelTestBlocks::WaterSource).Layer)];
            const FVoxelMeshLayer* const PartLayers[FarMesherTestParts] = {
                &Opaque, &CanopyLayer, &CanopyLayer, &WaterLayer};
            const EVoxelFace PartFaces[FarMesherTestParts] = {
                EVoxelFace::Up, EVoxelFace::Up, EVoxelFace::Down, EVoxelFace::Up};
            int32 Covered[FarMesherTestParts][16 * 16] = {};
            int32 WrongTops = 0, Uncovered = 0, Overdrawn = 0;
            FString FirstWrong, PartWrong, FirstGap;
            for (int32 Part = 0; Part < FarMesherTestParts; ++Part)
            {
                WrongTops += FarMesherTestCheckTops(Table, Center, *PartLayers[Part], PartFaces[Part], Part,
                    Covered[Part], TopQuads[Part], MergedTops[Part], PartWrong);
                if (FirstWrong.IsEmpty()) FirstWrong = PartWrong;
                for (int32 Index = 0; Index < 16 * 16; ++Index)
                {
                    float Plane;
                    uint16 State;
                    uint32 Tint;
                    const bool bHas = FarMesherTestPartOf(Table, Center.Cells[Index], Part, Plane, State, Tint);
                    if (Covered[Part][Index] == int32(bHas)) continue;
                    (Covered[Part][Index] < int32(bHas) ? Uncovered : Overdrawn)++;
                    if (FirstGap.IsEmpty())
                        FirstGap = FString::Printf(TEXT("%s at (%d, %d): %d quads over it, %d wanted"),
                            FarMesherTestPartNames[Part], Index % 16, Index / 16, Covered[Part][Index], int32(bHas));
                }
            }
            if (WrongTops || Uncovered || Overdrawn)
            {
                ++TopFailures;
                Test.AddError(FString::Printf(TEXT("tops run %d (missing neighbours 0x%x): %d cells under a quad of "
                                                   "another height, state or tint (first: %s); %d parts with no quad "
                                                   "over them and %d with more than one (first: %s)"),
                    Run, NullMask, WrongTops, *FirstWrong, Uncovered, Overdrawn, *FirstGap));
            }

            AllUnits += WallUnits + CanopyUnits;
            if (Missing || Doubled || Outside || WrongStates || WallUnits != WallsWanted ||
                CanopyUnits != CanopiesWanted)
            {
                ++Failures;
                Test.AddError(
                    FString::Printf(TEXT("watertight run %d (missing neighbours 0x%x): walls %lld drawn of %lld, "
                                         "canopy sides %lld drawn of %lld; %d missing, %d drawn twice, %d "
                                         "outside the column, %d with the wrong state"),
                        Run, NullMask, WallUnits, WallsWanted, CanopyUnits, CanopiesWanted, Missing, Doubled, Outside,
                        WrongStates));
            }
        }
        Test.TestEqual(
            TEXT("every random field's walls and canopy sides cover each open face exactly once"), Failures, 0);
        Test.AddInfo(FString::Printf(TEXT("watertight: %d runs, %lld unit faces checked"), Runs, AllUnits));
        Test.TestEqual(TEXT("every top and bottom quad covers only cells of its own height, state and tint, and each "
                            "cell's part exactly once"),
            TopFailures, 0);
        FString TopCounts;
        for (int32 Part = 0; Part < FarMesherTestParts; ++Part)
            TopCounts += FString::Printf(TEXT("%s%s %d (%d merged)"), Part ? TEXT(", ") : TEXT(""),
                FarMesherTestPartNames[Part], TopQuads[Part], MergedTops[Part]);
        Test.AddInfo(FString::Printf(TEXT("tops: %s quads checked"), *TopCounts));
    }

    // Case 3: a flat grass_block field (in a jungle, when the biome reference reads, so the tint is not plains) meshed
    // by the full mesher and the far mesher: the merged top quads match vertex for vertex.
    void FarMesherTestSameLayout(FAutomationTestBase& Test, const FVoxelBlockTable& Table)
    {
        const uint16 Stone = Table.FindState(TEXT("stone")), Dirt = Table.FindState(TEXT("dirt")),
                     Grass = Table.FindState(TEXT("grass_block"));
        FVoxelTerrainPreset Preset;
        FVoxelBiomeTable Biomes;
        FVoxelSurfaceInputs Surface;
        FVoxelTintTable TintTable;
        FString Error;
        int32 Jungle = INDEX_NONE;
        if (VoxelTerrainFactory::ReadInputs(
                VoxelWorldgen::GetReferenceRoot(), TEXT("overworld"), Preset, Biomes, Surface, Error) &&
            TintTable.Initialize(Biomes, &Error))
            Jungle = Biomes.FindBiome(TEXT("minecraft:jungle"));
        if (Jungle == INDEX_NONE)
            Test.AddWarning(
                FString::Printf(TEXT("no jungle tints (%s): the layout case compares plains tints"), *Error));
        const FVoxelTintTable* Tints = Jungle == INDEX_NONE ? nullptr : &TintTable;

        TArray<TSharedRef<FVoxelColumn>> Field;
        for (int32 DY = -1; DY <= 1; ++DY)
            for (int32 DX = -1; DX <= 1; ++DX)
            {
                TSharedRef<FVoxelColumn> FieldColumn = VoxelTestBlocks::MakeColumn(Table, FIntPoint(DX, DY),
                    [=](int32, int32, int32 Z) -> uint16
                    {
                        return Z < 60 ? Stone : Z < 63 ? Dirt : Z == 63 ? Grass : uint16(0);
                    });
                if (Jungle != INDEX_NONE)
                {
                    FVoxelBiomeStorage Storage;
                    Storage.Init(FIntPoint(DX, DY), 263);
                    for (int32 QZ = 0; QZ < FVoxelBiomeStorage::Layers; ++QZ)
                        for (int32 QY = 0; QY < FVoxelBiomeStorage::Width; ++QY)
                            for (int32 QX = 0; QX < FVoxelBiomeStorage::Width; ++QX)
                                Storage.SetQuart(QX, QY, QZ, uint8(Jungle));
                    FieldColumn->SetBiomes(MoveTemp(Storage));
                }
                Field.Add(FieldColumn);
            }
        FVoxelNeighborhood Hood;
        for (int32 DY = -1; DY <= 1; ++DY)
            for (int32 DX = -1; DX <= 1; ++DX)
                Hood.Set(DX, DY, &Field[(DY + 1) * 3 + DX + 1].Get());

        const FVoxelMeshOptions MeshOptions;
        const int32 Section = (63 - VoxelConstants::MinZ) / VoxelConstants::SectionSize;
        TUniquePtr<FVoxelPaddedSection> Padded = MakeUnique<FVoxelPaddedSection>();
        VoxelMesher::GatherPadded(Hood, Section, *Padded);
        FVoxelMeshData Full;
        VoxelMesher::MeshSection(Table, *Padded, Section, MeshOptions, Full, Tints, &Hood);

        const FVoxelFarClasses Classes(Table);
        TUniquePtr<FVoxelFarSummary> Summary = MakeUnique<FVoxelFarSummary>();
        VoxelFarMesher::Summarize(Classes, Table, Field[4].Get(), *Summary, Tints, &Hood, MeshOptions);
        const FVoxelFarSummary* const Same[4] = {Summary.Get(), Summary.Get(), Summary.Get(), Summary.Get()};
        FVoxelMeshData Far;
        VoxelFarMesher::Mesh(Table, *Summary, Same, VoxelFarMesher::FOptions(), Far);

        const FVoxelMeshLayer& FullLayer = Full.Layers[int32(EVoxelLayer::Opaque)];
        const FVoxelMeshLayer& FarLayer = Far.Layers[int32(EVoxelLayer::Opaque)];
        const int32 FullTop = FarMesherTestFindTop(FullLayer, 64.f), FarTop = FarMesherTestFindTop(FarLayer, 64.f);
        Test.TestEqual(TEXT("layout: the far field is one quad"), Far.GetQuadCount(), 1);
        if (!Test.TestTrue(
                TEXT("layout: both meshes have the field's top"), FullTop != INDEX_NONE && FarTop != INDEX_NONE))
            return;
        int32 Matched = 0;
        for (int32 Corner = 0; Corner < 4; ++Corner)
        {
            const int32 FarVertex = FarTop * 4 + Corner;
            int32 FullVertex = INDEX_NONE;
            for (int32 Other = FullTop * 4; Other < FullTop * 4 + 4; ++Other)
                if (FullLayer.Positions[Other] == FarLayer.Positions[FarVertex]) FullVertex = Other;
            if (FullVertex == INDEX_NONE)
            {
                Test.AddError(FString::Printf(TEXT("layout: the full top has no corner at the far top's %s (the meshes "
                                                   "merged differently)"),
                    *FarLayer.Positions[FarVertex].ToString()));
                continue;
            }
            const bool bSame = FullLayer.BlockUVs[FullVertex] == FarLayer.BlockUVs[FarVertex] &&
                FullLayer.Codes[FullVertex] == FarLayer.Codes[FarVertex] &&
                FullLayer.TintFaces[FullVertex] == FarLayer.TintFaces[FarVertex] &&
                FullLayer.Colors[FullVertex] == FarLayer.Colors[FarVertex];
            if (!bSame)
                Test.AddError(
                    FString::Printf(TEXT("layout: corner %s differs: block UV %s / %s, codes %s / %s, tint and "
                                         "face %s / %s (full / far)"),
                        *FarLayer.Positions[FarVertex].ToString(), *FullLayer.BlockUVs[FullVertex].ToString(),
                        *FarLayer.BlockUVs[FarVertex].ToString(), *FullLayer.Codes[FullVertex].ToString(),
                        *FarLayer.Codes[FarVertex].ToString(), *FullLayer.TintFaces[FullVertex].ToString(),
                        *FarLayer.TintFaces[FarVertex].ToString()));
            Matched += bSame;
        }
        Test.TestEqual(TEXT("layout: the far top's corners match the full top's"), Matched, 4);

        // The far top's corner order, its two triangles (indices from its first vertex) and its colours: every corner
        // open (R 255), G the grass's emission.
        int32 InOrder = 0, Open = 0;
        for (int32 Corner = 0; Corner < 4; ++Corner)
        {
            InOrder += FullLayer.Positions[FullTop * 4 + Corner] == FarLayer.Positions[FarTop * 4 + Corner];
            const FColor Color = FarLayer.Colors[FarTop * 4 + Corner];
            Open += Color.R == 255 && Color.G == uint8(Table.Get(Grass).LightEmission * 17);
        }
        Test.TestEqual(TEXT("layout: the far top's corners come in the full top's order"), InOrder, 4);
        Test.TestEqual(TEXT("layout: every far top corner is open and lit by the grass's emission"), Open, 4);
        if (Test.TestTrue(TEXT("layout: both tops have their six indices"),
                FullLayer.Indices.Num() >= FullTop * 6 + 6 && FarLayer.Indices.Num() >= FarTop * 6 + 6))
        {
            FString FullTriangles, FarTriangles;
            int32 SameIndices = 0;
            for (int32 Index = 0; Index < 6; ++Index)
            {
                const int32 FullIndex = int32(FullLayer.Indices[FullTop * 6 + Index]) - FullTop * 4;
                const int32 FarIndex = int32(FarLayer.Indices[FarTop * 6 + Index]) - FarTop * 4;
                SameIndices += FullIndex == FarIndex;
                FullTriangles += FString::Printf(TEXT(" %d"), FullIndex);
                FarTriangles += FString::Printf(TEXT(" %d"), FarIndex);
            }
            if (!Test.TestEqual(TEXT("layout: the far top's triangles match the full top's"), SameIndices, 6))
                Test.AddError(FString::Printf(
                    TEXT("layout: indices from the first vertex:%s (full) /%s (far)"), *FullTriangles, *FarTriangles));
        }
        Test.AddInfo(FString::Printf(TEXT("layout: grass top tint 0x%06x (plains 0x%06x)"),
            uint32(FarLayer.TintFaces[FarTop * 4].X), Table.Get(Grass).Tint));
    }

    // Case 4: the dev seed's decorated 3 x 3 at the origin, far-meshed column by column, every byte pinned.
    void FarMesherTestDigest(FAutomationTestBase& Test, const FVoxelBlockTable& Table)
    {
        FString Error;
        FVoxelTerrainPreset TerrainPreset;
        FVoxelBiomeTable Biomes;
        FVoxelSurfaceInputs Surface;
        FVoxelFeatureGraph Graph;
        if (!Test.TestTrue(FString::Printf(TEXT("digest: overworld inputs read (%s)"), *Error),
                VoxelTerrainFactory::ReadInputs(
                    VoxelWorldgen::GetReferenceRoot(), TEXT("overworld"), TerrainPreset, Biomes, Surface, Error)) ||
            !Test.TestTrue(FString::Printf(TEXT("digest: the decoration data reads (%s)"), *Error),
                VoxelWorldgen::ReadFeatures(VoxelWorldgen::GetReferenceRoot(), Graph, Error)))
            return;
        const TSharedRef<FVoxelDensityTerrain> Terrain = MakeShared<FVoxelDensityTerrain>(
            TerrainPreset, 263, Table, FVoxelDensityTerrainSettings(), &Biomes, &Surface);
        FVoxelDecorator Decorator;
        FVoxelDecorationOptions DecorationOptions;
        DecorationOptions.bPlants = true;
        DecorationOptions.bTrees = true;
        if (!Test.TestTrue(TEXT("digest: the terrain is valid"), Terrain->IsValid()) ||
            !Test.TestTrue(FString::Printf(TEXT("digest: the decorator compiles (%s)"), *Error),
                Decorator.Initialize(Graph, Table, Terrain->GetBiomeSource()->GetTable(), Terrain->GetSeed(),
                    DecorationOptions, &Error)))
            return;
        FVoxelDecoratedArea Area;
        FVoxelDecoratedArea::Build(Decorator, *Terrain, FIntPoint::ZeroValue, FarMesherTestRadius, true, false, Area);
        if (!Test.TestEqual(
                TEXT("digest: the area's columns"), Area.Columns.Num(), FarMesherTestWidth * FarMesherTestWidth))
            return;
        auto IndexOf = [](int32 CX, int32 CY)
        { return (CY + FarMesherTestRadius) * FarMesherTestWidth + CX + FarMesherTestRadius; };

        // Neighbours' summaries need only heights and states: untinted.
        const FVoxelFarClasses Classes(Table);
        TArray<FVoxelFarSummary> Plain;
        Plain.SetNum(Area.Columns.Num());
        for (int32 Index = 0; Index < Area.Columns.Num(); ++Index)
            VoxelFarMesher::Summarize(Classes, Table, *Area.Columns[Index], Plain[Index]);

        TUniquePtr<FVoxelFarSummary> Tinted = MakeUnique<FVoxelFarSummary>();
        const FVoxelMeshOptions MeshOptions;
        uint64 Hash = 0;
        int32 MaxQuads = 0, Meshed = 0;
        int64 AllQuads = 0;
        double AllMs = 0, MaxMs = 0;
        for (int32 CY = -1; CY <= 1; ++CY)
            for (int32 CX = -1; CX <= 1; ++CX)
            {
                FVoxelNeighborhood Hood;
                for (int32 DY = -1; DY <= 1; ++DY)
                    for (int32 DX = -1; DX <= 1; ++DX)
                        Hood.Set(DX, DY, Area.Columns[IndexOf(CX + DX, CY + DY)].Get());
                const FVoxelFarSummary* Neighbors[4];
                for (int32 Side = 0; Side < 4; ++Side)
                    Neighbors[Side] = &Plain[IndexOf(CX + FarMesherTestStepX[Side], CY + FarMesherTestStepY[Side])];
                FVoxelMeshData FarMesh;
                const double Began = FPlatformTime::Seconds();
                VoxelFarMesher::Summarize(
                    Classes, Table, *Area.Columns[IndexOf(CX, CY)], *Tinted, Terrain->GetTints(), &Hood, MeshOptions);
                VoxelFarMesher::Mesh(Table, *Tinted, Neighbors, VoxelFarMesher::FOptions(), FarMesh);
                const double Ms = (FPlatformTime::Seconds() - Began) * 1000.0;
                Test.TestTrue(FString::Printf(TEXT("digest: column (%d, %d) has ground"), CX, CY),
                    !FarMesh.Layers[int32(EVoxelLayer::Opaque)].IsEmpty());
                FarMesherTestHash(FarMesh, Meshed, Hash);
                const int32 Quads = FarMesh.GetQuadCount();
                AllQuads += Quads;
                MaxQuads = FMath::Max(MaxQuads, Quads);
                AllMs += Ms;
                MaxMs = FMath::Max(MaxMs, Ms);
                ++Meshed;
            }
        Test.AddInfo(FString::Printf(TEXT("digest: %d columns, quads per column mean %.1f, max %d; summary + mesh per "
                                          "column mean %.3f ms, max %.3f ms"),
            Meshed, double(AllQuads) / Meshed, MaxQuads, AllMs / Meshed, MaxMs));
        // Pinned from the first run (2026-10-11, far LOD step 1); 0 would fail with the value to pin.
        constexpr uint64 FarMesherExpectedDigest = 0x21cb3f5108dbe83bull;
        const bool bPinned = FarMesherExpectedDigest != 0 && Hash == FarMesherExpectedDigest;
        Test.TestTrue(
            FString::Printf(TEXT("digest: far mesh digest 0x%016llx is the pinned 0x%016llx (0 is unpinned: pin "
                                 "the actual value)"),
                Hash, FarMesherExpectedDigest),
            bPinned);
    }
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVoxelFarMesherTest, "Unnamed.Voxel.FarMesher",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FVoxelFarMesherTest::RunTest(const FString& Parameters)
{
    // Case 2 runs on the test blocks; the others need the imported 26.3 blocks.
    FarMesherTestWatertight(*this);
    const TSharedRef<const FVoxelBlockTable> BlockTable = VoxelBlocks::GetDefault();
    if (!BlockTable->FindState(TEXT("stone")))
    {
        AddWarning(TEXT("DA_Blocks is missing: run ImportBlocks.py (only the watertight case ran)"));
        return true;
    }
    FarMesherTestSummaryRules(*this, *BlockTable);
    FarMesherTestSameLayout(*this, *BlockTable);
    FarMesherTestDigest(*this, *BlockTable);
    return true;
}

#endif
