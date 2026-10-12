#if WITH_DEV_AUTOMATION_TESTS
#include "Biome/VoxelBiomeSource.h"
#include "Biome/VoxelBiomeTable.h"
#include "Foundation/FoundationBlockMeshes.h"
#include "Foundation/FoundationInteractionView.h"
#include "Decoration/VoxelDecorator.h"
#include "Decoration/VoxelFeatureGraph.h"
#include "Foundation/FoundationGrid.h"
#include "Generation/VoxelArea.h"
#include "Hash/CityHash.h"
#include "Meshing/VoxelFluidSurface.h"
#include "Meshing/VoxelMesher.h"
#include "Misc/AutomationTest.h"
#include "Random/VoxelRandom.h"
#include "Terrain/VoxelDensityTerrain.h"
#include "Tests/VoxelTestBlocks.h"
#include "Voxel/VoxelBlocks.h"
#include "Voxel/VoxelTerrainFactory.h"
#include "Voxel/VoxelWorldgenReader.h"

// Fluid surfaces (water and sky W3): the shared corner rule (VoxelFluidSurface) and the mesher's sloped tops, sides and
// flow textures, on VoxelTestBlocks plus the levels, falling water, lava and a water-holding model.
namespace
{
    using namespace VoxelTestBlocks;

    enum : uint16
    {
        // Water levels 1-7 at FluidMeshWaterFirst + level - 1, then falling water (level 8).
        FluidMeshWaterFirst = VoxelTestBlocks::Count,
        FluidMeshFalling = FluidMeshWaterFirst + 7,
        FluidMeshLavaSource,
        // Level 2: amount 6.
        FluidMeshLavaFlowing,
        // A model state holding water, with no quads of its own.
        FluidMeshKelp
    };
    constexpr uint32 FluidMeshWaterFlowCode = 1000, FluidMeshLavaFlowCode = 1001;
    // Section 4 holds Z 0..15: fluid scenes stand at Z 1 on a stone floor at Z 0.
    constexpr int32 FluidMeshSection = (0 - VoxelConstants::MinZ) / 16;
    constexpr int32 FluidMeshZ = 1;

    uint16 FluidMeshWater(int32 Level)
    {
        return Level == 0 ? uint16(WaterSource) : uint16(FluidMeshWaterFirst + Level - 1);
    }

    FVoxelBlockTable MakeFluidMeshTable()
    {
        FVoxelBlockTable Table = VoxelTestBlocks::Make();
        FVoxelStateInfo Water = Table.Get(WaterSource);
        for (int32 Level = 1; Level <= 8; ++Level)
        {
            Water.FluidLevel = uint8(Level);
            Water.FluidAmount = uint8(Level >= 8 ? 8 : 8 - Level);
            Table.SetState(FluidMeshWater(Level), Water);
        }
        FVoxelStateInfo Lava = Table.Get(WaterSource);
        Lava.Fluid = EVoxelFluid::Lava;
        Lava.Layer = EVoxelLayer::Opaque;
        Lava.Block = 9;
        Lava.LightEmission = 15;
        for (int32 Face = 0; Face < VoxelFaces::Count; ++Face)
            Lava.Faces[Face] = uint32(500 + Face);
        Table.SetState(FluidMeshLavaSource, Lava);
        Lava.FluidLevel = 2;
        Lava.FluidAmount = 6;
        Table.SetState(FluidMeshLavaFlowing, Lava);
        FVoxelStateInfo Kelp;
        Kelp.Shape = EVoxelShape::Model;
        Kelp.Layer = EVoxelLayer::Model;
        Kelp.Block = 10;
        Kelp.HeldFluid = WaterSource;
        Kelp.Fluid = EVoxelFluid::Water;
        Table.SetState(FluidMeshKelp, Kelp);
        Table.SetFlowCode(EVoxelFluid::Water, FluidMeshWaterFlowCode);
        Table.SetFlowCode(EVoxelFluid::Lava, FluidMeshLavaFlowCode);
        return Table;
    }

    // 3 x 3 columns around the origin filled by a function of world cells; column (0, 0)'s fluid section meshed.
    struct FFluidMeshWorld
    {
        FVoxelBlockTable Table;
        TMap<FIntPoint, TSharedRef<FVoxelColumn>> Columns;

        explicit FFluidMeshWorld(TFunction<uint16(int32, int32, int32)> Fill)
            : FFluidMeshWorld(MakeFluidMeshTable(), MoveTemp(Fill))
        {
        }

        FFluidMeshWorld(const FVoxelBlockTable& InTable, TFunction<uint16(int32, int32, int32)> Fill) : Table(InTable)
        {
            for (int32 CY = -1; CY <= 1; ++CY)
                for (int32 CX = -1; CX <= 1; ++CX)
                    Columns.Add({CX, CY},
                        MakeColumn(Table, {CX, CY},
                            [&](int32 X, int32 Y, int32 Z) { return Fill(CX * 16 + X, CY * 16 + Y, Z); }));
        }

        TUniquePtr<FVoxelPaddedSection> Gather() const
        {
            FVoxelNeighborhood Neighborhood;
            for (int32 DY = -1; DY <= 1; ++DY)
                for (int32 DX = -1; DX <= 1; ++DX)
                    Neighborhood.Set(DX, DY, &Columns.FindChecked({DX, DY}).Get());
            TUniquePtr<FVoxelPaddedSection> Padded = MakeUnique<FVoxelPaddedSection>();
            VoxelMesher::GatherPadded(Neighborhood, FluidMeshSection, *Padded);
            return Padded;
        }

        FVoxelMeshData Mesh(int32 SlopedTops, bool bGreedy = true, bool bOpenCorners = false) const
        {
            FVoxelMeshOptions Options;
            Options.SlopedTops = SlopedTops;
            Options.bGreedy = bGreedy;
            Options.bOpenCorners = bOpenCorners;
            FVoxelMeshData Out;
            VoxelMesher::MeshSection(Table, *Gather(), FluidMeshSection, Options, Out);
            return Out;
        }
    };

    // Fluid at Z 1 where Place gives one (0 for none), on a 16 x 16 stone floor at Z 0.
    TFunction<uint16(int32, int32, int32)> FluidMeshScene(TFunction<uint16(int32, int32)> Place)
    {
        return [Place](int32 X, int32 Y, int32 Z) -> uint16
        {
            if (X < 0 || Y < 0 || X > 15 || Y > 15) return Air;
            if (Z == 0) return Stone;
            return Z == FluidMeshZ ? Place(X, Y) : uint16(Air);
        };
    }

    struct FFluidMeshQuad
    {
        int32 Face = 0;
        uint32 Code = 0;
        FVector3f Positions[4];
        FVector2f UVs[4];
        // The cell the quad belongs to: the one its middle lies in, pulled back along a top face's normal.
        FIntVector Cell = FIntVector::ZeroValue;
    };

    TArray<FFluidMeshQuad> FluidMeshQuads(const FVoxelMeshLayer& Layer)
    {
        TArray<FFluidMeshQuad> Quads;
        for (int32 Quad = 0; Quad < Layer.GetQuadCount(); ++Quad)
        {
            FFluidMeshQuad& Out = Quads.AddDefaulted_GetRef();
            Out.Face = FMath::RoundToInt32(Layer.TintFaces[Quad * 4].Y);
            Out.Code = uint32(FMath::RoundToInt32(Layer.Codes[Quad * 4].X));
            FVector3f Mid = FVector3f::ZeroVector;
            for (int32 Corner = 0; Corner < 4; ++Corner)
            {
                Out.Positions[Corner] = Layer.Positions[Quad * 4 + Corner];
                Out.UVs[Corner] = Layer.BlockUVs[Quad * 4 + Corner];
                Mid += Out.Positions[Corner] / 4;
            }
            const FVector3f Normal(FoundationGrid::FaceNormal(EFoundationFace(Out.Face)));
            const FVector3f Inside = Mid - Normal * .01f;
            // A top's middle can lie at its cell's top (a full corner) or below; its cell is the one under it.
            Out.Cell = FIntVector(FMath::FloorToInt32(Inside.X), FMath::FloorToInt32(Inside.Y),
                Out.Face == int32(EVoxelFace::Up) ? FMath::FloorToInt32(Mid.Z - .001f) : FMath::FloorToInt32(Inside.Z));
        }
        return Quads;
    }

    const FFluidMeshQuad* FindFluidMeshQuad(
        TConstArrayView<FFluidMeshQuad> Quads, const FIntVector& Cell, EVoxelFace Face)
    {
        return Quads.FindByPredicate(
            [&](const FFluidMeshQuad& Quad) { return Quad.Cell == Cell && Quad.Face == int32(Face); });
    }

    // A top's height (above its cell's floor) at the cell corner (DX, DY) in {0, 1}; -1 when no vertex is there.
    float FluidMeshTopAt(const FFluidMeshQuad& Top, int32 DX, int32 DY)
    {
        for (const FVector3f& P : Top.Positions)
            if (FMath::IsNearlyEqual(P.X, float(Top.Cell.X + DX)) && FMath::IsNearlyEqual(P.Y, float(Top.Cell.Y + DY)))
                return P.Z - float(Top.Cell.Z);
        return -1;
    }

    // The highest of a side quad's vertices above its cell's floor.
    float FluidMeshSideTop(const FFluidMeshQuad& Side)
    {
        float Top = 0;
        for (const FVector3f& P : Side.Positions)
            Top = FMath::Max(Top, P.Z - float(Side.Cell.Z));
        return Top;
    }

    // A packed 3 x 3 ring from cells listed row by row (-Y first), each a state info or none.
    void FluidMeshRing(const FVoxelBlockTable& Table, const uint16 States[9], const bool Full[9], uint8 Out[9])
    {
        for (int32 Index = 0; Index < 9; ++Index)
            Out[Index] = VoxelFluidSurface::Pack(Table.Get(States[Index]), Full ? Full[Index] : false);
    }
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVoxelFluidSurfaceTest, "Unnamed.Voxel.Fluid.Surface",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FVoxelFluidSurfaceTest::RunTest(const FString& Parameters)
{
    using namespace VoxelFluidSurface;
    const FVoxelBlockTable Table = MakeFluidMeshTable();
    constexpr float Tolerance = 1e-5f;

    TestEqual(
        TEXT("Pack: a source is amount 8, water"), int32(Pack(Table.Get(WaterSource), false)), 8 | 1 << FluidShift);
    TestEqual(TEXT("Pack: falling water under more is amount 8, falling, full"),
        int32(Pack(Table.Get(FluidMeshFalling), true)), 8 | FallingBit | 1 << FluidShift | FullBit);
    TestEqual(TEXT("Pack: level 3 is amount 5"), int32(Pack(Table.Get(FluidMeshWater(3)), false)), 5 | 1 << FluidShift);
    TestEqual(TEXT("Pack: lava"), Pack(Table.Get(FluidMeshLavaSource), false) & FluidMask, 2 << FluidShift);
    TestEqual(TEXT("Pack: stone holds none"), int32(Pack(Table.Get(Stone), false)), 0);

    auto Surface =
        [&Table](std::initializer_list<uint16> States, int32 Mode, FSurface& Out, std::initializer_list<bool> Full = {})
    {
        uint16 Cells[9];
        bool Fulls[9] = {};
        int32 Index = 0;
        for (const uint16 State : States)
            Cells[Index++] = State;
        Index = 0;
        for (const bool bFull : Full)
            Fulls[Index++] = bFull;
        uint8 Ring[9];
        FluidMeshRing(Table, Cells, Fulls, Ring);
        return VoxelFluidSurface::Make(Ring, Mode, Out);
    };
    const uint16 S = WaterSource, L1 = FluidMeshWater(1), L2 = FluidMeshWater(2), L3 = FluidMeshWater(3),
                 L5 = FluidMeshWater(5), F = FluidMeshFalling, LS = FluidMeshLavaSource;
    FSurface Out;
    TestFalse(TEXT("Mode 0 never slopes"), Surface({0, 0, 0, 0, L3, L5, 0, 0, 0}, 0, Out));
    TestFalse(TEXT("A pool of sources stays flat (mode 1)"), Surface({S, S, S, S, S, S, S, S, S}, 1, Out));
    TestFalse(TEXT("...and under mode 2"), Surface({S, S, S, S, S, S, S, S, S}, 2, Out));
    TestFalse(TEXT("A cell under more fluid draws no top"),
        Surface({0, 0, 0, 0, L3, L5, 0, 0, 0}, 1, Out, {false, false, false, false, true}));

    // A source beside one drawn full (more water above it): equal levels, so flat under 1; under 2 its east edge
    // rises to the full cell.
    TestFalse(TEXT("A source beside a full source stays flat (mode 1)"),
        Surface({0, 0, 0, 0, S, S, 0, 0, 0}, 1, Out, {false, false, false, false, false, true}));
    if (TestTrue(TEXT("...and slopes under mode 2"),
            Surface({0, 0, 0, 0, S, S, 0, 0, 0}, 2, Out, {false, false, false, false, false, true})))
    {
        TestEqual(TEXT("...its east corners full"), Out.Corners[1] + Out.Corners[2], 2.f, Tolerance);
        TestEqual(TEXT("...its west corners at 8/9"), Out.Corners[0] + Out.Corners[3], 16 / 9.f, Tolerance);
        TestTrue(TEXT("...running downhill west"), Out.Flow.X < 0 && FMath::IsNearlyZero(Out.Flow.Y, Tolerance));
    }

    // A 5 -> 3 step: level 3 (5/9) beside level 5 (3/9) to the east. The shared edge is their mean, the outer edge
    // the cell's own height.
    if (TestTrue(TEXT("A 5 -> 3 step slopes"), Surface({0, 0, 0, 0, L3, L5, 0, 0, 0}, 1, Out)))
    {
        TestEqual(TEXT("...west corners at 5/9"), Out.Corners[0], 5 / 9.f, Tolerance);
        TestEqual(TEXT("...(-X +Y) too"), Out.Corners[3], 5 / 9.f, Tolerance);
        TestEqual(TEXT("...east corners at 4/9"), Out.Corners[1], 4 / 9.f, Tolerance);
        TestEqual(TEXT("...(+X +Y) too"), Out.Corners[2], 4 / 9.f, Tolerance);
        TestTrue(TEXT("...flowing east by 1/9 a block"), Out.Flow.Equals(FVector2f(1 / 9.f, 0), Tolerance));
    }
    // A source keeps its plateau: level 1 between a source (west) and level 2 (east).
    if (TestTrue(TEXT("Level 1 between a source and level 2 slopes"), Surface({0, 0, 0, S, L1, L2, 0, 0, 0}, 1, Out)))
    {
        TestEqual(TEXT("...its corners on the source at 8/9"), Out.Corners[0] + Out.Corners[3], 16 / 9.f, Tolerance);
        TestEqual(TEXT("...its corners on level 2 at 6.5/9"), Out.Corners[1] + Out.Corners[2], 13 / 9.f, Tolerance);
    }
    // Open corners (W3b): air beside a flowing cell counts as height 0 in a corner's mean; a corner at a source or a
    // full cell is unchanged, so no source slopes for it (nor does any generated fluid).
    auto Open = [&Table](std::initializer_list<uint16> States, int32 Mode, FSurface& Out)
    {
        uint8 Ring[9];
        int32 Index = 0;
        for (const uint16 State : States)
            Ring[Index++] = State ? Pack(Table.Get(State), false) : OpenCell;
        return VoxelFluidSurface::Make(Ring, Mode, Out, true);
    };
    if (TestTrue(TEXT("Open corners: level 1 east of a source, air elsewhere, slopes (mode 1)"),
            Open({0, 0, 0, S, L1, 0, 0, 0, 0}, 1, Out)))
    {
        TestEqual(TEXT("...its corners on the source at 8/9"), Out.Corners[0] + Out.Corners[3], 16 / 9.f, Tolerance);
        TestEqual(TEXT("...its corners on air at 7/36"), Out.Corners[1] + Out.Corners[2], 14 / 36.f, Tolerance);
    }
    if (TestTrue(TEXT("...without open corners"), Surface({0, 0, 0, S, L1, 0, 0, 0, 0}, 1, Out)))
        TestEqual(
            TEXT("...its corners on air stay at its own 7/9"), Out.Corners[1] + Out.Corners[2], 14 / 9.f, Tolerance);
    TestFalse(TEXT("Open corners: a source beside air stays flat (mode 2)"), Open({0, 0, 0, 0, S, 0, 0, 0, 0}, 2, Out));
    TestFalse(TEXT("...and a pool of sources at its rim (mode 1)"), Open({0, 0, 0, S, S, 0, S, S, 0}, 1, Out));

    // Falling water drawn full (more above it) to the south lifts the corners it touches to the full cell.
    if (TestTrue(TEXT("Level 1 beside a full falling cell slopes"),
            Surface({0, F, 0, 0, L1, 0, 0, 0, 0}, 1, Out, {false, true})))
    {
        TestEqual(TEXT("...its corners on the falling cell full"), Out.Corners[0] + Out.Corners[1], 2.f, Tolerance);
        TestEqual(TEXT("...the others at its own 7/9"), Out.Corners[2] + Out.Corners[3], 14 / 9.f, Tolerance);
        TestTrue(TEXT("...running downhill +Y"), Out.Flow.Y > 0);
    }
    TestFalse(TEXT("Another fluid doesn't count (lava beside water)"), Surface({0, 0, 0, 0, L1, LS, 0, 0, 0}, 1, Out));

    // Neighbours share their corners: on a random field of water levels, falling and full cells and gaps, every corner
    // reads the same from each of the cells around it that draws a top.
    constexpr uint64 Salt = VoxelRandom::Salt("fluid-surface-corners");
    constexpr int32 Size = 12;
    for (const int32 Mode : {1, 2})
    {
        int32 Disagree = 0, Corners = 0, Sloped = 0;
        for (int32 Trial = 0; Trial < 40; ++Trial)
        {
            uint8 Field[Size][Size];
            for (int32 Y = 0; Y < Size; ++Y)
                for (int32 X = 0; X < Size; ++X)
                {
                    const uint64 Hash = VoxelRandom::Hash64(Trial, Salt, X, Y, Mode);
                    const int32 Roll = VoxelRandom::ToIndex(Hash, 12);
                    const uint16 State = Roll < 2 ? uint16(Air) : Roll < 10 ? FluidMeshWater(Roll - 2) : uint16(F);
                    Field[Y][X] = Pack(Table.Get(State), VoxelRandom::ToIndex(Hash << 32, 6) == 0);
                }
            auto HeightAt = [&](int32 X, int32 Y, int32 Corner, float& Height)
            {
                if (!Field[Y][X] || (Field[Y][X] & FullBit)) return false;
                uint8 Ring[9];
                for (int32 DY = -1; DY <= 1; ++DY)
                    for (int32 DX = -1; DX <= 1; ++DX)
                        Ring[(DY + 1) * 3 + DX + 1] = Field[Y + DY][X + DX];
                FSurface Cell;
                const bool bSloped = VoxelFluidSurface::Make(Ring, Mode, Cell);
                Sloped += bSloped;
                Height = bSloped ? Cell.Corners[Corner] : (Field[Y][X] & AmountMask) / 9.f;
                return true;
            };
            // The point between cells (X - 1, Y - 1) .. (X, Y): corner 2, 3, 1, 0 of the four.
            for (int32 Y = 2; Y < Size - 1; ++Y)
                for (int32 X = 2; X < Size - 1; ++X)
                {
                    float Heights[4];
                    int32 Count = 0;
                    const int32 CellX[4] = {X - 1, X, X - 1, X}, CellY[4] = {Y - 1, Y - 1, Y, Y};
                    const int32 Corner[4] = {2, 3, 1, 0};
                    for (int32 Index = 0; Index < 4; ++Index)
                        Count += HeightAt(CellX[Index], CellY[Index], Corner[Index], Heights[Count]);
                    // Different fluids never meet here (water only); a top's corner can only disagree with another's.
                    for (int32 Index = 1; Index < Count; ++Index)
                        Disagree += !FMath::IsNearlyEqual(Heights[Index], Heights[0], Tolerance);
                    Corners += Count > 1;
                }
        }
        AddInfo(FString::Printf(TEXT("Mode %d: %d shared corners, %d sloped tops"), Mode, Corners, Sloped));
        TestEqual(FString::Printf(TEXT("Mode %d: every shared corner agrees"), Mode), Disagree, 0);
        TestTrue(FString::Printf(TEXT("Mode %d: the field slopes"), Mode), Sloped > 100);
    }

    // The flow UVs: v runs downhill, the texture turns without mirroring, and the corners stay inside it.
    FVector2f UVs[4];
    GetFlowUVs(FVector2f(0, 1), UVs);
    TestTrue(TEXT("Downhill +Y: the still texture's axes, (-,-) at the texture's 1/4"),
        UVs[0].Equals(FVector2f(.5f, .5f), Tolerance));
    TestTrue(TEXT("...(+,+) at 3/4"), UVs[2].Equals(FVector2f(1.5f, 1.5f), Tolerance));
    GetFlowUVs(FVector2f(3, 0), UVs);
    TestTrue(TEXT("Downhill +X: v grows toward +X"), UVs[1].Y > UVs[0].Y && UVs[2].Y > UVs[3].Y);
    TestEqual(TEXT("...u grows toward -Y (a quarter turn, not a mirror)"), UVs[0].X, 1.5f, Tolerance);
    GetFlowUVs(FVector2f(1, 1), UVs);
    bool bInside = true;
    for (const FVector2f& UV : UVs)
        bInside &= UV.X > 0 && UV.Y > 0 && UV.X < 2 && UV.Y < 2;
    TestTrue(TEXT("Downhill diagonally: every corner inside the texture"), bInside);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVoxelFluidShapesTest, "Unnamed.Voxel.Mesher.FluidShapes",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FVoxelFluidShapesTest::RunTest(const FString& Parameters)
{
    constexpr float Tolerance = 1e-5f;
    const int32 Water = int32(EVoxelLayer::Water);

    // A 5 -> 3 step: level 3 at (3, 3), level 5 east of it.
    {
        const FFluidMeshWorld World(FluidMeshScene([](int32 X, int32 Y) -> uint16
            { return Y == 3 && X == 3 ? FluidMeshWater(3)
                  : Y == 3 && X == 4  ? FluidMeshWater(5)
                                      : 0; }));
        const FIntVector A(3, 3, FluidMeshZ), B(4, 3, FluidMeshZ);
        const TArray<FFluidMeshQuad> Sloped = FluidMeshQuads(World.Mesh(1).Layers[Water]);
        TestEqual(TEXT("Step, mode 1: two tops and six outward sides"), Sloped.Num(), 8);
        const FFluidMeshQuad* TopA = FindFluidMeshQuad(Sloped, A, EVoxelFace::Up);
        const FFluidMeshQuad* TopB = FindFluidMeshQuad(Sloped, B, EVoxelFace::Up);
        if (TestTrue(TEXT("...both tops drawn"), TopA && TopB))
        {
            TestEqual(TEXT("...A's west edge at 5/9"), FluidMeshTopAt(*TopA, 0, 0), 5 / 9.f, Tolerance);
            TestEqual(TEXT("...A meets B at 4/9"), FluidMeshTopAt(*TopA, 1, 1), 4 / 9.f, Tolerance);
            TestEqual(TEXT("...B's west edge at 4/9 too"), FluidMeshTopAt(*TopB, 0, 1), 4 / 9.f, Tolerance);
            TestEqual(TEXT("...B's east edge at 3/9"), FluidMeshTopAt(*TopB, 1, 0), 3 / 9.f, Tolerance);
            TestEqual(TEXT("...the tops draw the flow texture"), int32(TopA->Code), int32(FluidMeshWaterFlowCode));
            // Downhill east: the texture's v grows toward +X at both edges.
            float WestV = 0, EastV = 0;
            for (int32 Corner = 0; Corner < 4; ++Corner)
                (TopA->Positions[Corner].X > A.X + .5f ? EastV : WestV) += TopA->UVs[Corner].Y / 2;
            TestTrue(FString::Printf(TEXT("...turned east: v grows with X (%.3f -> %.3f)"), WestV, EastV),
                EastV > WestV + .5f);
        }
        const FFluidMeshQuad* SideA = FindFluidMeshQuad(Sloped, A, EVoxelFace::North);
        if (TestTrue(TEXT("...A's -Y side drawn"), SideA != nullptr))
        {
            TestEqual(TEXT("...a trapezoid up to A's 5/9"), FluidMeshSideTop(*SideA), 5 / 9.f, Tolerance);
            TestEqual(TEXT("...on the water's side texture"), int32(SideA->Code),
                int32(World.Table.Get(WaterSource).Faces[int32(EVoxelFace::North)]));
        }
        TestTrue(TEXT("...no side between A and B"),
            !FindFluidMeshQuad(Sloped, A, EVoxelFace::East) && !FindFluidMeshQuad(Sloped, B, EVoxelFace::West));

        const TArray<FFluidMeshQuad> Stepped = FluidMeshQuads(World.Mesh(0).Layers[Water]);
        TestEqual(TEXT("Step, mode 0: the same eight faces"), Stepped.Num(), 8);
        const FFluidMeshQuad* FlatA = FindFluidMeshQuad(Stepped, A, EVoxelFace::Up);
        if (TestTrue(TEXT("...A's top drawn"), FlatA != nullptr))
        {
            TestEqual(TEXT("...flat at 5/9"), FluidMeshTopAt(*FlatA, 1, 1), 5 / 9.f, Tolerance);
            TestEqual(TEXT("...on the still texture"), int32(FlatA->Code),
                int32(World.Table.Get(WaterSource).Faces[int32(EVoxelFace::Up)]));
        }
    }

    // Equal levels still merge: a 4 x 4 pool of level 2 is one top and 16 sides, as a pool of sources.
    {
        const FFluidMeshWorld World(FluidMeshScene(
            [](int32 X, int32 Y) -> uint16 { return X >= 2 && X <= 5 && Y >= 2 && Y <= 5 ? FluidMeshWater(2) : 0; }));
        for (const int32 Mode : {0, 1, 2})
            TestEqual(FString::Printf(TEXT("A level-2 pool, mode %d: one merged top and 16 sides"), Mode),
                World.Mesh(Mode).Layers[Water].GetQuadCount(), 17);
    }

    // A spread: a source at (8, 8), level d at Manhattan distance d up to 7. Tops meet: at every point, every top there
    // has one height. Each top runs away from the source.
    {
        const FFluidMeshWorld World(FluidMeshScene(
            [](int32 X, int32 Y) -> uint16
            {
                const int32 Distance = FMath::Abs(X - 8) + FMath::Abs(Y - 8);
                return Distance <= 7 ? FluidMeshWater(Distance) : 0;
            }));
        for (const int32 Mode : {1, 2})
        {
            const TArray<FFluidMeshQuad> Quads = FluidMeshQuads(World.Mesh(Mode, false).Layers[Water]);
            TMap<FIntPoint, FVector2f> Range;
            int32 Tops = 0, Downhill = 0;
            for (const FFluidMeshQuad& Quad : Quads)
            {
                if (Quad.Face != int32(EVoxelFace::Up)) continue;
                ++Tops;
                for (const FVector3f& P : Quad.Positions)
                {
                    FVector2f& Seen = Range.FindOrAdd(
                        FIntPoint(FMath::RoundToInt32(P.X), FMath::RoundToInt32(P.Y)), FVector2f(P.Z, P.Z));
                    Seen = FVector2f(FMath::Min(Seen.X, P.Z), FMath::Max(Seen.Y, P.Z));
                }
                // Away from the source: the top's far corner from (8.5, 8.5) is its lowest.
                const FVector2f Away = FVector2f(Quad.Cell.X + .5f - 8.5f, Quad.Cell.Y + .5f - 8.5f);
                if (Away.IsNearlyZero()) continue;
                float Far = 0, Near = 0;
                for (const FVector3f& P : Quad.Positions)
                {
                    const float Along = (FVector2f(P.X - 8.5f, P.Y - 8.5f) | Away);
                    if (Along > (Away | Away)) Far = P.Z;
                    if (Along < (Away | Away)) Near = FMath::Max(Near, P.Z);
                }
                Downhill += Far <= Near + Tolerance;
            }
            float Worst = 0;
            for (const TPair<FIntPoint, FVector2f>& Pair : Range)
                Worst = FMath::Max(Worst, Pair.Value.Y - Pair.Value.X);
            TestEqual(FString::Printf(TEXT("Spread, mode %d: a top per cell"), Mode), Tops, 113);
            TestEqual(
                FString::Printf(TEXT("...tops meet at every point (largest step %.4f)"), Worst), Worst, 0.f, Tolerance);
            TestEqual(TEXT("...every top runs down away from the source"), Downhill, 112);
        }
    }

    // A waterfall: falling water at (8, 8) from Z 1 to 3 over the floor, level 1 around its foot. The foot's ring rises
    // to the full column; the column's sides are whole; its top cell (nothing above) is flat at 8/9.
    {
        const FFluidMeshWorld World(
            [](int32 X, int32 Y, int32 Z) -> uint16
            {
                if (X < 0 || Y < 0 || X > 15 || Y > 15) return Air;
                if (Z == 0) return Stone;
                if (X == 8 && Y == 8 && Z >= 1 && Z <= 3) return FluidMeshFalling;
                return Z == 1 && FMath::Abs(X - 8) + FMath::Abs(Y - 8) == 1 ? FluidMeshWater(1) : uint16(Air);
            });
        const TArray<FFluidMeshQuad> Quads = FluidMeshQuads(World.Mesh(1).Layers[Water]);
        const FFluidMeshQuad* Ring = FindFluidMeshQuad(Quads, FIntVector(9, 8, 1), EVoxelFace::Up);
        if (TestTrue(TEXT("Waterfall: the east ring cell's top drawn"), Ring != nullptr))
        {
            TestEqual(TEXT("...full where it meets the column"), FluidMeshTopAt(*Ring, 0, 0), 1.f, Tolerance);
            TestEqual(TEXT("...(both corners)"), FluidMeshTopAt(*Ring, 0, 1), 1.f, Tolerance);
            TestEqual(TEXT("...its own 7/9 outside"), FluidMeshTopAt(*Ring, 1, 0), 7 / 9.f, Tolerance);
        }
        const FFluidMeshQuad* Middle = FindFluidMeshQuad(Quads, FIntVector(8, 8, 2), EVoxelFace::East);
        if (TestTrue(TEXT("...the column's middle side drawn"), Middle != nullptr))
            TestEqual(TEXT("...whole"), FluidMeshSideTop(*Middle), 1.f, Tolerance);
        const FFluidMeshQuad* Head = FindFluidMeshQuad(Quads, FIntVector(8, 8, 3), EVoxelFace::Up);
        if (TestTrue(TEXT("...the column's top drawn"), Head != nullptr))
            TestEqual(TEXT("...flat at 8/9"), FluidMeshTopAt(*Head, 1, 1), 8 / 9.f, Tolerance);
        TestTrue(TEXT("...no top under more water"), !FindFluidMeshQuad(Quads, FIntVector(8, 8, 1), EVoxelFace::Up));
    }

    // Water held by a model counts as a source: level 1 beside kelp keeps the kelp's 8/9 on that edge.
    {
        const FFluidMeshWorld World(FluidMeshScene([](int32 X, int32 Y) -> uint16
            { return Y == 3 && X == 3 ? FluidMeshKelp
                  : Y == 3 && X == 4  ? FluidMeshWater(1)
                                      : 0; }));
        const TArray<FFluidMeshQuad> Quads = FluidMeshQuads(World.Mesh(1).Layers[Water]);
        const FFluidMeshQuad* Top = FindFluidMeshQuad(Quads, FIntVector(4, 3, 1), EVoxelFace::Up);
        if (TestTrue(TEXT("Kelp: the level-1 top drawn"), Top != nullptr))
        {
            TestEqual(TEXT("...at 8/9 on the kelp"), FluidMeshTopAt(*Top, 0, 0), 8 / 9.f, Tolerance);
            TestEqual(TEXT("...its own 7/9 beyond"), FluidMeshTopAt(*Top, 1, 1), 7 / 9.f, Tolerance);
        }
        const FFluidMeshQuad* Held = FindFluidMeshQuad(Quads, FIntVector(3, 3, 1), EVoxelFace::Up);
        if (TestTrue(TEXT("...the kelp's water top drawn"), Held != nullptr))
            TestEqual(
                TEXT("...flat at 8/9 (its corners are a source's)"), FluidMeshTopAt(*Held, 1, 1), 8 / 9.f, Tolerance);
    }

    // Lava: a source at (3, 3) beside flowing lava east of it. Its sides take the flow texture where it flows (all of
    // them under mode 2); a source's keep the still texture under 0 and 1.
    {
        const FFluidMeshWorld World(FluidMeshScene([](int32 X, int32 Y) -> uint16
            { return Y == 3 && X == 3 ? FluidMeshLavaSource
                  : Y == 3 && X == 4  ? FluidMeshLavaFlowing
                                      : 0; }));
        for (const int32 Mode : {0, 1, 2})
        {
            const TArray<FFluidMeshQuad> Quads = FluidMeshQuads(World.Mesh(Mode).Layers[int32(EVoxelLayer::Opaque)]);
            const FFluidMeshQuad* SourceSide = FindFluidMeshQuad(Quads, FIntVector(3, 3, 1), EVoxelFace::West);
            const FFluidMeshQuad* FlowingSide = FindFluidMeshQuad(Quads, FIntVector(4, 3, 1), EVoxelFace::East);
            if (!TestTrue(
                    FString::Printf(TEXT("Lava, mode %d: both outer sides drawn"), Mode), SourceSide && FlowingSide))
                continue;
            TestEqual(FString::Printf(TEXT("...the source's side (mode %d)"), Mode), int32(SourceSide->Code),
                int32(Mode == 2 ? FluidMeshLavaFlowCode : 500u + uint32(EVoxelFace::West)));
            TestEqual(FString::Printf(TEXT("...the flowing side (mode %d)"), Mode), int32(FlowingSide->Code),
                int32(Mode == 0 ? 500u + uint32(EVoxelFace::East) : FluidMeshLavaFlowCode));
        }
    }
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVoxelFluidGeneratedTest, "Unnamed.Voxel.Mesher.FluidGenerated",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FVoxelFluidGeneratedTest::RunTest(const FString& Parameters)
{
    // Generated fluid is all sources (and water held by models), so SlopedTops 1 meshes a generated area byte for byte
    // as 0 does; Mesher.Digest pins 1 (the default) to its goldens. Mode 2 is the vanilla look and may differ.
    const TSharedRef<const FVoxelBlockTable> Table = VoxelBlocks::GetDefault();
    if (!Table->FindState(TEXT("stone")))
    {
        AddError(TEXT("DA_Blocks is missing: run ImportBlocks.py"));
        return false;
    }
    FString Error;
    FVoxelTerrainPreset TerrainPreset;
    FVoxelBiomeTable Biomes;
    FVoxelSurfaceInputs Surface;
    FVoxelFeatureGraph Graph;
    if (!TestTrue(FString::Printf(TEXT("overworld inputs read (%s)"), *Error),
            VoxelTerrainFactory::ReadInputs(
                VoxelWorldgen::GetReferenceRoot(), TEXT("overworld"), TerrainPreset, Biomes, Surface, Error)) ||
        !TestTrue(FString::Printf(TEXT("the decoration data reads (%s)"), *Error),
            VoxelWorldgen::ReadFeatures(VoxelWorldgen::GetReferenceRoot(), Graph, Error)))
        return false;
    const TSharedRef<FVoxelDensityTerrain> Terrain =
        MakeShared<FVoxelDensityTerrain>(TerrainPreset, 263, *Table, FVoxelDensityTerrainSettings(), &Biomes, &Surface);
    FVoxelDecorator Decorator;
    if (!TestTrue(FString::Printf(TEXT("the decorator compiles (%s)"), *Error),
            Decorator.Initialize(Graph, *Table, Terrain->GetBiomeSource()->GetTable(), Terrain->GetSeed(),
                FVoxelDecorationOptions(), &Error)))
        return false;
    uint64 Digests[3] = {};
    int64 FluidQuads[3] = {};
    for (int32 Mode = 0; Mode < 3; ++Mode)
    {
        FVoxelMeshOptions Options;
        Options.SlopedTops = Mode;
        FVoxelArea Area;
        FVoxelArea::Build(*Table, *Terrain, FIntPoint::ZeroValue, 2, Options, false, Area, &Decorator);
        for (const FVoxelArea::FSectionMesh& Section : Area.Meshes)
            for (int32 Index = 0; Index < int32(EVoxelLayer::Count); ++Index)
            {
                const FVoxelMeshLayer& Layer = Section.Mesh.Layers[Index];
                if (Layer.IsEmpty()) continue;
                auto Add = [&Digests, Mode](const void* Data, SIZE_T Size)
                { Digests[Mode] = CityHash64WithSeed(static_cast<const char*>(Data), uint32(Size), Digests[Mode]); };
                const int32 Place[3] = {Index, Section.Column, Section.Section};
                Add(Place, sizeof(Place));
                Add(Layer.Positions.GetData(), Layer.Positions.Num() * sizeof(FVector3f));
                Add(Layer.BlockUVs.GetData(), Layer.BlockUVs.Num() * sizeof(FVector2f));
                Add(Layer.Codes.GetData(), Layer.Codes.Num() * sizeof(FVector2f));
                Add(Layer.TintFaces.GetData(), Layer.TintFaces.Num() * sizeof(FVector2f));
                Add(Layer.Colors.GetData(), Layer.Colors.Num() * sizeof(FColor));
                Add(Layer.Indices.GetData(), Layer.Indices.Num() * sizeof(uint32));
                if (Index == int32(EVoxelLayer::Water)) FluidQuads[Mode] += Layer.GetQuadCount();
            }
        AddInfo(FString::Printf(
            TEXT("SlopedTops %d: digest 0x%016llx, %lld water quads"), Mode, Digests[Mode], FluidQuads[Mode]));
    }
    TestTrue(TEXT("The area holds water"), FluidQuads[0] > 0);
    TestTrue(TEXT("SlopedTops 1 meshes generated fluid as 0 does, byte for byte"), Digests[1] == Digests[0]);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVoxelFluidPlaygroundTest, "Unnamed.Voxel.Mesher.FluidPlayground",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FVoxelFluidPlaygroundTest::RunTest(const FString& Parameters)
{
    // The Playground draws a sloped fluid cell (water and sky W3b: FoundationBlockMeshes::MakeFluidSurface and
    // GetFluidFaces, UFoundationInteractionView::MakeFluidSurfaceData) as the voxel mesher draws it, on the project's
    // blocks: the same cells slope, and their tops and outward sides have the same corners, UVs and codes.
    const TSharedRef<const FFoundationBlockRegistry> Registry = FFoundationBlockRegistry::GetDefault();
    const TSharedRef<const FVoxelBlockTable> Table = VoxelBlocks::GetDefault();
    const FFoundationBlockType* WaterType = Registry->FindBlock(TEXT("water"));
    const FFoundationBlockType* LavaType = Registry->FindBlock(TEXT("lava"));
    const FFoundationBlockType* StoneType = Registry->FindBlock(TEXT("stone"));
    const FFoundationBlockType* SeagrassType = Registry->FindBlock(TEXT("seagrass"));
    if (!WaterType || !LavaType || !StoneType || !SeagrassType)
    {
        AddError(TEXT("DA_Blocks is missing water, lava, stone or seagrass: run ImportBlocks.py"));
        return false;
    }
    // A fluid's state at a level (its only property).
    auto Level = [&Registry](const FFoundationBlockType* Type, int32 Value) -> uint16
    {
        const uint16 State = uint16(Type->MinStateId + Value);
        return Registry->GetFluid({State}).Level == Value ? State : uint16(0);
    };
    for (const int32 Value : {0, 1, 7, 8, 15})
        if (!TestTrue(FString::Printf(TEXT("water and lava level %d are their first state + %d"), Value, Value),
                Level(WaterType, Value) != 0 && Level(LavaType, Value) != 0))
            return false;
    const uint16 Stone = StoneType->GetDefaultState().Id, Seagrass = SeagrassType->GetDefaultState().Id;
    const int32 WaterLayer = int32(EVoxelLayer::Water), OpaqueLayer = int32(EVoxelLayer::Opaque);
    constexpr float Tolerance = 1e-5f;

    // Scenes at Z 1 on a stone floor; Above, when set, fills the cells over (8, 8) at Z 2 and 3.
    auto Scene = [Stone](TFunction<uint16(int32, int32)> Place, uint16 Above = 0)
    {
        return TFunction<uint16(int32, int32, int32)>(
            [Place, Above, Stone](int32 X, int32 Y, int32 Z) -> uint16
            {
                if (X < 0 || Y < 0 || X > 15 || Y > 15) return 0;
                if (Z == 0) return Stone;
                if (Z == FluidMeshZ) return Place(X, Y);
                return Above && X == 8 && Y == 8 && Z <= FluidMeshZ + 2 ? Above : uint16(0);
            });
    };
    auto Taxicab = [](int32 X, int32 Y) { return FMath::Abs(X - 8) + FMath::Abs(Y - 8); };
    struct FCase
    {
        const TCHAR* Name;
        TFunction<uint16(int32, int32, int32)> Fill;
    };
    const FCase Cases[] = {
        {TEXT("a 113-cell spread from a source"),
            Scene([&](int32 X, int32 Y) -> uint16
                { return Taxicab(X, Y) <= 7 ? Level(WaterType, Taxicab(X, Y)) : uint16(0); })},
        {TEXT("a waterfall's foot: falling water under more, levels 1-6 around"),
            Scene(
                [&](int32 X, int32 Y) -> uint16
                {
                    return Taxicab(X, Y) == 0 ? Level(WaterType, 8)
                        : Taxicab(X, Y) <= 6  ? Level(WaterType, Taxicab(X, Y))
                                              : uint16(0);
                },
                Level(WaterType, 8))},
        {TEXT("seagrass holding a source, levels 1-3 around"),
            Scene(
                [&](int32 X, int32 Y) -> uint16
                {
                    return Taxicab(X, Y) == 0 ? Seagrass
                        : Taxicab(X, Y) <= 3  ? Level(WaterType, Taxicab(X, Y))
                                              : uint16(0);
                })},
        {TEXT("lava: a source, levels 2, 4 and 6 east of it, level 2 south"),
            Scene(
                [&](int32 X, int32 Y) -> uint16
                {
                    return Y == 8 && X >= 3 && X <= 6 ? Level(LavaType, 2 * (X - 3))
                        : Y == 9 && X == 3            ? Level(LavaType, 2)
                                                      : uint16(0);
                })},
    };
    for (const FCase& Case : Cases)
    {
        const FFluidMeshWorld World(*Table, Case.Fill);
        auto GetState = [&Case](const FIntVector& At) { return FFoundationBlockState{Case.Fill(At.X, At.Y, At.Z)}; };
        for (const int32 Variant : {1, 2, 3})
        {
            // Modes 1 and 2, and mode 1 with open corners.
            const int32 Mode = Variant == 3 ? 1 : Variant;
            const bool bOpen = Variant == 3;
            const FVoxelMeshData Mesh = World.Mesh(Mode, false, bOpen);
            TArray<FFluidMeshQuad> Quads = FluidMeshQuads(Mesh.Layers[WaterLayer]);
            Quads.Append(FluidMeshQuads(Mesh.Layers[OpaqueLayer]));
            int32 Sloped = 0, Flat = 0, Faces = 0, Mismatches = 0;
            FString FirstMismatch;
            auto Mismatch = [&Mismatches, &FirstMismatch](const FString& What)
            {
                if (!Mismatches++) FirstMismatch = What;
            };
            for (int32 Y = 0; Y < 16; ++Y)
                for (int32 X = 0; X < 16; ++X)
                {
                    const FIntVector Cell(X, Y, FluidMeshZ);
                    const FFoundationBlockState State = GetState(Cell);
                    if (!FoundationBlockMeshes::HoldsFluid(*Registry, State)) continue;
                    const FFluidMeshQuad* Top = FindFluidMeshQuad(Quads, Cell, EVoxelFace::Up);
                    VoxelFluidSurface::FSurface Surface;
                    if (!FoundationBlockMeshes::MakeFluidSurface(*Registry, GetState, Cell, Mode, bOpen, Surface))
                    {
                        ++Flat;
                        for (int32 Corner = 0; Top && Corner < 4; ++Corner)
                            if (!FMath::IsNearlyEqual(Top->Positions[Corner].Z, Top->Positions[0].Z, Tolerance))
                                Mismatch(FString::Printf(
                                    TEXT("%s: the mesher tilts a top the Playground draws flat"), *Cell.ToString()));
                        continue;
                    }
                    ++Sloped;
                    // A model's water is drawn as the water source, as the view draws it.
                    const FFoundationBlockState Drawn = Registry->GetShape(State) == EFoundationBlockShape::Model
                        ? WaterType->GetDefaultState()
                        : State;
                    float Data[UFoundationInteractionView::CustomDataFloats];
                    UFoundationInteractionView::MakeFluidSurfaceData(*Registry, Drawn, Surface, Mode, Data);
                    FoundationBlockMeshes::FFluidFace Playground[6];
                    FoundationBlockMeshes::GetFluidFaces(Surface, Playground);
                    for (int32 Face = 0; Face < VoxelFaces::Count; ++Face)
                    {
                        // The floor covers the bottom; sides between cells of one fluid are hidden.
                        if (Face == int32(EVoxelFace::Down)) continue;
                        const FFluidMeshQuad* Quad = FindFluidMeshQuad(Quads, Cell, EVoxelFace(Face));
                        if (!Quad)
                        {
                            if (Face == int32(EVoxelFace::Up))
                                Mismatch(FString::Printf(TEXT("%s: the mesher draws no top"), *Cell.ToString()));
                            continue;
                        }
                        ++Faces;
                        const FoundationBlockMeshes::FFluidFace& Ours = Playground[Face];
                        for (int32 Corner = 0; Corner < 4; ++Corner)
                        {
                            const FVector3f At = Ours.Positions[Corner] + FVector3f(Cell);
                            if (!At.Equals(Quad->Positions[Corner], Tolerance) ||
                                !Ours.UVs[Corner].Equals(Quad->UVs[Corner], Tolerance))
                                Mismatch(FString::Printf(TEXT("%s face %d corner %d: (%s, uv %s) against the "
                                                              "mesher's (%s, uv %s)"),
                                    *Cell.ToString(), Face, Corner, *At.ToString(), *Ours.UVs[Corner].ToString(),
                                    *Quad->Positions[Corner].ToString(), *Quad->UVs[Corner].ToString()));
                        }
                        const uint32 Code = uint32(FMath::RoundToInt32(Data[Face]));
                        if (Code != Quad->Code)
                            Mismatch(FString::Printf(TEXT("%s face %d: code %u against the mesher's %u"),
                                *Cell.ToString(), Face, Code, Quad->Code));
                    }
                }
            const TCHAR* Corners = bOpen ? TEXT(", open corners") : TEXT("");
            AddInfo(FString::Printf(TEXT("%s, mode %d%s: %d sloped cells (%d faces compared), %d flat"), Case.Name,
                Mode, Corners, Sloped, Faces, Flat));
            TestTrue(FString::Printf(TEXT("%s, mode %d%s: cells slope"), Case.Name, Mode, Corners), Sloped > 0);
            TestEqual(FString::Printf(TEXT("%s, mode %d%s: the Playground draws them as the mesher (first: %s)"),
                          Case.Name, Mode, Corners, *FirstMismatch),
                Mismatches, 0);
        }
    }
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVoxelFluidCostTest, "Unnamed.Voxel.Mesher.FluidCost",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FVoxelFluidCostTest::RunTest(const FString& Parameters)
{
    // The mesh cost of sloped (unmerged) tops per touched section (water and sky W3b), on the test blocks: a section
    // holding a 113-cell spread, and one whose 16 x 16 layer of water has a level in every cell that differs from its
    // neighbours' (every top slopes), each meshed FluidCostRuns times per mode. Logs the median; indicative only (the
    // machine's load moves it), so no time is asserted.
    constexpr int32 FluidCostRuns = 41;
    struct FCase
    {
        const TCHAR* Name;
        TFunction<uint16(int32, int32)> Place;
    };
    const FCase Cases[] = {
        {TEXT("a 113-cell spread"),
            [](int32 X, int32 Y) -> uint16
            {
                const int32 D = FMath::Abs(X - 8) + FMath::Abs(Y - 8);
                return D <= 7 ? FluidMeshWater(D) : uint16(0);
            }},
        {TEXT("256 cells of mixed levels"),
            [](int32 X, int32 Y) -> uint16 { return FluidMeshWater((X * 3 + Y * 5 + X * Y) % 8); }},
    };
    for (const FCase& Case : Cases)
    {
        const FFluidMeshWorld World(FluidMeshScene(Case.Place));
        const TUniquePtr<FVoxelPaddedSection> Padded = World.Gather();
        for (int32 Mode = 0; Mode < 3; ++Mode)
        {
            FVoxelMeshOptions Options;
            Options.SlopedTops = Mode;
            TArray<double> Ms;
            int32 Quads = 0;
            for (int32 Run = 0; Run < FluidCostRuns; ++Run)
            {
                FVoxelMeshData Out;
                const double Start = FPlatformTime::Seconds();
                VoxelMesher::MeshSection(World.Table, *Padded, FluidMeshSection, Options, Out);
                Ms.Add((FPlatformTime::Seconds() - Start) * 1000.);
                Quads = Out.Layers[int32(EVoxelLayer::Water)].GetQuadCount();
            }
            Ms.Sort();
            AddInfo(FString::Printf(TEXT("%s, SlopedTops %d: median %.3f ms per section (min %.3f, max %.3f, %d "
                                         "runs), %d water quads"),
                Case.Name, Mode, Ms[Ms.Num() / 2], Ms[0], Ms.Last(), FluidCostRuns, Quads));
            TestTrue(FString::Printf(TEXT("%s, SlopedTops %d: water meshed"), Case.Name, Mode), Quads > 0);
        }
    }
    return true;
}

#endif
