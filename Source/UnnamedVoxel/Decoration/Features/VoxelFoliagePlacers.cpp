#include "Decoration/Features/VoxelTreeFeature.h"
#include "VoxelConstants.h"

// The foliage placers (phase 6, S9a). Each shape is ours where minecraft.wiki's "Tree definition" is silent, fitted to
// scout J's per-layer profiles in C:\dev\sandbox\phase6s9\fit (Docs/VoxelWorld.md, "Trees"). Fields are read by the
// data's names, each sampled once per tree.

namespace
{
    const FName FoliageRadius(TEXT("radius")), FoliageOffset(TEXT("offset")), FoliageHeight(TEXT("height")),
        FoliageTrunkHeight(TEXT("trunk_height"));

    // The chance a pine's radius cap grows by one more layer (ours, fitted: 96 growths in 134 tries of the pine probe).
    constexpr float PineGrowChance = 0.72f;

    // Corners always cut (a radius 0 layer keeps its single cell).
    bool CutCorners(int32 AbsX, int32 AbsY, int32 Radius)
    {
        return Radius > 0 && AbsX == Radius && AbsY == Radius;
    }

    // One square layer of leaves of Radius around Center (Z fixed). A double trunk widens it to -Radius .. Radius + 1,
    // and Skip sees the offsets folded so the 2 x 2 stays in the middle: Skip(AbsX, AbsY, Radius) returns true for a
    // cell to leave out.
    template <typename FSkip>
    void PlaceLayer(FVoxelTreeBuilder& Builder, FIntVector Center, int32 Radius, bool bDouble, FSkip&& Skip)
    {
        const int32 High = bDouble ? Radius + 1 : Radius;
        for (int32 DX = -Radius; DX <= High; ++DX)
            for (int32 DY = -Radius; DY <= High; ++DY)
            {
                const int32 AbsX = bDouble && DX > 0 ? FMath::Abs(DX - 1) : FMath::Abs(DX);
                const int32 AbsY = bDouble && DY > 0 ? FMath::Abs(DY - 1) : FMath::Abs(DY);
                if (Skip(AbsX, AbsY, Radius)) continue;
                Builder.SetLeaf(Center + FIntVector(DX, DY, 0));
            }
    }

    // blob_foliage_placer (ours, fitted to oak, birch, jungle tree and swamp oak): layers t = 0 .. height downward from
    // the attachment plus offset; radius R + bonus - 1 on the top two, R + bonus below; corners always cut on the top
    // layer, cut with chance 1/2 each on the others.
    void PlaceBlob(const FVoxelTreePlacerConfig& Foliage, FVoxelTreeBuilder& Builder,
        TConstArrayView<FVoxelFoliageAttachment> Attachments)
    {
        FVoxelXoroshiro& Random = Builder.Random;
        const int32 Radius = Foliage.SampleInt(FoliageRadius, Random);
        const int32 Offset = Foliage.SampleInt(FoliageOffset, Random);
        const int32 Height = Foliage.SampleInt(FoliageHeight, Random);
        for (const FVoxelFoliageAttachment& Attachment : Attachments)
            for (int32 Layer = 0; Layer <= Height; ++Layer)
            {
                const int32 LayerRadius = Radius + Attachment.RadiusBonus - (Layer <= 1 ? 1 : 0);
                const FIntVector Center = Attachment.Position + FIntVector(0, 0, Offset - Layer);
                PlaceLayer(Builder, Center, FMath::Max(0, LayerRadius), Attachment.bDoubleTrunk,
                    [&Random, Layer](int32 AbsX, int32 AbsY, int32 R)
                    { return R > 0 && AbsX == R && AbsY == R && (Layer == 0 || Random.NextInt(2) == 0); });
            }
    }

    // bush_foliage_placer (ours, fitted to jungle bush): layers t = 0 .. height downward from the attachment plus
    // offset, radius R + bonus - height + t (0, 1, 2 from the top for the data's R 2, height 2); every corner, the
    // single cell of a radius 0 layer included, is cut with chance 1/2.
    void PlaceBush(const FVoxelTreePlacerConfig& Foliage, FVoxelTreeBuilder& Builder,
        TConstArrayView<FVoxelFoliageAttachment> Attachments)
    {
        FVoxelXoroshiro& Random = Builder.Random;
        const int32 Radius = Foliage.SampleInt(FoliageRadius, Random);
        const int32 Offset = Foliage.SampleInt(FoliageOffset, Random);
        const int32 Height = Foliage.SampleInt(FoliageHeight, Random);
        for (const FVoxelFoliageAttachment& Attachment : Attachments)
            for (int32 Layer = 0; Layer <= Height; ++Layer)
            {
                const int32 LayerRadius = FMath::Max(0, Radius + Attachment.RadiusBonus - Height + Layer);
                const FIntVector Center = Attachment.Position + FIntVector(0, 0, Offset - Layer);
                PlaceLayer(Builder, Center, LayerRadius, Attachment.bDoubleTrunk,
                    [&Random](int32 AbsX, int32 AbsY, int32 R)
                    { return AbsX == R && AbsY == R && Random.NextInt(2) == 0; });
            }
    }

    // spruce_foliage_placer (radius, offset and trunk_height from the data; the rest ours, fitted to spruce): layers
    // from the attachment plus offset down to Depth = max(4, trunk height - trunk_height) below the attachment, which
    // leaves trunk_height bare logs at the base (exactly 1 on a 5-high trunk). The radius starts at s (0 or 1, chance
    // 1/2 each) with limit 1 and floor 0; after each layer it drops to the floor once it has reached the limit (then
    // the floor becomes 1 and the limit grows by one, up to R + bonus), else grows by one: 0,1,0,1,2,1,2,1,2.. for R 2.
    // Corners always cut, so a radius 0 layer on a log adds nothing.
    void PlaceSpruce(const FVoxelTreePlacerConfig& Foliage, FVoxelTreeBuilder& Builder,
        TConstArrayView<FVoxelFoliageAttachment> Attachments)
    {
        FVoxelXoroshiro& Random = Builder.Random;
        const int32 Radius = Foliage.SampleInt(FoliageRadius, Random);
        const int32 Offset = Foliage.SampleInt(FoliageOffset, Random);
        const int32 TrunkHeight = Foliage.SampleInt(FoliageTrunkHeight, Random);
        const int32 Start = Random.NextInt(2);
        const int32 Depth = FMath::Max(4, Builder.Height - TrunkHeight);
        for (const FVoxelFoliageAttachment& Attachment : Attachments)
        {
            const int32 MaxRadius = Radius + Attachment.RadiusBonus;
            int32 LayerRadius = Start, Limit = 1, Floor = 0;
            for (int32 Z = Offset; Z >= -Depth; --Z)
            {
                PlaceLayer(Builder, Attachment.Position + FIntVector(0, 0, Z), LayerRadius, Attachment.bDoubleTrunk,
                    CutCorners);
                if (LayerRadius >= Limit)
                {
                    LayerRadius = Floor;
                    Floor = 1;
                    Limit = FMath::Min(Limit + 1, MaxRadius);
                }
                else
                    ++LayerRadius;
            }
        }
    }

    // pine_foliage_placer (ours, fitted to pine): layers t = 0 .. height downward from the attachment plus offset. The
    // radius cap M starts at R + bonus and grows by one while below height, with PineGrowChance each, stopping at the
    // first failure; layer t < height has radius min(t, M), the bottom layer one less than the layer above. Corners
    // always cut.
    void PlacePine(const FVoxelTreePlacerConfig& Foliage, FVoxelTreeBuilder& Builder,
        TConstArrayView<FVoxelFoliageAttachment> Attachments)
    {
        FVoxelXoroshiro& Random = Builder.Random;
        const int32 Radius = Foliage.SampleInt(FoliageRadius, Random);
        const int32 Offset = Foliage.SampleInt(FoliageOffset, Random);
        const int32 Height = Foliage.SampleInt(FoliageHeight, Random);
        for (const FVoxelFoliageAttachment& Attachment : Attachments)
        {
            int32 Cap = Radius + Attachment.RadiusBonus;
            while (Cap < Height && Random.NextChance(PineGrowChance))
                ++Cap;
            int32 LayerRadius = 0;
            for (int32 Layer = 0; Layer <= Height; ++Layer)
            {
                LayerRadius = Layer < Height ? FMath::Min(Layer, Cap) : FMath::Max(0, LayerRadius - 1);
                PlaceLayer(Builder, Attachment.Position + FIntVector(0, 0, Offset - Layer), LayerRadius,
                    Attachment.bDoubleTrunk, CutCorners);
            }
        }
    }

    // fancy_foliage_placer (radius, offset and height from the data; the shape ours, exact against fancy oak's
    // single-cluster trees): layers t = 0 .. height downward from the attachment plus offset, radius R + bonus on the
    // inner layers and one less on the end layers, corners always cut. For the data's R 2, offset 4, height 4: a plus,
    // three 5 x 5 squares without corners and a plus, 73 cells. No draws.
    void PlaceFancyFoliage(const FVoxelTreePlacerConfig& Foliage, FVoxelTreeBuilder& Builder,
        TConstArrayView<FVoxelFoliageAttachment> Attachments)
    {
        FVoxelXoroshiro& Random = Builder.Random;
        const int32 Radius = Foliage.SampleInt(FoliageRadius, Random);
        const int32 Offset = Foliage.SampleInt(FoliageOffset, Random);
        const int32 Height = Foliage.SampleInt(FoliageHeight, Random, 4);
        for (const FVoxelFoliageAttachment& Attachment : Attachments)
            for (int32 Layer = 0; Layer <= Height; ++Layer)
            {
                const int32 LayerRadius = Radius + Attachment.RadiusBonus - (Layer > 0 && Layer < Height ? 0 : 1);
                PlaceLayer(Builder, Attachment.Position + FIntVector(0, 0, Offset - Layer), FMath::Max(0, LayerRadius),
                    Attachment.bDoubleTrunk, CutCorners);
            }
    }

    // acacia_foliage_placer (radius and offset from the data; the shape ours, exact against acacia's trees without a
    // fork: 57 leaves): with r = R + bonus, the layer at the attachment plus offset is a square of radius r + 1 without
    // its corners; the layer above keeps the cells within distance r of the centre (dX^2 + dY^2 <= r^2) for the main
    // cluster, the full square of radius r for a fork's (negative bonus). No draws.
    void PlaceAcacia(const FVoxelTreePlacerConfig& Foliage, FVoxelTreeBuilder& Builder,
        TConstArrayView<FVoxelFoliageAttachment> Attachments)
    {
        FVoxelXoroshiro& Random = Builder.Random;
        const int32 Radius = Foliage.SampleInt(FoliageRadius, Random);
        const int32 Offset = Foliage.SampleInt(FoliageOffset, Random);
        for (const FVoxelFoliageAttachment& Attachment : Attachments)
        {
            const int32 LayerRadius = FMath::Max(0, Radius + Attachment.RadiusBonus);
            const bool bRound = Attachment.RadiusBonus >= 0;
            const FIntVector Center = Attachment.Position + FIntVector(0, 0, Offset);
            PlaceLayer(Builder, Center, LayerRadius + 1, Attachment.bDoubleTrunk, CutCorners);
            PlaceLayer(Builder, Center + FIntVector(0, 0, 1), LayerRadius, Attachment.bDoubleTrunk,
                [bRound](int32 AbsX, int32 AbsY, int32 R) { return bRound && AbsX * AbsX + AbsY * AbsY > R * R; });
        }
    }

    const FName CherryCornerHole(TEXT("corner_hole_chance")), CherryWideHole(TEXT("wide_bottom_layer_hole_chance")),
        CherryHanging(TEXT("hanging_leaves_chance")), CherryHangingExtension(TEXT("hanging_leaves_extension_chance"));

    // The chance a wide layer's edge cell that survived wide_bottom_layer_hole_chance is still left out (ours, fitted:
    // those cells are present 0.5625 of the time in the probe's canopies).
    constexpr double CherryWideExtraHole = 0.25;

    // The cherry canopy's layers from the top: height above the attachment, radius below R, and the hole rule.
    enum class ECherryLayer : uint8
    {
        // Corners left out with corner_hole_chance.
        Plain,
        // Corners never; the cells beside them left out with corner_hole_chance.
        Near,
        // Corners never; the other edge cells but the axis tips left out with wide_bottom_layer_hole_chance, else
        // with CherryWideExtraHole.
        Wide
    };
    struct FCherryLayer
    {
        int32 Z, RadiusLess;
        ECherryLayer Rule;
        bool bHanging;
    };
    const FCherryLayer CherryLayers[5] = {{3, 3, ECherryLayer::Plain, false}, {2, 2, ECherryLayer::Plain, false},
        {1, 1, ECherryLayer::Near, false}, {0, 1, ECherryLayer::Wide, true}, {-1, 2, ECherryLayer::Plain, true}};

    // cherry_foliage_placer (radius, offset, corner_hole_chance, wide_bottom_layer_hole_chance, hanging_leaves_chance
    // and hanging_leaves_extension_chance from the data; the shape ours, fitted to cherry_bees_005; the data's height 5
    // is the five layers). Per attachment, five square layers from the top: 3 above the attachment (plus offset) radius
    // R - 3, then R - 2, R - 1, R - 1 at the attachment ("wide") and R - 2 one below, with CherryLayers' hole rules;
    // cells go X-major from -r to r and draw only where a rule applies. After the wide and the bottom layer, each edge
    // leaf that layer placed, in order, hangs a leaf below it with hanging_leaves_chance (where it can go), and that
    // one another below with hanging_leaves_extension_chance.
    void PlaceCherryFoliage(const FVoxelTreePlacerConfig& Foliage, FVoxelTreeBuilder& Builder,
        TConstArrayView<FVoxelFoliageAttachment> Attachments)
    {
        FVoxelXoroshiro& Random = Builder.Random;
        const int32 Radius = Foliage.SampleInt(FoliageRadius, Random);
        const int32 Offset = Foliage.SampleInt(FoliageOffset, Random);
        Foliage.SampleInt(FoliageHeight, Random, 5);
        const double CornerHole = Foliage.GetNumber(CherryCornerHole), WideHole = Foliage.GetNumber(CherryWideHole);
        const double Hanging = Foliage.GetNumber(CherryHanging), Extension = Foliage.GetNumber(CherryHangingExtension);
        TArray<FIntVector, TInlineAllocator<64>> Edges;
        for (const FVoxelFoliageAttachment& Attachment : Attachments)
            for (const FCherryLayer& Layer : CherryLayers)
            {
                const int32 R = FMath::Max(0, Radius + Attachment.RadiusBonus - Layer.RadiusLess);
                const FIntVector Center = Attachment.Position + FIntVector(0, 0, Offset + Layer.Z);
                Edges.Reset();
                for (int32 DX = -R; DX <= R; ++DX)
                    for (int32 DY = -R; DY <= R; ++DY)
                    {
                        const int32 Far = FMath::Max(FMath::Abs(DX), FMath::Abs(DY));
                        const int32 Near = FMath::Min(FMath::Abs(DX), FMath::Abs(DY));
                        bool bSkip = false;
                        if (Layer.Rule == ECherryLayer::Plain)
                            bSkip = R > 0 && Far == R && Near == R && Random.NextDouble() < CornerHole;
                        else if (Far == R && Near == R)
                            bSkip = true;
                        else if (Layer.Rule == ECherryLayer::Near)
                            bSkip = Far == R && Near == R - 1 && Random.NextDouble() < CornerHole;
                        else
                            bSkip = Far == R && Near > 0 &&
                                (Random.NextDouble() < WideHole || Random.NextDouble() < CherryWideExtraHole);
                        if (bSkip) continue;
                        const FIntVector Cell = Center + FIntVector(DX, DY, 0);
                        if (Builder.SetLeaf(Cell) && Far == R) Edges.Add(Cell);
                    }
                if (!Layer.bHanging) continue;
                for (const FIntVector& Edge : Edges)
                    if (Random.NextDouble() < Hanging && Builder.SetLeaf(Edge - FIntVector(0, 0, 1)) &&
                        Random.NextDouble() < Extension)
                        Builder.SetLeaf(Edge - FIntVector(0, 0, 2));
            }
    }

    const FName PoplarHoleChance(TEXT("side_hole_chance"));

    // One poplar layer around Center: the cells with max(|dX|, |dY|) <= B, |v| <= B and |u| <= B + Long, u = dX + dY
    // and v = dX - dY (swapped when bFlip), so the layer stretches along one diagonal. A cell on the boundary (equality
    // in any of the three) is left out with Hole. X-major from -B to B.
    void PlacePoplarLayer(FVoxelTreeBuilder& Builder, FIntVector Center, int32 B, int32 Long, bool bFlip, double Hole)
    {
        for (int32 DX = -B; DX <= B; ++DX)
            for (int32 DY = -B; DY <= B; ++DY)
            {
                int32 U = FMath::Abs(DX + DY), V = FMath::Abs(DX - DY);
                if (bFlip) Swap(U, V);
                if (V > B || U > B + Long) continue;
                const bool bEdge = V == B || U == B + Long || FMath::Max(FMath::Abs(DX), FMath::Abs(DY)) == B;
                if (bEdge && Builder.Random.NextDouble() < Hole) continue;
                Builder.SetLeaf(Center + FIntVector(DX, DY, 0));
            }
    }

    // poplar_foliage_placer (radius, height, offset and side_hole_chance from the data; the shape ours, fitted to the
    // three poplar probes). Draws in order: r = radius + bonus (counted with the trunk's cell: the widest layer reaches
    // r - 1), the height f, the offset, the diagonal flip (1/2), then the hole rolls. Before any leaf, logs at 1 ..
    // r - 5 out along the four directions on layer 3. Layers t = 0, 1, .. from f - 4 above the top log (plus offset)
    // down to just above the trunk's branch layer, B = r - 4, r - 3, r - 2, then r - 1 for f - 4 layers, then r - 2;
    // the branch layer itself has B = 2. A layer stretches 2 along its diagonal for t 0 and 1, else 1.
    void PlacePoplarFoliage(const FVoxelTreePlacerConfig& Foliage, FVoxelTreeBuilder& Builder,
        TConstArrayView<FVoxelFoliageAttachment> Attachments)
    {
        static const FIntPoint Directions[4] = {{1, 0}, {0, 1}, {-1, 0}, {0, -1}};
        FVoxelXoroshiro& Random = Builder.Random;
        const int32 Radius = Foliage.SampleInt(FoliageRadius, Random);
        const int32 Height = Foliage.SampleInt(FoliageHeight, Random, 5);
        const int32 Offset = Foliage.SampleInt(FoliageOffset, Random);
        const double Hole = Foliage.GetNumber(PoplarHoleChance);
        const bool bFlip = Random.NextDouble() < 0.5;
        for (const FVoxelFoliageAttachment& Attachment : Attachments)
        {
            const int32 R = Radius + Attachment.RadiusBonus;
            const int32 TopLog = Attachment.Position.Z - 1;
            const int32 TopZ = TopLog + Height - 4 + Offset;
            const int32 BranchZ = Attachment.Position.Z - (Attachment.BranchDepth > 0 ? Attachment.BranchDepth : 5);
            for (int32 Distance = 1; Distance <= R - 5; ++Distance)
                for (const FIntPoint& Direction : Directions)
                    Builder.SetLog(FIntVector(Attachment.Position.X + Direction.X * Distance,
                                       Attachment.Position.Y + Direction.Y * Distance, TopZ - 3),
                        Direction.X != 0 ? 0 : 1);
            int32 Layer = 0;
            for (int32 Z = TopZ; Z > BranchZ; --Z, ++Layer)
            {
                // r - 4, r - 3, r - 2, then r - 1 for f - 4 layers, then r - 2 for the rest.
                const int32 B = Layer < 3 ? R - 4 + Layer : Layer < Height - 1 ? R - 1 : R - 2;
                PlacePoplarLayer(Builder, FIntVector(Attachment.Position.X, Attachment.Position.Y, Z), FMath::Max(0, B),
                    Layer < 2 ? 2 : 1, bFlip, Hole);
            }
            PlacePoplarLayer(
                Builder, FIntVector(Attachment.Position.X, Attachment.Position.Y, BranchZ), 2, 1, bFlip, Hole);
        }
    }
}

EVoxelFoliagePlacer VoxelFoliagePlacers::FindType(FStringView Type)
{
    if (Type == TEXT("minecraft:blob_foliage_placer")) return EVoxelFoliagePlacer::Blob;
    if (Type == TEXT("minecraft:bush_foliage_placer")) return EVoxelFoliagePlacer::Bush;
    if (Type == TEXT("minecraft:spruce_foliage_placer")) return EVoxelFoliagePlacer::Spruce;
    if (Type == TEXT("minecraft:pine_foliage_placer")) return EVoxelFoliagePlacer::Pine;
    if (Type == TEXT("minecraft:fancy_foliage_placer")) return EVoxelFoliagePlacer::Fancy;
    if (Type == TEXT("minecraft:acacia_foliage_placer")) return EVoxelFoliagePlacer::Acacia;
    if (Type == TEXT("minecraft:cherry_foliage_placer")) return EVoxelFoliagePlacer::Cherry;
    if (Type == TEXT("minecraft:poplar_foliage_placer")) return EVoxelFoliagePlacer::Poplar;
    return EVoxelFoliagePlacer::Count;
}

void VoxelFoliagePlacers::Place(const FVoxelTreePlacerConfig& Foliage, FVoxelTreeBuilder& Builder,
    TConstArrayView<FVoxelFoliageAttachment> Attachments)
{
    switch (EVoxelFoliagePlacer(Foliage.Type))
    {
    case EVoxelFoliagePlacer::Blob:
        PlaceBlob(Foliage, Builder, Attachments);
        return;
    case EVoxelFoliagePlacer::Bush:
        PlaceBush(Foliage, Builder, Attachments);
        return;
    case EVoxelFoliagePlacer::Spruce:
        PlaceSpruce(Foliage, Builder, Attachments);
        return;
    case EVoxelFoliagePlacer::Pine:
        PlacePine(Foliage, Builder, Attachments);
        return;
    case EVoxelFoliagePlacer::Fancy:
        PlaceFancyFoliage(Foliage, Builder, Attachments);
        return;
    case EVoxelFoliagePlacer::Acacia:
        PlaceAcacia(Foliage, Builder, Attachments);
        return;
    case EVoxelFoliagePlacer::Cherry:
        PlaceCherryFoliage(Foliage, Builder, Attachments);
        return;
    case EVoxelFoliagePlacer::Poplar:
        PlacePoplarFoliage(Foliage, Builder, Attachments);
        return;
    default:
        return;
    }
}

int32 VoxelFoliagePlacers::GetReach(const FVoxelTreePlacerConfig& Foliage)
{
    switch (EVoxelFoliagePlacer(Foliage.Type))
    {
    case EVoxelFoliagePlacer::Blob:
        // The widest layer, plus one for a radius bonus.
        return Foliage.GetMaxInt(FoliageRadius) + 1;
    case EVoxelFoliagePlacer::Bush:
        // The bottom layer, R + bonus.
        return Foliage.GetMaxInt(FoliageRadius) + 1;
    case EVoxelFoliagePlacer::Spruce:
        // The limit reaches R + bonus (a start of 1 is the most when that is below 1).
        return FMath::Max(1, Foliage.GetMaxInt(FoliageRadius) + 1);
    case EVoxelFoliagePlacer::Pine:
        // min(t, M) with t below height: at most height - 1 whatever the bonus, plus one for a double trunk.
        return FMath::Max(1, Foliage.GetMaxInt(FoliageHeight));
    case EVoxelFoliagePlacer::Fancy:
        // The inner layers, R + bonus, plus one for a radius bonus.
        return Foliage.GetMaxInt(FoliageRadius) + 1;
    case EVoxelFoliagePlacer::Acacia:
        // The bottom layer, r + 1, plus one for a radius bonus.
        return Foliage.GetMaxInt(FoliageRadius) + 2;
    case EVoxelFoliagePlacer::Cherry:
        // The R - 1 layers, plus one for a radius bonus.
        return Foliage.GetMaxInt(FoliageRadius);
    case EVoxelFoliagePlacer::Poplar:
        // The widest layer, r - 1 (the cross logs reach r - 5), plus one for a radius bonus.
        return Foliage.GetMaxInt(FoliageRadius);
    default:
        return VoxelConstants::ColumnSize;
    }
}
