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

    // Tree coverage is checked against the input voxels, not the run list or the mesher's culling helpers. Keys
    // identify an oriented unit face by its owning world cell, so opposing internal border caps are distinguishable
    // from a duplicate external face. This fixture stays within [-256, 255] in X/Y and the normal world Z range.
    uint64 FarTreeTestFaceKey(const FIntVector& Cell, int32 Face)
    {
        return ((uint64(Face) * 512 + Cell.X + 256) * 512 + Cell.Y + 256) * 512 + Cell.Z + 64;
    }

    void FarMesherTestTrees(FAutomationTestBase& Test)
    {
        FVoxelBlockTable Table = VoxelTestBlocks::Make();
        constexpr uint16 LogY = VoxelTestBlocks::Count, LogX = LogY + 1, LogZ = LogY + 2, OtherLeaves = LogY + 3,
                         CropStem = LogY + 4, AttachedStem = LogY + 5;
        for (uint16 State : {LogY, LogX, LogZ})
        {
            FVoxelStateInfo Info = Table.Get(VoxelTestBlocks::Stone);
            Info.Block = 8;
            Info.Tint = 0x102030 + State * 0x010101;
            for (int32 Face = 0; Face < 6; ++Face)
                Info.Faces[Face] = uint32(State * 8 + Face) | VoxelFaces::CullBit;
            Table.SetState(State, Info);
        }
        // Three states of one log block stand in for its three axes; every face code is different.
        Table.SetDefaultState(TEXT("oak_log"), LogY);
        FVoxelStateInfo Leaves = Table.Get(VoxelTestBlocks::Leaves);
        Leaves.Tint = 0x246824;
        Table.SetState(VoxelTestBlocks::Leaves, Leaves);
        Leaves.Block = 9;
        Leaves.Tint = 0xA84B26;
        for (int32 Face = 0; Face < 6; ++Face)
            Leaves.Faces[Face] = uint32(OtherLeaves * 8 + Face) | VoxelFaces::CullBit;
        Table.SetState(OtherLeaves, Leaves);
        Table.SetDefaultState(TEXT("birch_leaves"), OtherLeaves);
        FVoxelStateInfo Snow = Table.Get(VoxelTestBlocks::Snow3);
        Snow.Tint = 0xE1F0FE;
        Table.SetState(VoxelTestBlocks::Snow3, Snow);
        // A suffix alone does not make vegetation into wood: crop stems are models, not full trunk cubes.
        for (uint16 State : {CropStem, AttachedStem})
        {
            FVoxelStateInfo Model;
            Model.Shape = EVoxelShape::Model;
            Model.Layer = EVoxelLayer::Model;
            Model.Block = State == CropStem ? 10 : 11;
            for (int32 Face = 0; Face < 6; ++Face)
                Model.Faces[Face] = uint32(State * 8 + Face);
            Table.SetState(State, Model);
        }
        Table.SetDefaultState(TEXT("minecraft:pumpkin_stem"), CropStem);
        Table.SetDefaultState(TEXT("minecraft:attached_melon_stem"), AttachedStem);
        const FVoxelFarClasses Classes(Table);
        Test.TestTrue(TEXT("trees: all three states of a log block are recognized"),
            Classes.IsLog(LogY) && Classes.IsLog(LogX) && Classes.IsLog(LogZ));
        Test.TestTrue(TEXT("trees: log recognition leaves legacy classification unchanged"),
            Classes.Get(LogY) == EVoxelFarClass::Skip && Classes.Get(LogX) == EVoxelFarClass::Skip &&
                Classes.Get(LogZ) == EVoxelFarClass::Skip);
        Test.TestFalse(TEXT("trees: a pumpkin crop stem model is not full-cube wood"), Classes.IsLog(CropStem));
        Test.TestFalse(TEXT("trees: an attached melon stem model is not full-cube wood"), Classes.IsLog(AttachedStem));
        Test.TestTrue(TEXT("trees: crop stem legacy skip classification is preserved"),
            Classes.Get(CropStem) == EVoxelFarClass::Skip && Classes.Get(AttachedStem) == EVoxelFarClass::Skip);

        // Center (-2, -3): trees cross each of its four sides and its southeast corner. The last two crowns
        // overlap, but the voxel field has one owner per occupied cell. All coordinates are deliberately negative.
        const FIntPoint Center(-2, -3);
        const FIntVector Origin(Center.X * 16, Center.Y * 16, 0);
        const FIntPoint Roots[] = {{0, 7}, {15, 7}, {7, 0}, {7, 15}, {15, 15}, {4, 5}, {7, 5}};
        TMap<FIntVector, uint16> Voxels;
        auto Put = [&Voxels, &Origin](int32 X, int32 Y, int32 Z, uint16 State)
        { Voxels.Add(Origin + FIntVector(X, Y, Z), State); };
        for (int32 RootIndex = 0; RootIndex < UE_ARRAY_COUNT(Roots); ++RootIndex)
        {
            const FIntPoint Root = Roots[RootIndex];
            for (int32 Tier = 0; Tier < 3; ++Tier)
                for (int32 DY = -2; DY <= 2; ++DY)
                    for (int32 DX = -2; DX <= 2; ++DX)
                        for (int32 DZ = 0; DZ < 2; ++DZ)
                            Put(Root.X + DX, Root.Y + DY, 68 + RootIndex % 5 + Tier * 4 + DZ,
                                Tier == 1 ? OtherLeaves : uint16(VoxelTestBlocks::Leaves));
        }
        for (int32 RootIndex = 0; RootIndex < UE_ARRAY_COUNT(Roots); ++RootIndex)
        {
            const FIntPoint Root = Roots[RootIndex];
            for (int32 Z = 65; Z <= 77 + RootIndex % 5; ++Z)
                Put(Root.X, Root.Y, Z, LogY);
            for (int32 DX = -2; DX <= 2; ++DX)
                Put(Root.X + DX, Root.Y, 72 + RootIndex % 5, LogX);
            for (int32 DY = -2; DY <= 2; ++DY)
                Put(Root.X, Root.Y + DY, 76 + RootIndex % 5, LogZ);
        }
        // Twelve separate leaf runs defeat a fixed eight-run representation. They are far above the other crowns.
        for (int32 Run = 0; Run < 12; ++Run)
            Put(11, 11, 90 + Run * 3, Run & 1 ? OtherLeaves : uint16(VoxelTestBlocks::Leaves));
        // Snow changes only the exposed top's state/tint. Also exercise a bare stem and adjacent axis transitions.
        Put(2, 11, 68, VoxelTestBlocks::Leaves);
        Put(2, 11, 69, VoxelTestBlocks::Snow3);
        Put(3, 11, 68, OtherLeaves);
        Put(3, 11, 69, VoxelTestBlocks::Snow3);
        Put(4, 11, 65, LogY);
        Put(4, 11, 66, LogX);
        Put(4, 11, 67, LogZ);
        Put(4, 11, 68, VoxelTestBlocks::Snow3);
        // The independent output checker rejects any tree box emitted for either model.
        Put(5, 11, 65, CropStem);
        Put(6, 11, 65, AttachedStem);
        auto StateAt = [&Voxels](const FIntVector& Cell) -> uint16
        {
            if (Cell.Z <= 64) return VoxelTestBlocks::Stone;
            const uint16* State = Voxels.Find(Cell);
            return State ? *State : uint16(0);
        };
        auto IsTree = [=](uint16 State)
        {
            return State == LogY || State == LogX || State == LogZ || State == VoxelTestBlocks::Leaves ||
                State == OtherLeaves;
        };
        auto IsLeaf = [=](uint16 State) { return State == VoxelTestBlocks::Leaves || State == OtherLeaves; };
        const FIntVector Steps[6] = {{-1, 0, 0}, {1, 0, 0}, {0, -1, 0}, {0, 1, 0}, {0, 0, -1}, {0, 0, 1}};
        TArray<TSharedRef<FVoxelColumn>> Columns;
        TArray<FVoxelFarSummary> Summaries;
        TArray<FVoxelFarSummary> LegacySummaries;
        Summaries.SetNum(9);
        LegacySummaries.SetNum(9);
        for (int32 DY = -1; DY <= 1; ++DY)
            for (int32 DX = -1; DX <= 1; ++DX)
            {
                const FIntPoint Position = Center + FIntPoint(DX, DY);
                Columns.Add(VoxelTestBlocks::MakeColumn(Table, Position, [=, &StateAt](int32 X, int32 Y, int32 Z)
                    { return StateAt(FIntVector(Position.X * 16 + X, Position.Y * 16 + Y, Z)); }));
                VoxelFarMesher::Summarize(Classes, Table, Columns.Last().Get(), Summaries[(DY + 1) * 3 + DX + 1],
                    nullptr, nullptr, FVoxelMeshOptions(), true);
                VoxelFarMesher::Summarize(Classes, Table, Columns.Last().Get(), LegacySummaries[(DY + 1) * 3 + DX + 1]);
            }
        // This is supplementary storage validation; the output coverage assertions below remain independent.
        Test.TestEqual(TEXT("trees: all twelve separated crown tiers survive the summary"),
            Summaries[4].GetTreeRuns(11, 11).Num(), 12);
        Test.TestTrue(TEXT("trees: model crop stems produce no wood runs"),
            Summaries[4].GetTreeRuns(5, 11).IsEmpty() && Summaries[4].GetTreeRuns(6, 11).IsEmpty());
        TUniquePtr<FVoxelFarSummary> Reused = MakeUnique<FVoxelFarSummary>();
        VoxelFarMesher::Summarize(
            Classes, Table, Columns[4].Get(), *Reused, nullptr, nullptr, FVoxelMeshOptions(), true);
        VoxelFarMesher::Summarize(Classes, Table, Columns[4].Get(), *Reused);
        Test.TestTrue(TEXT("trees: summarizing with the switch off clears previously collected tree runs"),
            !Reused->bTrees && Reused->TreeRuns.IsEmpty() && Reused->GetTreeRuns(11, 11).IsEmpty());

        // With unavailable neighbours, internal border caps are deliberately conservative. They may also exist
        // with summaries: a far column cannot assume the neighbour currently displays the same representation.
        uint64 NewNeighborHash[2] = {}, LegacyCenterHash[2] = {};
        const TCHAR* NeighborNames[] = {TEXT("tree1"), TEXT("tree0"), TEXT("mixed tree0/tree1"), TEXT("missing")};
        for (const bool bOpaqueLeaves : {false, true})
            for (int32 NeighborMode = 0; NeighborMode < 4; ++NeighborMode)
            {
                VoxelFarMesher::FOptions Options;
                Options.bTrees = true;
                Options.CanopyLayer = bOpaqueLeaves ? EVoxelLayer::Opaque : EVoxelLayer::Cutout;
                const FString Scenario = FString::Printf(TEXT("trees %s %s neighbours"),
                    bOpaqueLeaves ? TEXT("opaque") : TEXT("cutout"), NeighborNames[NeighborMode]);
                uint64 MeshHash = 0, LegacyHash = 0;
                TMap<uint64, int32> Counts;
                int32 Wrong = 0, Duplicate = 0, Missing = 0, Internal = 0, Quads = 0;
                FString FirstWrong;
                auto Fail = [&Wrong, &FirstWrong](const FString& Reason)
                {
                    if (!Wrong++) FirstWrong = Reason;
                };
                for (int32 DY = -1; DY <= 1; ++DY)
                    for (int32 DX = -1; DX <= 1; ++DX)
                    {
                        const FVoxelFarSummary* Neighbors[4] = {};
                        for (int32 Face = 0; Face < 4; ++Face)
                        {
                            const int32 NX = DX + Steps[Face].X, NY = DY + Steps[Face].Y;
                            if (NeighborMode < 3 && FMath::Abs(NX) <= 1 && FMath::Abs(NY) <= 1)
                            {
                                const int32 Index = (NY + 1) * 3 + NX + 1;
                                const bool bLegacy = NeighborMode == 1 || (NeighborMode == 2 && (Index & 1));
                                Neighbors[Face] = bLegacy ? &LegacySummaries[Index] : &Summaries[Index];
                            }
                        }
                        FVoxelMeshData Mesh;
                        VoxelFarMesher::Mesh(Table, Summaries[(DY + 1) * 3 + DX + 1], Neighbors, Options, Mesh);
                        FarMesherTestHash(Mesh, (DY + 1) * 3 + DX + 1, MeshHash);
                        VoxelFarMesher::FOptions LegacyOptions = Options;
                        LegacyOptions.bTrees = false;
                        FVoxelMeshData LegacyMesh;
                        VoxelFarMesher::Mesh(
                            Table, LegacySummaries[(DY + 1) * 3 + DX + 1], Neighbors, LegacyOptions, LegacyMesh);
                        FarMesherTestHash(LegacyMesh, (DY + 1) * 3 + DX + 1, LegacyHash);
                        Quads += Mesh.GetQuadCount();
                        const FIntPoint Position = Center + FIntPoint(DX, DY);
                        for (int32 LayerIndex = 0; LayerIndex < int32(EVoxelLayer::Count); ++LayerIndex)
                        {
                            const FVoxelMeshLayer& Layer = Mesh.Layers[LayerIndex];
                            for (int32 Quad = 0; Quad < Layer.GetQuadCount(); ++Quad)
                            {
                                const int32 Face = int32(Layer.TintFaces[Quad * 4].Y);
                                if (Face < 0 || Face >= 6)
                                {
                                    Fail(TEXT("face direction outside 0..5"));
                                    continue;
                                }
                                const int32 Axis = Face / 2, U = (Axis + 1) % 3, V = (Axis + 2) % 3;
                                FVector3f Min = Layer.Positions[Quad * 4], Max = Min;
                                for (int32 Corner = 1; Corner < 4; ++Corner)
                                {
                                    Min = Min.ComponentMin(Layer.Positions[Quad * 4 + Corner]);
                                    Max = Max.ComponentMax(Layer.Positions[Quad * 4 + Corner]);
                                }
                                const int32 Plane = FMath::RoundToInt(Min[Axis]);
                                const bool bUnitAligned = Min[Axis] == Max[Axis] && Min[Axis] == float(Plane) &&
                                    Min[U] == float(FMath::RoundToInt(Min[U])) &&
                                    Max[U] == float(FMath::RoundToInt(Max[U])) &&
                                    Min[V] == float(FMath::RoundToInt(Min[V])) &&
                                    Max[V] == float(FMath::RoundToInt(Max[V]));
                                if (!bUnitAligned)
                                    Fail(TEXT("tree/ground quad not axis aligned to unit cell boundaries"));
                                for (int32 B = FMath::RoundToInt(Min[V]); B < FMath::RoundToInt(Max[V]); ++B)
                                    for (int32 A = FMath::RoundToInt(Min[U]); A < FMath::RoundToInt(Max[U]); ++A)
                                    {
                                        FIntVector Local;
                                        Local[Axis] = Plane - (Face & 1);
                                        Local[U] = A;
                                        Local[V] = B;
                                        if (Local.Z <= 64) continue; // The unchanged heightfield's ground.
                                        const FIntVector World =
                                            Local + FIntVector(Position.X * 16, Position.Y * 16, 0);
                                        const uint16 State = StateAt(World);
                                        if (Local.X < 0 || Local.X >= 16 || Local.Y < 0 || Local.Y >= 16 ||
                                            !IsTree(State))
                                        {
                                            Fail(FString::Printf(TEXT("quad has no owned tree cell at %s face %d"),
                                                *World.ToString(), Face));
                                            continue;
                                        }
                                        ++Counts.FindOrAdd(FarTreeTestFaceKey(World, Face));
                                        const uint16 Above = StateAt(World + Steps[5]);
                                        const uint16 Draw =
                                            Face == 5 && Above == VoxelTestBlocks::Snow3 ? Above : State;
                                        const FVoxelStateInfo& Info = Table.Get(Draw);
                                        const int32 WantedLayer =
                                            int32(IsLeaf(State) ? Options.CanopyLayer : EVoxelLayer::Opaque);
                                        bool bSame = LayerIndex == WantedLayer;
                                        for (int32 Corner = 0; Corner < 4; ++Corner)
                                        {
                                            const int32 Vertex = Quad * 4 + Corner;
                                            bSame = bSame && uint32(Layer.Codes[Vertex].X) == Info.Faces[Face] &&
                                                uint32(Layer.TintFaces[Vertex].X) == Info.Tint &&
                                                int32(Layer.TintFaces[Vertex].Y) == Face &&
                                                Layer.Colors[Vertex].R == 255;
                                        }
                                        if (!bSame)
                                            Fail(FString::Printf(
                                                TEXT("state/axis/tint/layer mismatch at %s face %d state %d"),
                                                *World.ToString(), Face, Draw));
                                    }
                            }
                        }
                    }
                int32 Required = 0;
                for (const TPair<FIntVector, uint16>& Pair : Voxels)
                {
                    if (!IsTree(Pair.Value)) continue;
                    for (int32 Face = 0; Face < 6; ++Face)
                    {
                        const uint16 Next = StateAt(Pair.Key + Steps[Face]);
                        const bool bCovered = Next == VoxelTestBlocks::Stone ||
                            (IsTree(Next) && (IsLeaf(Pair.Value) || !IsLeaf(Next) || bOpaqueLeaves));
                        const int32 LocalX = ((Pair.Key.X % 16) + 16) % 16;
                        const int32 LocalY = ((Pair.Key.Y % 16) + 16) % 16;
                        const bool bBoundary = Face == 0 ? LocalX == 0
                            : Face == 1                  ? LocalX == 15
                            : Face == 2                  ? LocalY == 0
                            : Face == 3                  ? LocalY == 15
                                                         : false;
                        const int32 Count = Counts.FindRef(FarTreeTestFaceKey(Pair.Key, Face));
                        Duplicate += Count > 1;
                        if (!bCovered)
                        {
                            ++Required;
                            if (!Count)
                            {
                                ++Missing;
                                if (FirstWrong.IsEmpty())
                                    FirstWrong = FString::Printf(TEXT("missing exterior face at %s face %d state %d"),
                                        *Pair.Key.ToString(), Face, Pair.Value);
                            }
                        }
                        else if (Count && bBoundary && IsTree(Next))
                            Internal += Count;
                        else if (Count)
                            Fail(FString::Printf(
                                TEXT("unexpected internal face at %s face %d"), *Pair.Key.ToString(), Face));
                    }
                }
                Test.TestEqual(Scenario + TEXT(": missing required exterior faces"), Missing, 0);
                Test.TestEqual(Scenario + TEXT(": duplicated unit faces"), Duplicate, 0);
                Test.TestEqual(Scenario + TEXT(": wrong faces, state, axes, tint or layer"), Wrong, 0);
                const int32 Look = bOpaqueLeaves ? 1 : 0;
                if (NeighborMode == 0)
                {
                    NewNeighborHash[Look] = MeshHash;
                    LegacyCenterHash[Look] = LegacyHash;
                }
                else if (NeighborMode < 3)
                {
                    Test.TestEqual(Scenario + TEXT(": candidate output unchanged across mixed switch handoff"),
                        MeshHash, NewNeighborHash[Look]);
                    Test.TestEqual(Scenario + TEXT(": legacy output unchanged across mixed switch handoff"), LegacyHash,
                        LegacyCenterHash[Look]);
                }
                if (!FirstWrong.IsEmpty()) Test.AddInfo(Scenario + TEXT(": ") + FirstWrong);
                Test.AddInfo(
                    FString::Printf(TEXT("%s: %d required unit faces, %d conservative internal border caps, %d quads"),
                        *Scenario, Required, Internal, Quads));
            }

        // An actually displayed near neighbour has a roof above the trees and an asymmetric stone screen under
        // it. Its heightfield alone hides the entire border; its actual cells must reveal wood and every leaf tier
        // through the openings. Expected coverage reads those raw cells, independently of SummarizeBorder's mask.
        for (int32 Side = 0; Side < 4; ++Side)
        {
            const bool bAlongY = Side < 2;
            const int32 FacingEdge = (Side & 1) ? 0 : 15;
            const FIntPoint NearPosition = Center + FIntPoint(Steps[Side].X, Steps[Side].Y);
            const TSharedRef<FVoxelColumn> NearColumn = VoxelTestBlocks::MakeColumn(Table, NearPosition,
                [=](int32 X, int32 Y, int32 Z) -> uint16
                {
                    const int32 Edge = bAlongY ? X : Y, Along = bAlongY ? Y : X;
                    const bool bScreen = Edge == FacingEdge && (Along + Side) % 3 == 0 && Z >= 70 && Z <= 72;
                    return Z <= 64 || Z == 86 || bScreen ? uint16(VoxelTestBlocks::Stone) : uint16(0);
                });
            TUniquePtr<FVoxelFarSummary> NearSummary = MakeUnique<FVoxelFarSummary>();
            VoxelFarMesher::Summarize(Classes, Table, *NearColumn, *NearSummary);
            TUniquePtr<FVoxelFarBorder> NearBorder = MakeUnique<FVoxelFarBorder>();
            VoxelFarMesher::SummarizeBorder(Table, *NearColumn, Side, *NearBorder);
            const FVoxelFarSummary* Neighbors[4] = {};
            Neighbors[Side] = NearSummary.Get();
            for (const bool bOpaqueLeaves : {false, true})
                for (const bool bActualBorder : {false, true})
                {
                    VoxelFarMesher::FOptions Options;
                    Options.bTrees = true;
                    Options.CanopyLayer = bOpaqueLeaves ? EVoxelLayer::Opaque : EVoxelLayer::Cutout;
                    const FVoxelFarBorder* Borders[4] = {};
                    Borders[Side] = bActualBorder ? NearBorder.Get() : nullptr;
                    FVoxelMeshData Mesh;
                    VoxelFarMesher::Mesh(Table, Summaries[4], Neighbors, Options, Mesh, Borders);
                    TMap<int64, int32> Counts;
                    int32 WrongState = 0;
                    for (int32 LayerIndex = 0; LayerIndex < int32(EVoxelLayer::Count); ++LayerIndex)
                    {
                        const FVoxelMeshLayer& Layer = Mesh.Layers[LayerIndex];
                        FarMesherTestRasterize(Layer,
                            [&](int32 Quad, int32 Face, int32 X, int32 Y, int32 Z)
                            {
                                if (Face != Side || (bAlongY ? X : Y) != ((Side & 1) ? 15 : 0) || Z < 65) return;
                                ++Counts.FindOrAdd(FarMesherTestUnitKey(Face, X, Y, Z));
                                const uint16 State = StateAt(Origin + FIntVector(X, Y, Z));
                                const int32 WantedLayer =
                                    int32(IsLeaf(State) ? Options.CanopyLayer : EVoxelLayer::Opaque);
                                WrongState += !IsTree(State) || LayerIndex != WantedLayer ||
                                    uint32(Layer.Codes[Quad * 4].X) != Table.Get(State).Faces[Face];
                            });
                    }
                    int32 Missing = 0, Extra = 0, Duplicate = 0, WoodOpen = 0, LeavesOpen = 0;
                    for (int32 Along = 0; Along < 16; ++Along)
                        for (int32 Z = 65; Z < 86; ++Z)
                        {
                            const int32 OwnEdge = (Side & 1) ? 15 : 0;
                            const int32 X = bAlongY ? OwnEdge : Along, Y = bAlongY ? Along : OwnEdge;
                            const uint16 State = StateAt(Origin + FIntVector(X, Y, Z));
                            const uint16 Next =
                                NearColumn->GetState(bAlongY ? FacingEdge : Along, bAlongY ? Along : FacingEdge, Z);
                            const bool bExpected = IsTree(State) && bActualBorder && Next == 0;
                            const int32 Count = Counts.FindRef(FarMesherTestUnitKey(Side, X, Y, Z));
                            Missing += bExpected && !Count;
                            Extra += !bExpected && Count;
                            Duplicate += Count > 1;
                            WoodOpen += bExpected && !IsLeaf(State);
                            LeavesOpen += bExpected && IsLeaf(State);
                        }
                    const FString What = FString::Printf(TEXT("trees near roof side %d %s %s border"), Side,
                        bOpaqueLeaves ? TEXT("opaque") : TEXT("cutout"), bActualBorder ? TEXT("actual") : TEXT("no"));
                    Test.TestEqual(What + TEXT(": missing exposed wood/crown faces"), Missing, 0);
                    Test.TestEqual(What + TEXT(": unexpected covered faces"), Extra, 0);
                    Test.TestEqual(What + TEXT(": duplicate faces"), Duplicate, 0);
                    Test.TestEqual(What + TEXT(": wrong owner/state/layer"), WrongState, 0);
                    if (bActualBorder)
                        Test.TestTrue(
                            What + TEXT(": fixture exposes both wood and crown"), WoodOpen > 0 && LeavesOpen > 0);
                }
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
    // 5. A neighbour drawn in full across the East edge (review A3). Center: stone through 80. Neighbour, Y < 8: stone
    // through 60 and a roof at 90 (air 61-89); Y >= 8: stone through 100 but a cave at 30-32. Its ground (90, 100) is
    // above the center's, so the heightfield rule draws no East wall; with the border the wall shows where the
    // neighbour's cells are open below 80: 20 unit faces per cell under the roof, 3 by the cave. A neighbour solid
    // under its ground (stone through 70) changes nothing.
    void FarMesherTestBorders(FAutomationTestBase& Test, const FVoxelBlockTable& Table)
    {
        constexpr int32 East = 1;
        const uint16 Stone = Table.FindState(TEXT("stone"));
        const FVoxelFarClasses Classes(Table);
        const TSharedRef<FVoxelColumn> Center = VoxelTestBlocks::MakeColumn(
            Table, FIntPoint(0, 0), [Stone](int32, int32, int32 Z) -> uint16 { return Z <= 80 ? Stone : 0; });
        const TSharedRef<FVoxelColumn> Roofed = VoxelTestBlocks::MakeColumn(Table, FIntPoint(1, 0),
            [Stone](int32, int32 Y, int32 Z) -> uint16
            {
                if (Y < 8) return Z <= 60 || Z == 90 ? Stone : 0;
                return Z <= 100 && (Z < 30 || Z > 32) ? Stone : 0;
            });
        const TSharedRef<FVoxelColumn> Solid = VoxelTestBlocks::MakeColumn(
            Table, FIntPoint(1, 0), [Stone](int32, int32, int32 Z) -> uint16 { return Z <= 70 ? Stone : 0; });
        TUniquePtr<FVoxelFarSummary> CenterSummary = MakeUnique<FVoxelFarSummary>();
        TUniquePtr<FVoxelFarSummary> NeighborSummary = MakeUnique<FVoxelFarSummary>();
        VoxelFarMesher::Summarize(Classes, Table, *Center, *CenterSummary);
        TUniquePtr<FVoxelFarBorder> Border = MakeUnique<FVoxelFarBorder>();

        // The East wall's unit faces on the center's edge cells, and the mesh's quads.
        auto MeshEast = [&](const FVoxelColumn& Neighbor, bool bBorder, int32& OutQuads) -> TSet<int64>
        {
            VoxelFarMesher::Summarize(Classes, Table, Neighbor, *NeighborSummary);
            VoxelFarMesher::SummarizeBorder(Table, Neighbor, East, *Border);
            const FVoxelFarSummary* Neighbors[4] = {nullptr, NeighborSummary.Get(), nullptr, nullptr};
            const FVoxelFarBorder* Borders[4] = {nullptr, bBorder ? Border.Get() : nullptr, nullptr, nullptr};
            FVoxelMeshData Mesh;
            VoxelFarMesher::Mesh(Table, *CenterSummary, Neighbors, VoxelFarMesher::FOptions(), Mesh, Borders);
            OutQuads = Mesh.GetQuadCount();
            TSet<int64> Faces;
            FarMesherTestRasterize(Mesh.Layers[int32(EVoxelLayer::Opaque)],
                [&Faces](int32, int32 Side, int32 X, int32 Y, int32 Z)
                {
                    if (Side == East && X == 15) Faces.Add(FarMesherTestUnitKey(Side, X, Y, Z));
                });
            return Faces;
        };

        int32 Quads = 0, PlainQuads = 0;
        const TSet<int64> Plain = MeshEast(*Roofed, false, PlainQuads);
        Test.TestTrue(
            TEXT("borders: the border is open under the roof"), Border->IsOpen(0, 61) && Border->IsOpen(7, 89));
        Test.TestTrue(TEXT("borders: the border is closed in the stone and the roof"),
            !Border->IsOpen(0, 60) && !Border->IsOpen(7, 90) && !Border->IsOpen(8, 29));
        Test.TestTrue(TEXT("borders: the border is open in the cave"), Border->IsOpen(8, 30) && Border->IsOpen(15, 32));
        Test.TestEqual(TEXT("borders: heightfield rule, no East wall under the roof"), Plain.Num(), 0);
        const TSet<int64> Bordered = MeshEast(*Roofed, true, Quads);
        TSet<int64> Expected;
        for (int32 Y = 0; Y < 16; ++Y)
            for (int32 Z = Y < 8 ? 61 : 30; Z <= (Y < 8 ? 80 : 32); ++Z)
                Expected.Add(FarMesherTestUnitKey(East, 15, Y, Z));
        Test.TestEqual(TEXT("borders: East unit faces with the border (8 x 20 + 8 x 3)"), Bordered.Num(), 184);
        Test.TestTrue(TEXT("borders: exactly the open cells under the center's ground"),
            Bordered.Num() == Expected.Num() && Bordered.Includes(Expected));

        int32 SolidQuads = 0, SolidBorderedQuads = 0;
        const TSet<int64> SolidPlain = MeshEast(*Solid, false, SolidQuads);
        const TSet<int64> SolidBordered = MeshEast(*Solid, true, SolidBorderedQuads);
        Test.TestEqual(TEXT("borders: solid neighbour, the wall 71-80 either way"), SolidPlain.Num(), 160);
        Test.TestTrue(TEXT("borders: solid neighbour, the border changes nothing"),
            SolidBordered.Num() == SolidPlain.Num() && SolidBordered.Includes(SolidPlain) &&
                SolidBorderedQuads == SolidQuads);
        Test.AddInfo(FString::Printf(TEXT("borders: %d quads without the border, %d with it"), PlainQuads, Quads));
    }

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
        TArray<FVoxelFarSummary> TreePlain;
        Plain.SetNum(Area.Columns.Num());
        TreePlain.SetNum(Area.Columns.Num());
        for (int32 Index = 0; Index < Area.Columns.Num(); ++Index)
        {
            VoxelFarMesher::Summarize(Classes, Table, *Area.Columns[Index], Plain[Index]);
            VoxelFarMesher::Summarize(
                Classes, Table, *Area.Columns[Index], TreePlain[Index], nullptr, nullptr, FVoxelMeshOptions(), true);
        }

        TUniquePtr<FVoxelFarSummary> Tinted = MakeUnique<FVoxelFarSummary>();
        TUniquePtr<FVoxelFarSummary> TreeTinted = MakeUnique<FVoxelFarSummary>();
        TUniquePtr<FVoxelFarSummary> RepeatTinted = MakeUnique<FVoxelFarSummary>();
        const FVoxelMeshOptions MeshOptions;
        VoxelFarMesher::FOptions TreeOptions;
        TreeOptions.bTrees = true;
        // Logical upload payload from BuildStreams: 4 * 48 vertex bytes + 2 * 14 triangle bytes per quad;
        // each nonempty layer pads its triangle count to a multiple of four. Excludes allocator/driver overhead.
        auto StreamBytes = [](const FVoxelMeshData& Mesh)
        {
            int64 Bytes = 0;
            for (const FVoxelMeshLayer& Layer : Mesh.Layers)
            {
                const int64 Quads = Layer.GetQuadCount();
                Bytes += 220 * Quads + 28 * (Quads & 1);
            }
            return Bytes;
        };
        uint64 Hash = 0;
        uint64 TreeHash = 0, RepeatHash = 0;
        int32 MaxQuads = 0, Meshed = 0;
        int32 MinQuads = MAX_int32, TreeMinQuads = MAX_int32, TreeMaxQuads = 0, TreeRunCount = 0;
        int64 AllQuads = 0;
        int64 TreeAllQuads = 0, AllBytes = 0, TreeAllBytes = 0, MaxBytes = 0, TreeMaxBytes = 0;
        double AllMs = 0, MaxMs = 0;
        double TreeAllMs = 0, TreeMaxMs = 0;
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
                MinQuads = FMath::Min(MinQuads, Quads);
                MaxQuads = FMath::Max(MaxQuads, Quads);
                const int64 Bytes = StreamBytes(FarMesh);
                AllBytes += Bytes;
                MaxBytes = FMath::Max(MaxBytes, Bytes);
                AllMs += Ms;
                MaxMs = FMath::Max(MaxMs, Ms);
                const FVoxelFarSummary* TreeNeighbors[4];
                for (int32 Side = 0; Side < 4; ++Side)
                    TreeNeighbors[Side] =
                        &TreePlain[IndexOf(CX + FarMesherTestStepX[Side], CY + FarMesherTestStepY[Side])];
                FVoxelMeshData TreeMesh;
                const double TreeBegan = FPlatformTime::Seconds();
                VoxelFarMesher::Summarize(Classes, Table, *Area.Columns[IndexOf(CX, CY)], *TreeTinted,
                    Terrain->GetTints(), &Hood, MeshOptions, true);
                VoxelFarMesher::Mesh(Table, *TreeTinted, TreeNeighbors, TreeOptions, TreeMesh);
                const double TreeMs = (FPlatformTime::Seconds() - TreeBegan) * 1000.0;
                FarMesherTestHash(TreeMesh, Meshed, TreeHash);
                TreeRunCount += TreeTinted->TreeRuns.Num();
                const int32 TreeQuads = TreeMesh.GetQuadCount();
                TreeAllQuads += TreeQuads;
                TreeMinQuads = FMath::Min(TreeMinQuads, TreeQuads);
                TreeMaxQuads = FMath::Max(TreeMaxQuads, TreeQuads);
                const int64 TreeBytes = StreamBytes(TreeMesh);
                TreeAllBytes += TreeBytes;
                TreeMaxBytes = FMath::Max(TreeMaxBytes, TreeBytes);
                TreeAllMs += TreeMs;
                TreeMaxMs = FMath::Max(TreeMaxMs, TreeMs);
                FVoxelMeshData RepeatMesh;
                VoxelFarMesher::Summarize(Classes, Table, *Area.Columns[IndexOf(CX, CY)], *RepeatTinted,
                    Terrain->GetTints(), &Hood, MeshOptions, true);
                VoxelFarMesher::Mesh(Table, *RepeatTinted, TreeNeighbors, TreeOptions, RepeatMesh);
                FarMesherTestHash(RepeatMesh, Meshed, RepeatHash);
                ++Meshed;
            }
        Test.AddInfo(FString::Printf(TEXT("digest: %d columns, quads per column mean %.1f, max %d; summary + mesh per "
                                          "column mean %.3f ms, max %.3f ms"),
            Meshed, double(AllQuads) / Meshed, MaxQuads, AllMs / Meshed, MaxMs));
        Test.TestTrue(TEXT("trees natural seed263: fixture contains tree runs"), TreeRunCount > 0);
        Test.TestEqual(
            TEXT("trees natural seed263: repeated summary and mesh are byte deterministic"), TreeHash, RepeatHash);
        Test.AddInfo(
            FString::Printf(TEXT("trees natural seed263: %d final columns, legacy quads min/mean/max %d/%.1f/%d, "
                                 "trees %d/%.1f/%d; logical payload bytes mean/max %lld/%lld -> %lld/%lld; "
                                 "tree digest 0x%016llx (informational, not pinned)"),
                Meshed, MinQuads, double(AllQuads) / Meshed, MaxQuads, TreeMinQuads, double(TreeAllQuads) / Meshed,
                TreeMaxQuads, AllBytes / Meshed, MaxBytes, TreeAllBytes / Meshed, TreeMaxBytes, TreeHash));
        Test.AddInfo(FString::Printf(TEXT("trees natural seed263: indicative summary+mesh ms mean/max %.3f/%.3f -> "
                                          "%.3f/%.3f; loaded automation timing is not quiet throughput evidence"),
            AllMs / Meshed, MaxMs, TreeAllMs / Meshed, TreeMaxMs));
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
        AddError(TEXT("DA_Blocks is missing: run ImportBlocks.py (only the watertight case ran)"));
        return false;
    }
    FarMesherTestSummaryRules(*this, *BlockTable);
    FarMesherTestSameLayout(*this, *BlockTable);
    FarMesherTestBorders(*this, *BlockTable);
    FarMesherTestDigest(*this, *BlockTable);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVoxelFarTreesTest, "Unnamed.Voxel.FarTrees",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FVoxelFarTreesTest::RunTest(const FString& Parameters)
{
    FarMesherTestTrees(*this);
    return true;
}

#endif
