#if WITH_DEV_AUTOMATION_TESTS
#include "Foundation/FoundationGrid.h"
#include "Foundation/FoundationProfile.h"
#include "MeshCardBuild.h"
#include "MeshCardRepresentation.h"
#include "Meshing/VoxelMesher.h"
#include "Misc/AutomationTest.h"
#include "Random/VoxelRandom.h"
#include "Tests/VoxelTestBlocks.h"
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
#endif
