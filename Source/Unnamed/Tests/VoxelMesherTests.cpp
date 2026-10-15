#if WITH_DEV_AUTOMATION_TESTS
#include "Core/RealtimeMeshBuilder.h"
#include "Foundation/FoundationBlockMeshes.h"
#include "Foundation/FoundationBlockRegistry.h"
#include "Foundation/FoundationGrid.h"
#include "Foundation/FoundationProfile.h"
#include "MeshCardBuild.h"
#include "MeshCardRepresentation.h"
#include "Meshing/VoxelMesher.h"
#include "Misc/AutomationTest.h"
#include "Random/VoxelRandom.h"
#include "Tests/VoxelTestBlocks.h"
#include "Voxel/VoxelBlocks.h"
#include "Voxel/VoxelChunkComponent.h"

namespace
{
    using namespace VoxelTestBlocks;

    // Columns around the origin, filled by a function of world cells, meshed with their real neighbours.
    struct FMesherTestWorld
    {
        FVoxelBlockTable Table = VoxelTestBlocks::Make();
        TMap<FIntPoint, TSharedRef<FVoxelColumn>> Columns;

        template <typename FillType> FMesherTestWorld(int32 Radius, FillType&& Fill)
        {
            for (int32 CY = -Radius; CY <= Radius; ++CY)
                for (int32 CX = -Radius; CX <= Radius; ++CX)
                    Columns.Add({CX, CY},
                        MakeColumn(Table, {CX, CY},
                            [&](int32 X, int32 Y, int32 Z) { return Fill(CX * 16 + X, CY * 16 + Y, Z); }));
        }

        uint16 Get(const FIntVector& Cell) const
        {
            const FIntPoint Column(FoundationGrid::FloorDivide(Cell.X, 16), FoundationGrid::FloorDivide(Cell.Y, 16));
            const TSharedRef<FVoxelColumn>* Found = Columns.Find(Column);
            return Found ? (*Found)->GetState(Cell.X - Column.X * 16, Cell.Y - Column.Y * 16, Cell.Z) : 0;
        }

        TArray<FVoxelMeshData> MeshColumn(FIntPoint Position, const FVoxelMeshOptions& Options) const
        {
            FVoxelNeighborhood Neighborhood;
            for (int32 DY = -1; DY <= 1; ++DY)
                for (int32 DX = -1; DX <= 1; ++DX)
                {
                    const TSharedRef<FVoxelColumn>* Found = Columns.Find(Position + FIntPoint(DX, DY));
                    Neighborhood.Set(DX, DY, Found ? &Found->Get() : nullptr);
                }
            TArray<FVoxelMeshData> Meshes;
            Meshes.SetNum(VoxelConstants::SectionCount);
            TUniquePtr<FVoxelPaddedSection> Padded = MakeUnique<FVoxelPaddedSection>();
            for (int32 Section = 0; Section < VoxelConstants::SectionCount; ++Section)
            {
                VoxelMesher::GatherPadded(Neighborhood, Section, *Padded);
                VoxelMesher::MeshSection(Table, *Padded, Section, Options, Meshes[Section]);
            }
            return Meshes;
        }
    };

    int32 CountLayerQuads(TConstArrayView<FVoxelMeshData> Meshes, EVoxelLayer Layer)
    {
        int32 Count = 0;
        for (const FVoxelMeshData& Mesh : Meshes)
            Count += Mesh.Layers[int32(Layer)].GetQuadCount();
        return Count;
    }

    FIntVector FaceOffsetOf(int32 Face)
    {
        return FoundationGrid::FaceOffset(EFoundationFace(Face));
    }

    // One face of one cell in column (0, 0): ((z - MinZ) * 256 + y * 16 + x) * 6 + face.
    int32 MesherFaceKey(const FIntVector& Cell, int32 Face)
    {
        return ((Cell.Z - VoxelConstants::MinZ) * 256 + Cell.Y * 16 + Cell.X) * 6 + Face;
    }

    // Every face the mesher should draw in column (0, 0), with its corner levels at (-,-), (+,-), (+,+), (-,+), by
    // an independent cell-by-cell reading of the rules in VoxelMesher.h.
    TMap<int32, uint8> ExpectedMesherFaces(const FMesherTestWorld& World, bool bOcclusion)
    {
        TMap<int32, uint8> Faces;
        auto Casts = [&World](const FIntVector& Cell)
        { return World.Table.Get(World.Get(Cell)).bCastsOcclusion ? 1 : 0; };
        for (int32 Z = VoxelConstants::MinZ; Z <= VoxelConstants::MaxZ; ++Z)
            for (int32 Y = 0; Y < 16; ++Y)
                for (int32 X = 0; X < 16; ++X)
                {
                    const FIntVector Cell(X, Y, Z);
                    const FVoxelStateInfo& Info = World.Table.Get(World.Get(Cell));
                    if (Info.IsAir()) continue;
                    for (int32 Face = 0; Face < 6; ++Face)
                    {
                        const FVoxelStateInfo& Next = World.Table.Get(World.Get(Cell + FaceOffsetOf(Face)));
                        const bool bCovered = (Next.OpaqueFaces >> VoxelFaces::GetOpposite(Face)) & 1;
                        const bool bCulls = (Info.Faces[Face] & VoxelFaces::CullBit) != 0;
                        bool bHidden = false;
                        if (Info.Shape == EVoxelShape::Full)
                            bHidden = (bCulls && bCovered) ||
                                (Info.Layer == EVoxelLayer::Translucent && Next.Shape == EVoxelShape::Full &&
                                    Next.Block == Info.Block);
                        else if (Info.IsFluid())
                            bHidden = bCovered || Next.IsFluid();
                        else
                            bHidden = bCulls && bCovered;
                        if (bHidden) continue;
                        uint8 Corners = 0xFF;
                        if (bOcclusion && Info.Shape == EVoxelShape::Full)
                        {
                            const int32 Axis = Face >> 1;
                            FIntVector U(0), V(0);
                            U[(Axis + 1) % 3] = 1;
                            V[(Axis + 2) % 3] = 1;
                            const FIntVector Front = Cell + FaceOffsetOf(Face);
                            const int32 SignU[4] = {-1, 1, 1, -1}, SignV[4] = {-1, -1, 1, 1};
                            Corners = 0;
                            for (int32 Corner = 0; Corner < 4; ++Corner)
                            {
                                const int32 A = Casts(Front + U * SignU[Corner]), B = Casts(Front + V * SignV[Corner]);
                                const int32 D = Casts(Front + U * SignU[Corner] + V * SignV[Corner]);
                                Corners |= uint8((A && B ? 0 : 3 - A - B - D) << (2 * Corner));
                            }
                        }
                        Faces.Add(MesherFaceKey(Cell, Face), Corners);
                    }
                }
        return Faces;
    }

    // The faces the meshes actually cover, each merged quad split back into cells, with its corner levels read from
    // the vertex colors. A face covered twice counts as a duplicate.
    TMap<int32, uint8> DrawnMesherFaces(TConstArrayView<FVoxelMeshData> Meshes, int32& OutDuplicates)
    {
        TMap<int32, uint8> Faces;
        OutDuplicates = 0;
        for (const FVoxelMeshData& Mesh : Meshes)
            for (const FVoxelMeshLayer& Layer : Mesh.Layers)
                for (int32 Quad = 0; Quad < Layer.GetQuadCount(); ++Quad)
                {
                    const int32 Face = FMath::RoundToInt32(Layer.TintFaces[Quad * 4].Y);
                    const int32 Axis = Face >> 1, AxisU = (Axis + 1) % 3, AxisV = (Axis + 2) % 3;
                    FVector3f Min(TNumericLimits<float>::Max()), Max(-TNumericLimits<float>::Max());
                    for (int32 Corner = 0; Corner < 4; ++Corner)
                    {
                        Min = Min.ComponentMin(Layer.Positions[Quad * 4 + Corner]);
                        Max = Max.ComponentMax(Layer.Positions[Quad * 4 + Corner]);
                    }
                    uint8 Corners = 0;
                    for (int32 Corner = 0; Corner < 4; ++Corner)
                    {
                        const FVector3f& P = Layer.Positions[Quad * 4 + Corner];
                        const bool bHighU = P[AxisU] > Min[AxisU], bHighV = P[AxisV] > Min[AxisV];
                        const int32 Slot = bHighV ? (bHighU ? 2 : 3) : (bHighU ? 1 : 0);
                        Corners |= uint8((Layer.Colors[Quad * 4 + Corner].R / 85) << (2 * Slot));
                    }
                    const float Plane = Min[Axis];
                    const int32 CellAxis =
                        FMath::FloorToInt32(VoxelFaces::IsPositive(Face) ? Plane - 1e-3f : Plane + 1e-3f);
                    for (int32 V = FMath::FloorToInt32(Min[AxisV] + 1e-3f); V < FMath::CeilToInt32(Max[AxisV] - 1e-3f);
                        ++V)
                        for (int32 U = FMath::FloorToInt32(Min[AxisU] + 1e-3f);
                            U < FMath::CeilToInt32(Max[AxisU] - 1e-3f); ++U)
                        {
                            FIntVector Cell;
                            Cell[Axis] = CellAxis;
                            Cell[AxisU] = U;
                            Cell[AxisV] = V;
                            const int32 Key = MesherFaceKey(Cell, Face);
                            if (Faces.Contains(Key)) ++OutDuplicates;
                            Faces.Add(Key, Corners);
                        }
                }
        return Faces;
    }

    // A column's Lumen cards, built from its meshed sections as the voxel world builds them.
    FMeshCardsBuildData MesherCardsOf(TConstArrayView<FVoxelMeshData> Meshes)
    {
        TArray<const FVoxelMeshData*> Sections;
        for (const FVoxelMeshData& Mesh : Meshes)
            if (!Mesh.IsEmpty()) Sections.Add(&Mesh);
        FMeshCardsBuildData Cards;
        UVoxelChunkComponent::BuildCards(Sections, Cards);
        return Cards;
    }

    // The opaque and cutout faces that no card facing the same way contains (Lumen captures a face only through
    // such a card).
    int32 CountUncardedFaces(TConstArrayView<FVoxelMeshData> Meshes, const FMeshCardsBuildData& Cards)
    {
        constexpr float BlockCm = float(UFoundationProfile::BlockCm);
        int32 Uncarded = 0;
        for (const FVoxelMeshData& Mesh : Meshes)
            for (const EVoxelLayer LayerKind : {EVoxelLayer::Opaque, EVoxelLayer::Cutout})
            {
                const FVoxelMeshLayer& Layer = Mesh.Layers[int32(LayerKind)];
                for (int32 Quad = 0; Quad < Layer.GetQuadCount(); ++Quad)
                {
                    const int32 Face = FMath::RoundToInt32(Layer.TintFaces[Quad * 4].Y);
                    const bool bCarded = Cards.CardBuildData.ContainsByPredicate(
                        [&](const FLumenCardBuildData& Card)
                        {
                            if (Card.AxisAlignedDirectionIndex != Face) return false;
                            for (int32 Corner = 0; Corner < 4; ++Corner)
                            {
                                const FVector3f InCard =
                                    Card.OBB.TransformLocalToCard(Layer.Positions[Quad * 4 + Corner] * BlockCm);
                                if (FMath::Abs(InCard.X) > Card.OBB.Extent.X + 0.01f ||
                                    FMath::Abs(InCard.Y) > Card.OBB.Extent.Y + 0.01f ||
                                    FMath::Abs(InCard.Z) > Card.OBB.Extent.Z + 0.01f)
                                    return false;
                            }
                            return true;
                        });
                    Uncarded += !bCarded;
                }
            }
        return Uncarded;
    }
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVoxelMesherShapesTest, "Unnamed.Voxel.Mesher.Shapes",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FVoxelMesherShapesTest::RunTest(const FString& Parameters)
{
    FVoxelMeshOptions Greedy, PerFace;
    PerFace.bGreedy = false;
    // Small shapes in an otherwise empty world, all inside one section (Z 0..15 is section 4).
    auto Quads = [](auto&& Fill, const FVoxelMeshOptions& Options, EVoxelLayer Layer)
    { return CountLayerQuads(FMesherTestWorld(1, Fill).MeshColumn({0, 0}, Options), Layer); };
    auto Box = [](uint16 State, FIntVector Min, FIntVector Max)
    {
        return [=](int32 X, int32 Y, int32 Z) -> uint16
        { return X >= Min.X && Y >= Min.Y && Z >= Min.Z && X <= Max.X && Y <= Max.Y && Z <= Max.Z ? State : 0; };
    };
    TestEqual(TEXT("One block: 6 faces"), Quads(Box(Stone, {5, 5, 5}, {5, 5, 5}), Greedy, EVoxelLayer::Opaque), 6);
    TestEqual(TEXT("A 2x2x2 cube merges to 6 quads"),
        Quads(Box(Stone, {2, 2, 2}, {3, 3, 3}), Greedy, EVoxelLayer::Opaque), 6);
    TestEqual(
        TEXT("...or 24 faces unmerged"), Quads(Box(Stone, {2, 2, 2}, {3, 3, 3}), PerFace, EVoxelLayer::Opaque), 24);
    TestEqual(TEXT("A 4x1x1 bar: 6 quads"), Quads(Box(Stone, {1, 1, 1}, {4, 1, 1}), Greedy, EVoxelLayer::Opaque), 6);
    TestEqual(TEXT("Glass hides the faces it shares with glass"),
        Quads(Box(Glass, {1, 1, 1}, {2, 1, 1}), PerFace, EVoxelLayer::Translucent), 10);
    TestEqual(TEXT("Leaves keep the faces between leaves"),
        Quads(Box(Leaves, {1, 1, 1}, {2, 1, 1}), PerFace, EVoxelLayer::Cutout), 12);
    TestEqual(TEXT("A 16x16 floor crossing every column border is one quad per side in each column"),
        Quads(Box(Stone, {-8, -8, 0}, {7, 7, 0}), Greedy, EVoxelLayer::Opaque), 4);
    TestEqual(TEXT("A slab: 6 faces"), Quads(Box(SlabBottom, {3, 3, 3}, {3, 3, 3}), Greedy, EVoxelLayer::Opaque), 6);
    TestEqual(
        TEXT("Three snow layers: 6 faces"), Quads(Box(Snow3, {3, 3, 3}, {3, 3, 3}), Greedy, EVoxelLayer::Opaque), 6);

    // Partial faces sit where the shape ends.
    const TArray<FVoxelMeshData> Slab =
        FMesherTestWorld(1, Box(SlabBottom, {3, 3, 3}, {3, 3, 3})).MeshColumn({0, 0}, Greedy);
    const TArray<FVoxelMeshData> Water =
        FMesherTestWorld(1, Box(WaterSource, {3, 3, 3}, {3, 3, 3})).MeshColumn({0, 0}, Greedy);
    const TArray<FVoxelMeshData> Snow =
        FMesherTestWorld(1, Box(Snow3, {3, 3, 3}, {3, 3, 3})).MeshColumn({0, 0}, Greedy);
    auto TopOf = [](const FVoxelMeshLayer& Layer)
    {
        float Top = -1e9f;
        for (const FVector3f& P : Layer.Positions)
            Top = FMath::Max(Top, P.Z);
        return Top;
    };
    const int32 Section = (3 - VoxelConstants::MinZ) / 16;
    TestEqual(
        TEXT("A bottom slab's top is at half height"), TopOf(Slab[Section].Layers[int32(EVoxelLayer::Opaque)]), 3.5f);
    TestEqual(TEXT("A water source's surface is at 8/9"), TopOf(Water[Section].Layers[int32(EVoxelLayer::Water)]),
        3 + 8.f / 9, 1e-5f);
    TestEqual(
        TEXT("A lone water cell has 6 faces"), Water[Section].Layers[int32(EVoxelLayer::Water)].GetQuadCount(), 6);
    TestEqual(TEXT("Three snow layers reach 3/8 of the cell"), TopOf(Snow[Section].Layers[int32(EVoxelLayer::Opaque)]),
        3 + 3.f / 8, 1e-5f);

    // Snow on stone: the layer's opaque bottom hides the stone's top, and the stone hides the layer's bottom.
    const TArray<FVoxelMeshData> SnowOnStone = FMesherTestWorld(1,
        [](int32 X, int32 Y, int32 Z) -> uint16
        {
            if (X != 3 || Y != 3) return Air;
            return Z == 2 ? Stone : Z == 3 ? Snow3 : Air;
        }).MeshColumn({0, 0}, Greedy);
    int32 Overdrawn = 0;
    const TMap<int32, uint8> Drawn = DrawnMesherFaces(SnowOnStone, Overdrawn);
    auto FacesDrawnIn = [&Drawn](const FIntVector& Cell)
    {
        int32 Found = 0;
        for (int32 Face = 0; Face < VoxelFaces::Count; ++Face)
            Found += Drawn.Contains(MesherFaceKey(Cell, Face));
        return Found;
    };
    const FIntVector StoneCell(3, 3, 2), SnowCell(3, 3, 3);
    TestEqual(TEXT("Snow on stone: 10 faces in all"), CountLayerQuads(SnowOnStone, EVoxelLayer::Opaque), 10);
    TestEqual(TEXT("...5 of them the stone's"), FacesDrawnIn(StoneCell), 5);
    TestFalse(TEXT("...not its top"), Drawn.Contains(MesherFaceKey(StoneCell, int32(EVoxelFace::Up))));
    TestEqual(TEXT("...5 of them the snow's"), FacesDrawnIn(SnowCell), 5);
    TestFalse(TEXT("...not its bottom"), Drawn.Contains(MesherFaceKey(SnowCell, int32(EVoxelFace::Down))));

    // A pool on stone: the surface merges, the floor is hidden, sides only face air.
    const FMesherTestWorld Pool(1,
        [](int32 X, int32 Y, int32 Z) -> uint16
        {
            if (Z == 0 && X >= 0 && X <= 3 && Y >= 0 && Y <= 3) return Stone;
            return Z == 1 && X >= 0 && X <= 3 && Y >= 0 && Y <= 3 ? WaterSource : 0;
        });
    TestEqual(TEXT("A 4x4 pool: one merged surface quad and 16 side faces (partial faces stay per cell)"),
        CountLayerQuads(Pool.MeshColumn({0, 0}, Greedy), EVoxelLayer::Water), 17);

    // Occlusion: a block on a floor darkens the floor around it.
    const TArray<FVoxelMeshData> Corner = FMesherTestWorld(1,
        [](int32 X, int32 Y, int32 Z) -> uint16
        {
            return (Z == 0 && X >= 0 && X <= 4 && Y >= 0 && Y <= 4) || (Z == 1 && X == 2 && Y == 2) ? Stone : 0;
        }).MeshColumn({0, 0}, Greedy);
    int32 Darkened = 0;
    for (const FVoxelMeshData& Mesh : Corner)
        for (const FColor& Color : Mesh.Layers[int32(EVoxelLayer::Opaque)].Colors)
            Darkened += Color.R < 255;
    TestTrue(FString::Printf(TEXT("Corners next to the block are darkened (%d vertices)"), Darkened), Darkened > 0);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVoxelMesherBordersTest, "Unnamed.Voxel.Mesher.Borders",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FVoxelMesherBordersTest::RunTest(const FString& Parameters)
{
    // Random blocks of every kind over 3 x 3 columns and three sections (Z 0..47), on a stone floor: every face at
    // every section and column border depends on a neighbour.
    constexpr uint64 Salt = VoxelRandom::Salt("mesher-borders");
    const FMesherTestWorld World(1,
        [](int32 X, int32 Y, int32 Z) -> uint16
        {
            if (Z < -8 || Z > 47) return Air;
            if (Z < 0) return Stone;
            const int32 Roll = VoxelRandom::ToIndex(VoxelRandom::Hash64(7, Salt, X, Y, Z), 100);
            return Roll < 45 ? Air
                : Roll < 70  ? Stone
                : Roll < 78  ? Dirt
                : Roll < 84  ? Glass
                : Roll < 89  ? Leaves
                : Roll < 92  ? SlabBottom
                : Roll < 95  ? SlabTop
                : Roll < 98  ? WaterSource
                             : WaterFlowing;
        });
    const TMap<int32, uint8> Expected = ExpectedMesherFaces(World, true);
    for (const bool bGreedy : {true, false})
    {
        FVoxelMeshOptions Options;
        Options.bGreedy = bGreedy;
        const TArray<FVoxelMeshData> Meshes = World.MeshColumn({0, 0}, Options);
        int32 Duplicates = 0, Quads = 0;
        for (const FVoxelMeshData& Mesh : Meshes)
            Quads += Mesh.GetQuadCount();
        const TMap<int32, uint8> Drawn = DrawnMesherFaces(Meshes, Duplicates);
        int32 Missing = 0, Extra = 0, Shading = 0, FirstMissing = INDEX_NONE, FirstExtra = INDEX_NONE;
        for (const TPair<int32, uint8>& Face : Expected)
        {
            const uint8* Found = Drawn.Find(Face.Key);
            if (!Found)
            {
                ++Missing;
                if (FirstMissing == INDEX_NONE) FirstMissing = Face.Key;
            }
            else if (*Found != Face.Value)
                ++Shading;
        }
        for (const TPair<int32, uint8>& Face : Drawn)
            if (!Expected.Contains(Face.Key))
            {
                ++Extra;
                if (FirstExtra == INDEX_NONE) FirstExtra = Face.Key;
            }
        const TCHAR* Mode = bGreedy ? TEXT("greedy") : TEXT("per face");
        AddInfo(FString::Printf(TEXT("%s: %d faces expected, %d quads drawn"), Mode, Expected.Num(), Quads));
        TestEqual(FString::Printf(TEXT("%s: no face missing (first: key %d)"), Mode, FirstMissing), Missing, 0);
        TestEqual(FString::Printf(TEXT("%s: no hidden face drawn (first: key %d)"), Mode, FirstExtra), Extra, 0);
        TestEqual(FString::Printf(TEXT("%s: no face drawn twice"), Mode), Duplicates, 0);
        TestEqual(FString::Printf(TEXT("%s: every face has its own corner shading"), Mode), Shading, 0);
        if (!bGreedy) TestEqual(TEXT("Unmerged: one quad per face"), Quads, Expected.Num());
    }
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVoxelMesherCardsTest, "Unnamed.Voxel.Mesher.Cards",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FVoxelMesherCardsTest::RunTest(const FString& Parameters)
{
    const FVoxelMeshOptions Greedy;
    auto Meshed = [&Greedy](auto&& Fill) { return FMesherTestWorld(1, Fill).MeshColumn({0, 0}, Greedy); };
    auto Block = [](uint16 State)
    { return [State](int32 X, int32 Y, int32 Z) -> uint16 { return X == 5 && Y == 5 && Z == 5 ? State : 0; }; };

    // One block: a card per direction, looking along that direction and fitted to its face.
    const TArray<FVoxelMeshData> OneBlock = Meshed(Block(Stone));
    const FMeshCardsBuildData StoneCards = MesherCardsOf(OneBlock);
    TestEqual(TEXT("One block: 6 cards"), StoneCards.CardBuildData.Num(), 6);
    for (int32 Face = 0; Face < 6; ++Face)
    {
        const FLumenCardBuildData* Card = StoneCards.CardBuildData.FindByPredicate(
            [Face](const FLumenCardBuildData& Each) { return Each.AxisAlignedDirectionIndex == Face; });
        if (!TestNotNull(FString::Printf(TEXT("A card for direction %d"), Face), Card)) continue;
        TestTrue(FString::Printf(TEXT("Direction %d: the card looks along the face's normal"), Face),
            Card->OBB.AxisZ.Equals(MeshCardRepresentation::GetAxisAlignedDirection(Face), 1e-5f));
        TestTrue(FString::Printf(
                     TEXT("Direction %d: the card is a block wide and thin (%s)"), Face, *Card->OBB.Extent.ToString()),
            Card->OBB.Extent.X >= 50 && Card->OBB.Extent.Y >= 50 && Card->OBB.Extent.Z < 25);
    }
    TestEqual(TEXT("One block: every face is in a card"), CountUncardedFaces(OneBlock, StoneCards), 0);

    // Lumen captures opaque and masked surfaces only.
    TestEqual(TEXT("Glass gets no cards"), MesherCardsOf(Meshed(Block(Glass))).CardBuildData.Num(), 0);
    TestEqual(TEXT("Water gets no cards"), MesherCardsOf(Meshed(Block(WaterSource))).CardBuildData.Num(), 0);
    TestEqual(TEXT("Leaves (cutout) get 6 cards"), MesherCardsOf(Meshed(Block(Leaves))).CardBuildData.Num(), 6);

    // A pillar through every section at the column's corner, with nothing around it: 24 sections x 4 sides plus the
    // two ends would be 98 cards. They merge into Lumen's limit and still contain every face.
    const TArray<FVoxelMeshData> Pillar =
        Meshed([](int32 X, int32 Y, int32 Z) -> uint16 { return X == 0 && Y == 0 ? Stone : 0; });
    const FMeshCardsBuildData PillarCards = MesherCardsOf(Pillar);
    TestTrue(FString::Printf(TEXT("A full-height pillar: %d cards, at most %d"), PillarCards.CardBuildData.Num(),
                 UVoxelChunkComponent::MaxCards),
        PillarCards.CardBuildData.Num() <= UVoxelChunkComponent::MaxCards && PillarCards.CardBuildData.Num() >= 6);
    TestEqual(TEXT("A full-height pillar: every face is in a card"), CountUncardedFaces(Pillar, PillarCards), 0);

    // Random blocks of every kind (as in Mesher.Borders): every opaque and cutout face is in a card.
    constexpr uint64 Salt = VoxelRandom::Salt("mesher-cards");
    const TArray<FVoxelMeshData> Random = Meshed(
        [](int32 X, int32 Y, int32 Z) -> uint16
        {
            if (Z < -8 || Z > 47) return Air;
            if (Z < 0) return Stone;
            const int32 Roll = VoxelRandom::ToIndex(VoxelRandom::Hash64(11, Salt, X, Y, Z), 100);
            return Roll < 50 ? Air : Roll < 75 ? Stone : Roll < 85 ? Leaves : Roll < 92 ? Glass : SlabBottom;
        });
    const FMeshCardsBuildData RandomCards = MesherCardsOf(Random);
    AddInfo(FString::Printf(TEXT("Random blocks: %d cards"), RandomCards.CardBuildData.Num()));
    TestTrue(
        TEXT("Random blocks: within Lumen's limit"), RandomCards.CardBuildData.Num() <= UVoxelChunkComponent::MaxCards);
    TestEqual(
        TEXT("Random blocks: every opaque and cutout face is in a card"), CountUncardedFaces(Random, RandomCards), 0);
    return true;
}
namespace
{
    // Model states for the model tests, after VoxelTestBlocks' own.
    enum : uint16
    {
        ModelTestCross = VoxelTestBlocks::Count,
        ModelTestGrass,
        ModelTestCarpet,
        ModelTestLily,
        ModelTestKelp,
        ModelTestGlow,
        ModelTestLichen,
        ModelTestStateCount
    };
    constexpr uint32 ModelTestTint = 0x91BD59;
    constexpr int32 ModelTestZ = 66;

    // A quad through four points, its facing the axis nearest its normal (the side it turns against, as the cube
    // faces do), shaded as Shade (-1: as its facing).
    FVoxelModelQuad ModelTestQuad(const FVector3f& A, const FVector3f& B, const FVector3f& C, const FVector3f& D,
        uint32 Code, int32 Cull = -1, int32 Shade = -1, int32 Light = 0)
    {
        FVoxelModelQuad Quad;
        const FVector3f Points[4] = {A, B, C, D};
        const FVector2f UVs[4] = {{0, 0}, {1, 0}, {1, 1}, {0, 1}};
        for (int32 Vertex = 0; Vertex < 4; ++Vertex)
        {
            Quad.Positions[Vertex] = Points[Vertex];
            Quad.UVs[Vertex] = UVs[Vertex];
        }
        const FVector3f Normal = -FVector3f::CrossProduct(B - A, C - A);
        int32 Best = 0;
        for (int32 Face = 1; Face < 6; ++Face)
            if ((FVector3f(FoundationGrid::FaceNormal(EFoundationFace(Face))) | Normal) >
                (FVector3f(FoundationGrid::FaceNormal(EFoundationFace(Best))) | Normal))
                Best = Face;
        Quad.Facing = uint8(Best);
        Quad.Shade = uint8(Shade < 0 ? Best : Shade);
        Quad.CullFace = int8(Cull);
        Quad.Code = Code;
        Quad.Light = uint8(Light);
        return Quad;
    }

    // Both sides of a plane through four points.
    void ModelTestPlane(TArray<FVoxelModelQuad>& Out, const FVector3f& A, const FVector3f& B, const FVector3f& C,
        const FVector3f& D, uint32 Code, int32 Shade)
    {
        Out.Add(ModelTestQuad(A, B, C, D, Code, -1, Shade));
        Out.Add(ModelTestQuad(A, D, C, B, Code, -1, Shade));
    }

    // A box face, wound like the mesher's cube faces.
    FVoxelModelQuad ModelTestBoxFace(
        const FVector3f& Min, const FVector3f& Max, EVoxelFace Face, uint32 Code, int32 Cull)
    {
        const int32 Axis = VoxelFaces::GetAxis(int32(Face)), U = (Axis + 1) % 3, V = (Axis + 2) % 3;
        FVector3f Corners[4];
        for (int32 Corner = 0; Corner < 4; ++Corner)
        {
            Corners[Corner][Axis] = VoxelFaces::IsPositive(int32(Face)) ? Max[Axis] : Min[Axis];
            Corners[Corner][U] = Corner == 1 || Corner == 2 ? Max[U] : Min[U];
            Corners[Corner][V] = Corner >= 2 ? Max[V] : Min[V];
        }
        const FVector3f Normal(FoundationGrid::FaceNormal(EFoundationFace(Face)));
        if ((FVector3f::CrossProduct(Corners[1] - Corners[0], Corners[2] - Corners[0]) | Normal) > 0)
            Swap(Corners[1], Corners[3]);
        return ModelTestQuad(Corners[0], Corners[1], Corners[2], Corners[3], Code, Cull);
    }

    FVoxelBlockTable ModelTestTable()
    {
        FVoxelBlockTable Table = VoxelTestBlocks::Make();
        TArray<FVoxelModelQuad> Quads;
        // 0: a cross, two crossing planes with both faces, shaded up, tinted.
        ModelTestPlane(
            Quads, {0, 0, 0}, {1, 1, 0}, {1, 1, 1}, {0, 0, 1}, 900 | VoxelFaces::TintBit, int32(EVoxelFace::Up));
        ModelTestPlane(
            Quads, {1, 0, 0}, {0, 1, 0}, {0, 1, 1}, {1, 0, 1}, 900 | VoxelFaces::TintBit, int32(EVoxelFace::Up));
        const int32 Cross = Table.AddModel(Quads, false);
        // 1: a carpet, a box 1/16 high: the bottom and sides cull, the top does not; it takes ambient occlusion.
        Quads.Reset();
        for (int32 Face = 0; Face < 6; ++Face)
            Quads.Add(ModelTestBoxFace(
                {0, 0, 0}, {1, 1, 1.f / 16}, EVoxelFace(Face), 901, Face == int32(EVoxelFace::Up) ? -1 : Face));
        const int32 Carpet = Table.AddModel(Quads, true);
        // 2-5: four lily pad alternatives, told apart by their texture.
        int32 Lily[4];
        for (int32 Alternative = 0; Alternative < 4; ++Alternative)
        {
            Quads.Reset();
            Quads.Add(ModelTestBoxFace({0, 0, 0}, {1, 1, 1.f / 64}, EVoxelFace::Up, 910 + Alternative, -1));
            Lily[Alternative] = Table.AddModel(Quads, false);
        }
        // 6: a glowing model: one quad at element light 15, one without.
        Quads.Reset();
        Quads.Add(ModelTestBoxFace({0, 0, 0}, {1, 1, .5f}, EVoxelFace::Up, 920, -1));
        Quads.Last().Light = 15;
        Quads.Add(ModelTestBoxFace({0, 0, 0}, {1, 1, .5f}, EVoxelFace::Down, 921, -1));
        const int32 Glow = Table.AddModel(Quads, false);
        // 7: a ceiling plane facing down, its override turned to south by the blockstate rotation.
        Quads.Reset();
        Quads.Add(ModelTestBoxFace({0, 0, .9f}, {1, 1, .9f}, EVoxelFace::Down, 930, -1));
        Quads.Last().Shade = uint8(EVoxelFace::South);
        const int32 Lichen = Table.AddModel(Quads, false);

        auto Add = [&Table](uint16 State, FName Name, uint16 Block, TArray<TArray<int32>> Parts)
        {
            FVoxelStateInfo Info;
            Info.Shape = EVoxelShape::Model;
            Info.Layer = EVoxelLayer::Model;
            Info.Block = Block;
            Info.Tint = ModelTestTint;
            Info.TintSource = EVoxelTintSource::Grass;
            TArray<TConstArrayView<int32>> Views;
            for (const TArray<int32>& Part : Parts)
                Views.Add(Part);
            Info.ModelParts = Table.AddModelParts(Views);
            Info.ModelPartCount = uint8(Parts.Num());
            Table.SetState(State, Info);
            Table.SetDefaultState(Name, State);
            return Info;
        };
        Add(ModelTestCross, TEXT("cross"), 20, {{Cross}});
        FVoxelStateInfo Grass = Add(ModelTestGrass, TEXT("short_grass"), 21, {{Cross}});
        Grass.Offset = EVoxelOffset::HorizontalAndDown;
        Grass.OffsetHorizontal = .25f;
        Grass.OffsetDown = -.2f;
        Table.SetState(ModelTestGrass, Grass);
        Add(ModelTestCarpet, TEXT("carpet"), 22, {{Carpet}});
        Add(ModelTestLily, TEXT("lily_pad"), 23, {{Lily[0], Lily[1], Lily[2], Lily[3]}});
        FVoxelStateInfo Kelp = Add(ModelTestKelp, TEXT("kelp"), 24, {{Cross}});
        Kelp.HeldFluid = VoxelTestBlocks::WaterSource;
        Table.SetState(ModelTestKelp, Kelp);
        FVoxelStateInfo Lit = Add(ModelTestGlow, TEXT("glow"), 25, {{Glow}});
        Lit.LightEmission = 2;
        Table.SetState(ModelTestGlow, Lit);
        Add(ModelTestLichen, TEXT("glow_lichen"), 26, {{Lichen}});
        return Table;
    }

    // One column at the origin filled by Fill(X, Y, Z) (local X and Y, world Z), its section at ModelTestZ meshed
    // alone.
    template <typename FillType>
    FVoxelMeshData ModelTestMesh(const FVoxelBlockTable& Table, FillType&& Fill, const FVoxelMeshOptions& Options = {},
        TSharedPtr<FVoxelColumn>* OutColumn = nullptr)
    {
        TSharedRef<FVoxelColumn> Column = VoxelTestBlocks::MakeColumn(Table, {0, 0}, Fill);
        if (OutColumn) *OutColumn = Column;
        FVoxelNeighborhood Neighborhood;
        Neighborhood.Set(0, 0, &Column.Get());
        const int32 Section = (ModelTestZ - VoxelConstants::MinZ) / VoxelConstants::SectionSize;
        TUniquePtr<FVoxelPaddedSection> Padded = MakeUnique<FVoxelPaddedSection>();
        VoxelMesher::GatherPadded(Neighborhood, Section, *Padded);
        FVoxelMeshData Out;
        VoxelMesher::MeshSection(Table, *Padded, Section, Options, Out, nullptr, &Neighborhood);
        return Out;
    }

    // One state at (8, 8, ModelTestZ) on a stone floor, plus whatever Extra places.
    auto ModelTestOne(uint16 State, TFunction<uint16(int32, int32, int32)> Extra = nullptr)
    {
        return [State, Extra](int32 X, int32 Y, int32 Z) -> uint16
        {
            if (Extra)
                if (const uint16 Placed = Extra(X, Y, Z)) return Placed;
            if (Z < ModelTestZ) return VoxelTestBlocks::Stone;
            return X == 8 && Y == 8 && Z == ModelTestZ ? State : VoxelTestBlocks::Air;
        };
    }

    const FVoxelMeshLayer& ModelTestLayer(const FVoxelMeshData& Mesh)
    {
        return Mesh.Layers[int32(EVoxelLayer::Model)];
    }
}

// The see-through part (water and sky W3c): SplitSeeThrough hands it exactly the see-through layer's vertices and
// triangles, rebased, and leaves the column every other layer.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVoxelMesherSeeThroughTest, "Unnamed.Voxel.Mesher.SeeThroughPart",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FVoxelMesherSeeThroughTest::RunTest(const FString& Parameters)
{
    using namespace RealtimeMesh;
    constexpr int32 LayerCount = int32(EVoxelLayer::Count);
    constexpr int32 SeeThrough = int32(EVoxelLayer::Translucent);
    constexpr float BlockCm = float(UFoundationProfile::BlockCm);
    // Two sections with a quad on every layer, the second with two see-through quads (three in all: six triangles,
    // padded to eight); a vertex's x is its layer.
    FVoxelMeshData Meshes[2];
    int32 Quads[LayerCount] = {};
    for (int32 MeshIndex = 0; MeshIndex < 2; ++MeshIndex)
        for (int32 Layer = 0; Layer < LayerCount; ++Layer)
            for (int32 Quad = 0; Quad < (MeshIndex == 1 && Layer == SeeThrough ? 2 : 1); ++Quad, ++Quads[Layer])
            {
                FVoxelMeshLayer& Out = Meshes[MeshIndex].Layers[Layer];
                const uint32 First = uint32(Out.Positions.Num());
                for (int32 Corner = 0; Corner < 4; ++Corner)
                {
                    Out.Positions.Add(FVector3f(float(Layer), float(Corner & 1), float(Corner >> 1) + Quad));
                    Out.BlockUVs.Add(FVector2f(float(Corner & 1), float(Corner >> 1)));
                    Out.Codes.Add(FVector2f(float(Layer), 0));
                    Out.TintFaces.Add(FVector2f(0, 5));
                    Out.Colors.Add(FColor::White);
                }
                Out.Indices.Append({First, First + 1, First + 2, First, First + 2, First + 3});
            }
    const TArray<const FVoxelMeshData*> Sections = {&Meshes[0], &Meshes[1]};
    FRealtimeMeshStreamSet Streams;
    UVoxelChunkComponent::BuildStreams(Sections, Streams);
    const int32 Vertices = Streams.FindChecked(FRealtimeMeshStreams::Position).Num();
    const int32 Triangles = Streams.FindChecked(FRealtimeMeshStreams::Triangles).Num();
    // Each layer's triangles, padded to a multiple of four (BuildStreams).
    const int32 SeeThroughTriangles = Align(Quads[SeeThrough] * 2, 4);

    FRealtimeMeshStreamSet Part;
    TestTrue(TEXT("The set splits"), UVoxelChunkComponent::SplitSeeThrough(Streams, Part));
    // Every vertex stream of a set keeps one count; every index stream counts its triangles.
    auto CheckSet = [this](const TCHAR* What, const FRealtimeMeshStreamSet& Set, int32 WantVertices,
                        int32 WantTriangles, TFunctionRef<bool(float, uint16)> Belongs)
    {
        bool bCounts = true;
        Set.ForEach([&](const FRealtimeMeshStream& Stream)
            { bCounts &= Stream.Num() == (Stream.GetStreamKey().IsVertexStream() ? WantVertices : WantTriangles); });
        TestTrue(FString::Printf(
                     TEXT("%s: %d vertices and %d triangles in every stream"), What, WantVertices, WantTriangles),
            bCounts);
        const FRealtimeMeshStream& Positions = Set.FindChecked(FRealtimeMeshStreams::Position);
        const FRealtimeMeshStream& TriangleStream = Set.FindChecked(FRealtimeMeshStreams::Triangles);
        const FRealtimeMeshStream& Groups = Set.FindChecked(FRealtimeMeshStreams::PolyGroups);
        int32 Outside = 0, Strangers = 0;
        for (int32 Triangle = 0; Triangle < TriangleStream.Num(); ++Triangle)
        {
            const TIndex3<uint32>& Corners = TriangleStream.GetData<TIndex3<uint32>>()[Triangle];
            for (const uint32 Corner : Corners.Indices)
            {
                if (Corner >= uint32(Positions.Num()))
                {
                    ++Outside;
                    continue;
                }
                const float Layer = Positions.GetData<FVector3f>()[Corner].X / BlockCm;
                Strangers += !Belongs(Layer, Groups.GetData<uint16>()[Triangle]);
            }
        }
        TestEqual(FString::Printf(TEXT("%s: every index within its vertices"), What), Outside, 0);
        TestEqual(FString::Printf(TEXT("%s: every triangle's corners on its own layer"), What), Strangers, 0);
    };
    CheckSet(TEXT("The part"), Part, Quads[SeeThrough] * 4, SeeThroughTriangles,
        [](float Layer, uint16 Group) { return Layer == SeeThrough && Group == SeeThrough; });
    CheckSet(TEXT("The column"), Streams, Vertices - Quads[SeeThrough] * 4, Triangles - SeeThroughTriangles,
        [](float Layer, uint16 Group) { return Layer != SeeThrough && Group == uint16(Layer); });
    // The part's vertices in the sections' order: section 0's quad, then section 1's two.
    const FRealtimeMeshStream& PartPositions = Part.FindChecked(FRealtimeMeshStreams::Position);
    bool bOrder = PartPositions.Num() == 12;
    for (int32 Vertex = 0; bOrder && Vertex < 12; ++Vertex)
    {
        const FVoxelMeshLayer& Source = Meshes[Vertex < 4 ? 0 : 1].Layers[SeeThrough];
        bOrder &=
            PartPositions.GetData<FVector3f>()[Vertex] == Source.Positions[Vertex < 4 ? Vertex : Vertex - 4] * BlockCm;
    }
    TestTrue(TEXT("The part's vertices are the see-through layer's, in order"), bOrder);

    // Without see-through triangles nothing moves.
    for (FVoxelMeshData& Mesh : Meshes)
        Mesh.Layers[SeeThrough] = FVoxelMeshLayer();
    UVoxelChunkComponent::BuildStreams(Sections, Streams);
    const int32 Before = Streams.FindChecked(FRealtimeMeshStreams::Triangles).Num();
    FRealtimeMeshStreamSet None;
    TestFalse(TEXT("No see-through layer: no split"), UVoxelChunkComponent::SplitSeeThrough(Streams, None));
    TestEqual(TEXT("...and the column keeps its triangles"), Streams.FindChecked(FRealtimeMeshStreams::Triangles).Num(),
        Before);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVoxelMesherModelsTest, "Unnamed.Voxel.Mesher.Models",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FVoxelMesherModelsTest::RunTest(const FString& Parameters)
{
    const FVoxelBlockTable Table = ModelTestTable();

    // A cross on stone: its four quads, unmoved, in the model layer only, shaded up, open, untinted without biomes.
    {
        const FVoxelMeshData Mesh = ModelTestMesh(Table, ModelTestOne(ModelTestCross));
        const FVoxelMeshLayer& Layer = ModelTestLayer(Mesh);
        TestEqual(TEXT("cross: four quads"), Layer.GetQuadCount(), 4);
        TestEqual(
            TEXT("cross: nothing in the cutout layer"), Mesh.Layers[int32(EVoxelLayer::Cutout)].GetQuadCount(), 0);
        TestEqual(TEXT("cross: six indices per quad"), Layer.Indices.Num(), 24);
        const FVoxelModelQuad& First = Table.GetModelQuads(0)[0];
        TestTrue(TEXT("cross: placed at its cell"),
            Layer.Positions[0].Equals(FVector3f(8.f, 8.f, float(ModelTestZ)) + First.Positions[0], 1e-5f));
        bool bShade = true, bOpen = true, bCode = true, bTint = true;
        for (int32 Vertex = 0; Vertex < Layer.Positions.Num(); ++Vertex)
        {
            bShade &= Layer.TintFaces[Vertex].Y == float(EVoxelFace::Up);
            bTint &= Layer.TintFaces[Vertex].X == float(ModelTestTint);
            bOpen &= Layer.Colors[Vertex].R == 255 && Layer.Colors[Vertex].G == 0;
            bCode &= Layer.Codes[Vertex] == FVector2f(float(900 | VoxelFaces::TintBit), 0);
        }
        TestTrue(TEXT("cross: every vertex shaded as up (the override)"), bShade);
        TestTrue(TEXT("cross: the state's plains tint without biomes"), bTint);
        TestTrue(TEXT("cross: open (no ambient occlusion) and unlit"), bOpen);
        TestTrue(TEXT("cross: the quad's own code, no overlay"), bCode);

        FVoxelMeshOptions Off;
        Off.bModels = false;
        TestTrue(TEXT("voxel.Models 0: no model quads"),
            ModelTestLayer(ModelTestMesh(Table, ModelTestOne(ModelTestCross), Off)).IsEmpty());

        // Lumen never cards model quads: the stone floor's cards are the same with and without the cross.
        FMeshCardsBuildData Cards, Bare;
        const FVoxelMeshData Without = ModelTestMesh(Table, ModelTestOne(ModelTestCross), Off);
        const FVoxelMeshData* Sections[] = {&Mesh};
        const FVoxelMeshData* BareSections[] = {&Without};
        UVoxelChunkComponent::BuildCards(Sections, Cards);
        UVoxelChunkComponent::BuildCards(BareSections, Bare);
        bool bSame = Cards.CardBuildData.Num() == Bare.CardBuildData.Num() && Cards.Bounds == Bare.Bounds;
        for (int32 Card = 0; bSame && Card < Cards.CardBuildData.Num(); ++Card)
            bSame = Cards.CardBuildData[Card].OBB.Origin == Bare.CardBuildData[Card].OBB.Origin &&
                Cards.CardBuildData[Card].OBB.Extent == Bare.CardBuildData[Card].OBB.Extent;
        TestTrue(
            FString::Printf(TEXT("cross: the model quads add no card (%d cards)"), Cards.CardBuildData.Num()), bSame);
    }

    // Culling: a carpet on stone with stone to its east loses its bottom and east quads, never its top.
    {
        auto East = [](int32 X, int32 Y, int32 Z) -> uint16
        { return X == 9 && Y == 8 && Z == ModelTestZ ? VoxelTestBlocks::Stone : 0; };
        TestEqual(TEXT("carpet: six quads in the air above"),
            ModelTestLayer(ModelTestMesh(Table, [](int32 X, int32 Y, int32 Z) -> uint16
                               { return X == 8 && Y == 8 && Z == ModelTestZ ? ModelTestCarpet : 0; }))
                .GetQuadCount(),
            6);
        const FVoxelMeshData Mesh = ModelTestMesh(Table, ModelTestOne(ModelTestCarpet, East));
        const FVoxelMeshLayer& Layer = ModelTestLayer(Mesh);
        TestEqual(TEXT("carpet: bottom and east culled"), Layer.GetQuadCount(), 4);
        // Glass covers nothing: the carpet keeps its quad toward it.
        auto GlassEast = [](int32 X, int32 Y, int32 Z) -> uint16
        { return X == 9 && Y == 8 && Z == ModelTestZ ? VoxelTestBlocks::Glass : 0; };
        TestEqual(TEXT("carpet: glass hides nothing"),
            ModelTestLayer(ModelTestMesh(Table, ModelTestOne(ModelTestCarpet, GlassEast))).GetQuadCount(), 5);

        // Ambient occlusion (R9): the top lies 1/16 above its cell's floor, so it takes the full face there, read
        // around its own cell: the stone to the east darkens its east corners by one level.
        int32 Up = INDEX_NONE;
        for (int32 Quad = 0; Quad < Layer.GetQuadCount(); ++Quad)
            if (Layer.TintFaces[Quad * 4].Y == float(EVoxelFace::Up)) Up = Quad;
        if (TestTrue(TEXT("carpet: the top is drawn"), Up != INDEX_NONE))
            for (int32 Vertex = Up * 4; Vertex < Up * 4 + 4; ++Vertex)
            {
                const bool bEast = Layer.Positions[Vertex].X > 8.5f;
                TestEqual(FString::Printf(TEXT("carpet top corner %d occlusion"), Vertex - Up * 4),
                    int32(Layer.Colors[Vertex].R), bEast ? 170 : 255);
            }
        FVoxelMeshOptions NoOcclusion;
        NoOcclusion.bAmbientOcclusion = false;
        const FVoxelMeshLayer Flat =
            ModelTestLayer(ModelTestMesh(Table, ModelTestOne(ModelTestCarpet, East), NoOcclusion));
        bool bOpen = Flat.GetQuadCount() == 4;
        for (const FColor& Color : Flat.Colors)
            bOpen &= Color.R == 255;
        TestTrue(TEXT("carpet: ambient occlusion off leaves it open"), bOpen);
        // Its east side, 1/16 high at the cell's east edge, faces the air beyond when nothing is there: open.
        TestEqual(TEXT("carpet: the table finds the top's front as its own cell"),
            int32(Table.GetModelQuads(1)[int32(EVoxelFace::Up)].OcclusionFront), int32(FVoxelModelQuad::OwnCell));
        TestEqual(TEXT("carpet: a side's front is the cell beyond it"),
            int32(Table.GetModelQuads(1)[int32(EVoxelFace::East)].OcclusionFront), int32(EVoxelFace::East));
        TestEqual(TEXT("cross: no ambient occlusion"), int32(Table.GetModelQuads(0)[0].OcclusionFront), -1);
    }

    // Offsets (R6): a function of X and Y only, within the block's range; the mesher applies it.
    {
        const FVoxelStateInfo& Grass = Table.Get(ModelTestGrass);
        TSet<float> Seen;
        bool bInRange = true;
        for (int32 Y = -40; Y < 40; ++Y)
            for (int32 X = -40; X < 40; ++X)
            {
                const FVector3f Offset = VoxelModels::GetOffset(Grass, X, Y);
                bInRange &= FMath::Abs(Offset.X) <= .25f + 1e-6f && FMath::Abs(Offset.Y) <= .25f + 1e-6f &&
                    Offset.Z <= 0 && Offset.Z >= -.2f - 1e-6f;
                Seen.Add(Offset.X);
            }
        TestTrue(TEXT("offsets stay within +-0.25 and [-0.2, 0]"), bInRange);
        TestTrue(FString::Printf(TEXT("offsets vary (%d distinct X offsets)"), Seen.Num()), Seen.Num() >= 12);
        TestTrue(TEXT("the plain cross has none"),
            VoxelModels::GetOffset(Table.Get(ModelTestCross), 3, 4) == FVector3f::ZeroVector);
        // (x, y, z) and (x, y, z + 1): one cell above the other, the same offset.
        const FVoxelMeshLayer Low = ModelTestLayer(ModelTestMesh(Table, ModelTestOne(ModelTestGrass)));
        const FVoxelMeshLayer High = ModelTestLayer(ModelTestMesh(Table,
            [](int32 X, int32 Y, int32 Z) -> uint16
            {
                return Z < ModelTestZ + 1                     ? VoxelTestBlocks::Stone
                    : X == 8 && Y == 8 && Z == ModelTestZ + 1 ? ModelTestGrass
                                                              : 0;
            }));
        if (TestTrue(TEXT("grass drawn in both cells"), Low.GetQuadCount() == 4 && High.GetQuadCount() == 4))
        {
            const FVector3f Expected = VoxelModels::GetOffset(Grass, 8, 8);
            TestTrue(TEXT("the mesher moves the quads by the offset"),
                (Low.Positions[0] - FVector3f(8.f, 8.f, float(ModelTestZ)) - Table.GetModelQuads(0)[0].Positions[0])
                    .Equals(Expected, 1e-5f));
            TestTrue(TEXT("one cell up: the same offset"),
                (High.Positions[0] - FVector3f(0.f, 0.f, 1.f)).Equals(Low.Positions[0], 1e-5f));
        }
    }

    // Alternatives (R7): one per part by position, every one of the four used, the same each time.
    {
        auto Field = [](int32 X, int32 Y, int32 Z) -> uint16
        {
            return Z < ModelTestZ ? VoxelTestBlocks::Stone : Z == ModelTestZ ? ModelTestLily : 0;
        };
        const FVoxelMeshLayer Layer = ModelTestLayer(ModelTestMesh(Table, Field));
        const FVoxelMeshLayer Again = ModelTestLayer(ModelTestMesh(Table, Field));
        TestEqual(TEXT("lily pads: one quad each"), Layer.GetQuadCount(), 256);
        int32 Counts[4] = {};
        bool bPicked = true;
        for (int32 Quad = 0; Quad < Layer.GetQuadCount(); ++Quad)
        {
            const int32 Alternative = int32(Layer.Codes[Quad * 4].X) - 910;
            if (Alternative >= 0 && Alternative < 4) ++Counts[Alternative];
            float MinX = Layer.Positions[Quad * 4].X, MinY = Layer.Positions[Quad * 4].Y;
            for (int32 Vertex = Quad * 4 + 1; Vertex < Quad * 4 + 4; ++Vertex)
            {
                MinX = FMath::Min(MinX, Layer.Positions[Vertex].X);
                MinY = FMath::Min(MinY, Layer.Positions[Vertex].Y);
            }
            const int32 X = FMath::RoundToInt32(MinX), Y = FMath::RoundToInt32(MinY);
            bPicked &= Alternative == VoxelModels::PickAlternative(X, Y, ModelTestZ, 0, 4);
        }
        TestTrue(FString::Printf(TEXT("all four used (%d, %d, %d, %d)"), Counts[0], Counts[1], Counts[2], Counts[3]),
            Counts[0] > 30 && Counts[1] > 30 && Counts[2] > 30 && Counts[3] > 30);
        TestTrue(TEXT("each quad is its cell's pick"), bPicked);
        TestTrue(TEXT("the same pick every time"), Layer.Codes == Again.Codes && Layer.Positions == Again.Positions);
    }

    // Water around a water-holding state, never against water; the heightmaps count it.
    {
        TSharedPtr<FVoxelColumn> Column;
        const FVoxelMeshData Mesh = ModelTestMesh(Table, ModelTestOne(ModelTestKelp), {}, &Column);
        TestEqual(
            TEXT("kelp: its water's top and four sides"), Mesh.Layers[int32(EVoxelLayer::Water)].GetQuadCount(), 5);
        TestEqual(TEXT("kelp: its cross"), ModelTestLayer(Mesh).GetQuadCount(), 4);
        TestEqual(TEXT("kelp: motion blocking counts its water"),
            Column->GetHeight(EVoxelHeightmap::MotionBlocking, 8, 8), ModelTestZ);
        TestEqual(TEXT("kelp: the ocean floor is the stone below"),
            Column->GetHeight(EVoxelHeightmap::OceanFloor, 8, 8), ModelTestZ - 1);
        FVoxelMeshOptions Off;
        Off.bModels = false;
        const FVoxelMeshData Bare = ModelTestMesh(Table, ModelTestOne(ModelTestKelp), Off);
        TestEqual(TEXT("voxel.Models 0: the water stays"), Bare.Layers[int32(EVoxelLayer::Water)].GetQuadCount(), 5);
        TestEqual(TEXT("voxel.Models 0: no cross"), ModelTestLayer(Bare).GetQuadCount(), 0);
        // A source to the east: no face between them on either side; the source keeps its top and three sides.
        auto East = [](int32 X, int32 Y, int32 Z) -> uint16
        { return X == 9 && Y == 8 && Z == ModelTestZ ? VoxelTestBlocks::WaterSource : 0; };
        const FVoxelMeshData Beside = ModelTestMesh(Table, ModelTestOne(ModelTestKelp, East));
        const FVoxelMeshLayer& Water = Beside.Layers[int32(EVoxelLayer::Water)];
        // The two tops merge into one quad.
        TestEqual(TEXT("kelp beside water: one top and 3 + 3 sides"), Water.GetQuadCount(), 7);
        int32 Between = 0;
        for (int32 Quad = 0; Quad < Water.GetQuadCount(); ++Quad)
        {
            bool bOnPlane = true;
            for (int32 Vertex = Quad * 4; Vertex < Quad * 4 + 4; ++Vertex)
                bOnPlane &= FMath::IsNearlyEqual(Water.Positions[Vertex].X, 9.f);
            Between += bOnPlane;
        }
        TestEqual(TEXT("kelp beside water: nothing on the plane between"), Between, 0);
    }

    // Emission: the brighter of the state's light and the element's.
    {
        const FVoxelMeshLayer Layer = ModelTestLayer(ModelTestMesh(Table, ModelTestOne(ModelTestGlow)));
        if (TestEqual(TEXT("glow: two quads"), Layer.GetQuadCount(), 2))
        {
            TestEqual(TEXT("glow: the element's light 15"), int32(Layer.Colors[0].G), 255);
            TestEqual(TEXT("glow: the state's light 2"), int32(Layer.Colors[4].G), 34);
        }
    }

    // The look-call switch: a quad shaded other than its facing is shaded as up; the rest keep their shading.
    {
        FVoxelMeshOptions Unturned;
        Unturned.bModelShadeUnturned = true;
        const FVoxelMeshLayer Turned = ModelTestLayer(ModelTestMesh(Table, ModelTestOne(ModelTestLichen)));
        const FVoxelMeshLayer Up = ModelTestLayer(ModelTestMesh(Table, ModelTestOne(ModelTestLichen), Unturned));
        if (TestTrue(TEXT("lichen: one quad both ways"), Turned.GetQuadCount() == 1 && Up.GetQuadCount() == 1))
        {
            TestEqual(TEXT("lichen: shaded as its turned override"), Turned.TintFaces[0].Y, float(EVoxelFace::South));
            TestEqual(TEXT("lichen unturned: shaded as up"), Up.TintFaces[0].Y, float(EVoxelFace::Up));
        }
        const FVoxelMeshLayer Carpet = ModelTestLayer(ModelTestMesh(Table, ModelTestOne(ModelTestCarpet)));
        const FVoxelMeshLayer CarpetUp = ModelTestLayer(ModelTestMesh(Table, ModelTestOne(ModelTestCarpet), Unturned));
        TestTrue(
            TEXT("unturned: quads without an override keep their shading"), Carpet.TintFaces == CarpetUp.TintFaces);
    }
    return true;
}

namespace
{
    // Quads of one model on the same four corners as an earlier one (thin planes, R3): how many pairs, and how many
    // face the same way. The one allowed exception: the firefly bush's emissive overlay (model 302, elements 4-7,
    // light_emission 15 in its model file) lies on the four planes of its cross (elements 0-3), facing the same way,
    // and draws after them.
    void ModelDataPairs(FAutomationTestBase& Test, int32 Model, TConstArrayView<FVoxelModelQuad> Quads, int32& OutPairs,
        int32& OutSameWay)
    {
        auto Turn = [](const FVoxelModelQuad& Quad)
        {
            return FVector3f::CrossProduct(
                Quad.Positions[1] - Quad.Positions[0], Quad.Positions[2] - Quad.Positions[0]);
        };
        for (int32 Index = 0; Index < Quads.Num(); ++Index)
            for (int32 Other = Index + 1; Other < Quads.Num(); ++Other)
            {
                int32 Shared = 0;
                for (const FVector3f& A : Quads[Index].Positions)
                    for (const FVector3f& B : Quads[Other].Positions)
                        Shared += A.Equals(B, 1e-4f);
                if (Shared != 4) continue;
                ++OutPairs;
                if ((Turn(Quads[Index]) | Turn(Quads[Other])) < 0) continue;
                const bool bFireflyOverlay = Model == 302 && Index < 4 && Other == Index + 4 &&
                    Quads[Index].Light == 0 && Quads[Other].Light == 15;
                if (bFireflyOverlay) continue;
                Test.AddInfo(FString::Printf(TEXT("same-facing pair: model %d quads %d and %d"), Model, Index, Other));
                ++OutSameWay;
            }
    }
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVoxelMesherModelDataTest, "Unnamed.Voxel.Mesher.ModelData",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FVoxelMesherModelDataTest::RunTest(const FString& Parameters)
{
    // The real block table's copy of the baked models (S5's 303 models).
    const TSharedRef<const FVoxelBlockTable> Table = VoxelBlocks::GetDefault();
    if (!Table->FindState(TEXT("stone")))
    {
        AddError(TEXT("DA_Blocks is missing: run ImportBlocks.py"));
        return false;
    }
    TestEqual(TEXT("baked models (block_models.json's 4,867)"), Table->GetModelCount(), 4867);
    int32 Quads = 0, Wound = 0, Twins = 0, BadTwins = 0;
    for (int32 Model = 0; Model < Table->GetModelCount(); ++Model)
    {
        const TConstArrayView<FVoxelModelQuad> ModelQuads = Table->GetModelQuads(Model);
        for (int32 Index = 0; Index < ModelQuads.Num(); ++Index)
        {
            const FVoxelModelQuad& Quad = ModelQuads[Index];
            ++Quads;
            // The cube faces' convention: Cross(B - A, C - A) opposes the facing normal (Unreal draws clockwise).
            const FVector3f Normal(FoundationGrid::FaceNormal(EFoundationFace(Quad.Facing)));
            const FVector3f Turn =
                FVector3f::CrossProduct(Quad.Positions[1] - Quad.Positions[0], Quad.Positions[2] - Quad.Positions[0]);
            Wound += (Turn | Normal) < 0;
        }
        ModelDataPairs(*this, Model, ModelQuads, Twins, BadTwins);
    }
    // The check catches a thin plane turned the wrong way: model 0's first pair, its second quad wound backwards.
    {
        TArray<FVoxelModelQuad> Broken(Table->GetModelQuads(0));
        int32 Pairs = 0, SameWay = 0;
        for (int32 Other = 1; Other < Broken.Num() && !SameWay; ++Other)
        {
            Swap(Broken[Other].Positions[1], Broken[Other].Positions[3]);
            ModelDataPairs(*this, 0, Broken, Pairs, SameWay);
            if (!SameWay) Swap(Broken[Other].Positions[1], Broken[Other].Positions[3]);
        }
        TestTrue(TEXT("mutation: a thin plane wound backwards is caught"), SameWay > 0);
    }
    // 52,741 baked quads (block_models.json: 52,261 + the 16 bed item models' 30 each), less the 16 exact repeats the
    // copy drops (flowerbed segments that list a plane twice), plus the 20 the torch's flame cut adds (4 sides in each
    // of its 5 models; T3b).
    TestEqual(TEXT("model quads"), Quads, 52745);
    TestEqual(TEXT("quads wound like the cube faces"), Wound, Quads);
    TestTrue(FString::Printf(TEXT("zero-thickness planes have both faces (%d pairs)"), Twins), Twins > 100);
    // The bars' side models (iron_bars and the four copper bars: models 3109-3110, 3119-3120, 3129-3130, 3139-3140,
    // 3149-3150) keep vanilla's 0.001-pixel caps: a down face at y 0.001/16 over the element's own down face at 0,
    // and an up face just under its top, closer than the pair check's 1e-4. Two pairs each, 20 in all, as in vanilla.
    TestEqual(TEXT("each pair faces opposite ways (but the bars' 20 caps)"), BadTwins, 20);

    // A wall with no post and no sides (up=false, every side none) draws nothing in vanilla: an empty model.
    const TSharedRef<const FFoundationBlockRegistry> Registry = FFoundationBlockRegistry::GetDefault();
    auto IsBareWall = [&Registry](uint16 Id)
    {
        const FFoundationBlockState State{Id};
        if (!Registry->GetBlock(State).Id.ToString().EndsWith(TEXT("_wall")) ||
            Registry->GetValue(State, TEXT("up")) != TEXT("false"))
            return false;
        for (const TCHAR* Side : {TEXT("north"), TEXT("east"), TEXT("south"), TEXT("west")})
            if (Registry->GetValue(State, Side) != TEXT("none")) return false;
        return true;
    };
    int32 ModelStates = 0, BadParts = 0, BareWalls = 0;
    for (int32 State = 1; State < Table->GetStateCount(); ++State)
    {
        const FVoxelStateInfo& Info = Table->Get(uint16(State));
        if (Info.Shape != EVoxelShape::Model) continue;
        ++ModelStates;
        // Model states draw in the model section, a translucent block's (the panes) in the translucent one.
        const bool bTranslucent =
            Registry->GetBlock(FFoundationBlockState{uint16(State)}).Layer == EFoundationRenderLayer::Translucent;
        BadParts += Info.Layer != (bTranslucent ? EVoxelLayer::Translucent : EVoxelLayer::Model) ||
            Info.OpaqueFaces != 0 || (Info.ModelPartCount == 0 && !IsBareWall(uint16(State)));
        BareWalls += Info.ModelPartCount == 0 && IsBareWall(uint16(State));
        const int32* Part = Table->GetModelParts(Info);
        for (int32 Index = 0; Index < Info.ModelPartCount; ++Index)
        {
            const int32 Alternatives = *Part++;
            BadParts += Alternatives < 1;
            for (int32 Alternative = 0; Alternative < Alternatives; ++Alternative)
                BadParts += Part[Alternative] < 0 || Part[Alternative] >= Table->GetModelCount();
            Part += Alternatives;
        }
    }
    TestEqual(TEXT("model states (block_states.json's 25,082)"), ModelStates, 25082);
    // block_manifest.json's 32 walls, each bare dry and bare waterlogged.
    TestEqual(TEXT("bare wall states with no parts"), BareWalls, 64);
    TestEqual(TEXT("model states with bad parts, layer or covered faces"), BadParts, 0);

    auto Default = [&Table](const TCHAR* Block) -> const FVoxelStateInfo&
    { return Table->Get(Table->FindState(Block)); };
    const FVoxelStateInfo& Lily = Default(TEXT("lily_pad"));
    TestTrue(
        TEXT("lily pad: one part of 4 alternatives"), Lily.ModelPartCount == 1 && Table->GetModelParts(Lily)[0] == 4);
    TestTrue(
        TEXT("lily pad: the constant #208030"), Lily.TintSource == EVoxelTintSource::Constant && Lily.Tint == 0x208030);
    TestTrue(TEXT("leaf litter: dry foliage"), Default(TEXT("leaf_litter")).TintSource == EVoxelTintSource::DryFoliage);
    TestTrue(TEXT("short grass: grass"), Default(TEXT("short_grass")).TintSource == EVoxelTintSource::Grass);
    TestTrue(TEXT("vine: foliage"), Default(TEXT("vine")).TintSource == EVoxelTintSource::Foliage);
    TestTrue(TEXT("kelp holds water"), Default(TEXT("kelp")).HeldFluid != 0 && Default(TEXT("kelp")).HoldsFluid());
    TestTrue(TEXT("a dandelion holds none"), Default(TEXT("dandelion")).HeldFluid == 0);
    // Codex review 15 #2: a stained glass pane's model draws in the translucent section, keeping its half-transparent
    // pixels, not in the cutout model section.
    {
        const uint16 Pane = Table->FindState(TEXT("white_stained_glass_pane"));
        const uint16 Floor = Table->FindState(TEXT("stone"));
        const FVoxelMeshData PaneMesh = ModelTestMesh(*Table, [Pane, Floor](int32 X, int32 Y, int32 Z) -> uint16
            { return Z < ModelTestZ                     ? Floor
                  : X == 8 && Y == 8 && Z == ModelTestZ ? Pane
                                                        : uint16(0); });
        TestTrue(TEXT("a stained glass pane draws in the translucent section"),
            Pane != 0 && Table->Get(Pane).Layer == EVoxelLayer::Translucent &&
                PaneMesh.Layers[int32(EVoxelLayer::Translucent)].GetQuadCount() > 0 &&
                ModelTestLayer(PaneMesh).IsEmpty());
    }
    const FVoxelStateInfo& Grass = Default(TEXT("short_grass"));
    TestTrue(FString::Printf(TEXT("short grass: XYZ offset +-0.25, down to -0.2 (%g, %g)"), Grass.OffsetHorizontal,
                 Grass.OffsetDown),
        Grass.Offset == EVoxelOffset::HorizontalAndDown && FMath::IsNearlyEqual(Grass.OffsetHorizontal, .25f, 1e-4f) &&
            FMath::IsNearlyEqual(Grass.OffsetDown, -.2f, 1e-4f));
    const FVoxelStateInfo& Dripstone = Default(TEXT("pointed_dripstone"));
    TestTrue(TEXT("pointed dripstone: XZ offset +-0.125"),
        Dripstone.Offset == EVoxelOffset::Horizontal && FMath::IsNearlyEqual(Dripstone.OffsetHorizontal, .125f, 1e-4f));
    // Glow lichen glows 7 wherever it has a face (its default state has none).
    int32 LichenLight = 0;
    for (int32 State = 1; State < Table->GetStateCount(); ++State)
        if (Table->Get(uint16(State)).Block == Default(TEXT("glow_lichen")).Block)
            LichenLight = FMath::Max<int32>(LichenLight, Table->Get(uint16(State)).LightEmission);
    TestEqual(TEXT("glow lichen: light 7"), LichenLight, 7);
    bool bFireflyGlows = false;
    const FVoxelStateInfo& Firefly = Default(TEXT("firefly_bush"));
    const int32* Part = Table->GetModelParts(Firefly);
    for (int32 Alternative = 0; Alternative < Part[0]; ++Alternative)
        for (const FVoxelModelQuad& Quad : Table->GetModelQuads(Part[1 + Alternative]))
            bFireflyGlows |= Quad.Light == 15;
    TestTrue(TEXT("firefly bush: its overlay quads carry light 15"), bFireflyGlows);
    return true;
}

// Codex review 13 #1: a waterlogged slab holds a water source in the voxel table (swum in, fogged, drawn), and the
// mesher draws that water around the slab; a dry slab draws none.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVoxelMesherWaterloggedSlabTest, "Unnamed.Voxel.Mesher.WaterloggedSlab",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FVoxelMesherWaterloggedSlabTest::RunTest(const FString&)
{
    const TSharedRef<const FFoundationBlockRegistry> Blocks = FFoundationBlockRegistry::GetDefault();
    const FFoundationBlockRegistry& Registry = *Blocks;
    const TSharedRef<const FVoxelBlockTable> Table = VoxelBlocks::GetDefault();
    const FFoundationBlockType* Slab = Registry.FindBlock(TEXT("oak_slab"));
    if (!TestNotNull(TEXT("oak slab"), Slab)) return false;
    const FFoundationBlockState Dry = Registry.WithValue(Slab->GetDefaultState(), TEXT("type"), TEXT("bottom"));
    const FFoundationBlockState Wet = Registry.WithValue(Dry, TEXT("waterlogged"), TEXT("true"));
    TestEqual(TEXT("a bottom slab"), Table->Get(Dry.Id).Shape, EVoxelShape::SlabBottom);
    TestTrue(TEXT("the waterlogged slab holds a water source"),
        Registry.GetFluid(Wet).bWater && Registry.GetFluid(Wet).bSource && Table->Get(Wet.Id).HeldFluid != 0);
    TestTrue(TEXT("the dry slab holds none"), !Registry.GetFluid(Dry).bWater && Table->Get(Dry.Id).HeldFluid == 0);
    TestTrue(TEXT("the block meshes see its water"),
        FoundationBlockMeshes::HoldsFluid(Registry, Wet) && !FoundationBlockMeshes::HoldsFluid(Registry, Dry));
    const uint16 Stone = Table->FindState(TEXT("stone"));
    auto WaterQuads = [&](FFoundationBlockState State)
    {
        const FVoxelMeshData Mesh = ModelTestMesh(*Table,
            [&](int32 X, int32 Y, int32 Z) -> uint16
            {
                if (Z < ModelTestZ) return Stone;
                return X == 8 && Y == 8 && Z == ModelTestZ ? State.Id : uint16(0);
            });
        return Mesh.Layers[int32(EVoxelLayer::Water)].Positions.Num();
    };
    TestEqual(TEXT("a dry slab draws no water"), WaterQuads(Dry), 0);
    TestTrue(TEXT("a waterlogged slab draws its water"), WaterQuads(Wet) > 0);
    return true;
}
#endif
