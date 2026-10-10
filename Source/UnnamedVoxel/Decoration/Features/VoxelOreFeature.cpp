#include "Decoration/Features/VoxelOreFeature.h"
#include "Decoration/VoxelFeatureGraph.h"

namespace
{
    bool OreFail(FString* OutError, const FString& Message)
    {
        if (OutError) *OutError = Message;
        return false;
    }

    // Sets Count bits from Bit on.
    FORCEINLINE void SetBitRun(uint64* Words, int32 Bit, int32 Count)
    {
        while (Count > 0)
        {
            const int32 Offset = Bit & 63, Run = FMath::Min(Count, 64 - Offset);
            Words[Bit >> 6] |= (Run == 64 ? ~uint64(0) : (uint64(1) << Run) - 1) << Offset;
            Bit += Run;
            Count -= Run;
        }
    }

    struct FBlobSphere
    {
        float X, Y, Z, Radius;
    };
}

VoxelOreFeature::FReach VoxelOreFeature::GetReach(int32 Size)
{
    // Sphere radii stay under Size / 16 + 1/2; a cell counts when its centre is inside, so one more block covers the
    // half cell between a centre and its edge.
    const double Radius = Size / 16. + .5;
    FReach Reach;
    Reach.Horizontal = int32(FMath::CeilToDouble(Size / 8. + Radius)) + 1;
    Reach.Down = int32(FMath::CeilToDouble(2 + Radius)) + 1;
    Reach.Up = int32(FMath::CeilToDouble(Radius)) + 1;
    return Reach;
}

void VoxelOreFeature::GetBlobCells(int32 Size, FIntVector Origin, FVoxelXoroshiro& Random, TArray<FIntVector>& Out)
{
    Out.Reset();
    if (Size <= 0) return;
    const double Angle = Random.NextDouble() * UE_DOUBLE_PI;
    const double Half = Size / 8.;
    const double X0 = Origin.X + FMath::Sin(Angle) * Half, X1 = Origin.X - FMath::Sin(Angle) * Half;
    const double Y0 = Origin.Y + FMath::Cos(Angle) * Half, Y1 = Origin.Y - FMath::Cos(Angle) * Half;
    const double Z0 = Origin.Z - 1 + Random.NextRange(-1, 1), Z1 = Origin.Z - 1 + Random.NextRange(-1, 1);

    // Candidates are marked in a box around the origin, so a cell inside two spheres counts once. Z runs fastest, so
    // a sphere's span in one cell column is one run of bits.
    const FReach Reach = GetReach(Size);
    const int32 Side = 2 * Reach.Horizontal + 1, Height = Reach.Down + Reach.Up + 1;
    const int32 ZShift = int32(FMath::CeilLogTwo(uint32(Height))), YShift = int32(FMath::CeilLogTwo(uint32(Side)));
    const int32 XShift = YShift + ZShift;
    const int32 BaseX = Origin.X - Reach.Horizontal, BaseY = Origin.Y - Reach.Horizontal, BaseZ = Origin.Z - Reach.Down;
    TArray<uint64, TInlineAllocator<256>> Marks;
    Marks.SetNumZeroed(((Side << XShift) + 63) / 64);
    uint64* Words = Marks.GetData();

    // Spheres are marked in float, in box coordinates (all positive, so truncation floors). A sphere inside one of
    // the last few marked adds nothing, so it is skipped; its radius is still drawn.
    constexpr int32 KeptMax = 4;
    FBlobSphere Kept[KeptMax] = {};
    int32 KeptCount = 0;
    const float LX0 = float(X0 - BaseX), LX1 = float(X1 - BaseX), LY0 = float(Y0 - BaseY), LY1 = float(Y1 - BaseY);
    const float LZ0 = float(Z0 - BaseZ), LZ1 = float(Z1 - BaseZ);
    // The largest integer strictly below Value, and the smallest strictly above it, for Value >= 0.
    const auto Below = [](float Value)
    {
        const int32 Whole = int32(Value);
        return float(Whole) == Value ? Whole - 1 : Whole;
    };
    const auto Above = [](float Value) { return int32(Value) + 1; };
    for (int32 Index = 0; Index < Size; ++Index)
    {
        const float T = float(Index) / Size;
        FBlobSphere Sphere = {LX0 + (LX1 - LX0) * T, LY0 + (LY1 - LY0) * T, LZ0 + (LZ1 - LZ0) * T, 0};
        Sphere.Radius = float((Random.NextDouble() * Size / 16. * (FMath::Sin(UE_DOUBLE_PI * T) + 1) + 1) / 2);
        bool bInside = false;
        for (int32 K = 0; K < FMath::Min(KeptCount, KeptMax) && !bInside; ++K)
        {
            const FBlobSphere& Other = Kept[K];
            const float Room = Other.Radius - Sphere.Radius;
            bInside = Room >= 0 &&
                FMath::Square(Sphere.X - Other.X) + FMath::Square(Sphere.Y - Other.Y) +
                        FMath::Square(Sphere.Z - Other.Z) <=
                    Room * Room;
        }
        if (bInside) continue;
        Kept[KeptCount++ % KeptMax] = Sphere;

        // Cells whose centre (cell + 1/2) lies strictly inside the sphere.
        const float Radius2 = Sphere.Radius * Sphere.Radius;
        const int32 MaxX = Below(Sphere.X + Sphere.Radius - .5f);
        for (int32 X = Above(Sphere.X - Sphere.Radius - .5f); X <= MaxX; ++X)
        {
            const float DX = X + .5f - Sphere.X;
            const float RestX = Radius2 - DX * DX;
            if (RestX <= 0) continue;
            const float SpanY = FMath::Sqrt(RestX);
            const int32 MaxY = Below(Sphere.Y + SpanY - .5f);
            for (int32 Y = Above(Sphere.Y - SpanY - .5f); Y <= MaxY; ++Y)
            {
                const float DY = Y + .5f - Sphere.Y;
                const float RestY = RestX - DY * DY;
                if (RestY <= 0) continue;
                const float SpanZ = FMath::Sqrt(RestY);
                const int32 MinZ = Above(Sphere.Z - SpanZ - .5f), MaxZ = Below(Sphere.Z + SpanZ - .5f);
                if (MaxZ >= MinZ) SetBitRun(Words, (X << XShift) + (Y << ZShift) + MinZ, MaxZ - MinZ + 1);
            }
        }
    }

    const int32 ZMask = (1 << ZShift) - 1, YMask = (1 << YShift) - 1;
    for (int32 Word = 0; Word < Marks.Num(); ++Word)
        for (uint64 Bits = Words[Word]; Bits; Bits &= Bits - 1)
        {
            const int32 Bit = Word * 64 + int32(FMath::CountTrailingZeros64(Bits));
            Out.Add(FIntVector(BaseX + (Bit >> XShift), BaseY + (Bit >> ZShift & YMask), BaseZ + (Bit & ZMask)));
        }
}

bool VoxelOreFeature::Compile(const FVoxelFeatureGraph& Graph, int32 FeatureNode, FVoxelBlockTags& Tags,
    const FVoxelBlockTable& Table, FVoxelOreConfig& OutConfig, TArray<FVoxelOreTarget>& OutTargets, FString& OutSkip,
    FString* OutError)
{
    const FVoxelFeatureValue* Size = Graph.FindField(FeatureNode, TEXT("size"));
    const FVoxelFeatureValue* Discard = Graph.FindField(FeatureNode, TEXT("discard_chance_on_air_exposure"));
    const FVoxelFeatureValue* TargetsValue = Graph.FindField(FeatureNode, TEXT("targets"));
    if (!Size || !Discard || !TargetsValue) return OreFail(OutError, TEXT("ore without size, discard or targets"));
    OutConfig.Size = int32(Size->Number);
    OutConfig.DiscardOnAir = float(Discard->Number);
    // A blob must stay inside the 3x3 its column's Decorate sees, its air-exposure neighbours included.
    if (GetReach(OutConfig.Size).Horizontal > 15)
    {
        OutSkip = TEXT("ore larger than a 3x3 holds");
        return true;
    }
    OutTargets.Reset();
    for (int32 Item = 0; Item < TargetsValue->Count; ++Item)
    {
        const int32 TargetNode = Graph.GetNode(Graph.Items[TargetsValue->Index + Item]);
        const FVoxelFeatureValue* State = Graph.FindField(TargetNode, TEXT("state"));
        const FVoxelFeatureValue* Rule = Graph.FindField(TargetNode, TEXT("target"));
        if (!State || !Rule) return OreFail(OutError, TEXT("ore target without state or target"));
        // A state is a block id, or {"id"} with no properties for an ore.
        FStringView Id;
        if (State->Type == EVoxelFeatureValueType::String)
            Id = Graph.Strings[State->Index];
        else if (const int32 StateNode = Graph.GetNode(*State); StateNode != INDEX_NONE)
        {
            if (Graph.FindField(StateNode, TEXT("properties")))
            {
                OutSkip = TEXT("ore state with properties");
                return true;
            }
            const FVoxelFeatureValue* IdValue = Graph.FindField(StateNode, TEXT("id"));
            if (IdValue && IdValue->Type == EVoxelFeatureValueType::String) Id = Graph.Strings[IdValue->Index];
        }
        FVoxelOreTarget Target;
        if (!Target.Rule.Compile(Graph, Graph.GetNode(*Rule), Tags, OutError)) return false;
        const int32 Block = Tags.FindBlock(Id);
        if (Block == INDEX_NONE) continue;
        Target.State = Table.FindState(FName(Id.StartsWith(TEXT("minecraft:")) ? Id.RightChop(10) : Id));
        Target.Id = FString(Id);
        OutTargets.Add(MoveTemp(Target));
    }
    if (OutTargets.IsEmpty())
    {
        OutSkip = TEXT("ore whose blocks the table lacks");
        return true;
    }
    return true;
}
