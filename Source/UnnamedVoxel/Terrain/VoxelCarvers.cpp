#include "Terrain/VoxelCarvers.h"
#include "Async/UniqueLock.h"
#include "Decoration/VoxelFeatureGraph.h"
#include "Random/VoxelRandom.h"
#include "Storage/VoxelColumn.h"
#include "VoxelConstants.h"

namespace
{
    constexpr int32 CarverColumn = VoxelConstants::ColumnSize;

    // The walker's shape (our own; the wiki's descriptions, with the free numbers fitted to scout K's black-box
    // statistics, facts.md 4A, in two rounds of C:\dev\sandbox\phase6s10\walkfit.py, a mirror of this walker: per
    // start, cave / extra / canyon, ours against the jar's: cells 5,310 / 5,180 / 10,500 against 5,134 / 4,885 /
    // 11,038, farthest chunk 6 / 6 / 7 against 6 / 6 / 7, top y 211 / 97 / 87 against 224 / 104 / 89; a third round
    // damped the canyons' pitch after the C++ walk topped out at 91). Caves (wiki "Cave", Carver caves): a system is a
    // room (1 in 4) joined to 1 to 4 trunks, or one trunk alone; a trunk may split into two branches; root to branch
    // end runs 85 to 112 blocks; a tunnel is thin at both ends and thick in the middle, flickers, and has a round
    // ceiling and a flat floor. Rooms are about 5 to 15 blocks across and 1 to 14 high.
    constexpr int32 CarverTunnelLength = 112, CarverTunnelLengthSpan = 28;
    constexpr double CarverRadiusBase = 1.5;
    constexpr int32 CarverRoomOdds = 4, CarverRoomTrunks = 4;
    constexpr double CarverRoomRadius = 2.5, CarverRoomRadiusSpan = 5;
    // Configured carver (wiki): weird_thickness_bias multiplies thickness by U * U * 3 + 1, one time in 10.
    constexpr int32 CarverWeirdOdds = 10;
    constexpr double CarverWeirdScale = 3;
    constexpr double CarverPitchStart = 0.25;
    constexpr double CarverTurn = 0.1, CarverYawKeep = 0.8, CarverYawKick = 4, CarverPitchKeep = 0.9,
                     CarverPitchKick = 2;
    constexpr double CarverPitchHold = 0.7, CarverPitchHoldSteep = 0.85;
    constexpr int32 CarverSteepOdds = 6;
    constexpr double CarverBranchMinThickness = 1, CarverBranchThickness = 0.5, CarverBranchThicknessBase = 0.5;
    constexpr int32 CarverSkipOdds = 4;
    // Canyons (wiki "Canyon"): about 85 to 127 blocks long, usually under 15 wide, small ledges along the top.
    constexpr int32 CarverCanyonLength = 112;
    constexpr double CarverCanyonRadiusBase = 2;
    constexpr double CarverCanyonPitchHold = 0.6, CarverCanyonYawKeep = 0.5, CarverCanyonYawKick = 4,
                     CarverCanyonPitchKeep = 0.8, CarverCanyonPitchKick = 1, CarverCanyonTurn = 0.05;
    constexpr double CarverLedge = 1;
    constexpr int32 CarverCanyonSkipOdds = 4;

    bool CarverFail(FString* OutError, const FString& Message)
    {
        if (OutError) *OutError = Message;
        return false;
    }

    // [Min, Max] columns a step's cells can reach (one block of margin); Start is the path's start column.
    void CarverStepColumns(const FVoxelCarveStep& Step, FIntPoint Start, FIntPoint& OutMin, FIntPoint& OutMax)
    {
        OutMin.X = Start.X + FMath::FloorToInt32((Step.X - Step.Horizontal - 1) / CarverColumn);
        OutMin.Y = Start.Y + FMath::FloorToInt32((Step.Y - Step.Horizontal - 1) / CarverColumn);
        OutMax.X = Start.X + FMath::FloorToInt32((Step.X + Step.Horizontal + 1) / CarverColumn);
        OutMax.Y = Start.Y + FMath::FloorToInt32((Step.Y + Step.Horizontal + 1) / CarverColumn);
    }

    // A tunnel of a cave system: steps First to Length - 1 from (X, Y, Z), which may split into two branches.
    struct FCarverTunnel
    {
        double X, Y, Z, Yaw, Pitch, Thickness, VerticalScale;
        int32 First, Length;
        bool bMayBranch;
    };
} // namespace

bool FVoxelCarverConfig::Compile(
    const FVoxelFeatureGraph& Graph, int32 Entry, FVoxelCarverConfig& Out, FString* OutError)
{
    const TArray<FVoxelFeatureEntry>& Entries = Graph.Entries[int32(EVoxelFeatureRegistry::Carver)];
    if (!Entries.IsValidIndex(Entry) || Entries[Entry].Node == INDEX_NONE)
        return CarverFail(OutError, TEXT("carver: no entry"));
    const int32 Node = Entries[Entry].Node;
    Out = FVoxelCarverConfig();
    Out.Id = Entries[Entry].Id;
    const FStringView Type = Graph.GetType(Node);
    const auto Error = [&](const TCHAR* What)
    { return CarverFail(OutError, FString::Printf(TEXT("%s: %s"), *Out.Id, What)); };
    const auto Number = [&](int32 Owner, const TCHAR* Key, double& Value)
    {
        const FVoxelFeatureValue* Field = Graph.FindField(Owner, Key);
        if (!Field || Field->Type != EVoxelFeatureValueType::Number) return false;
        Value = Field->Number;
        return true;
    };
    const auto Float = [&](int32 Owner, const TCHAR* Key, FVoxelFloatProvider& Provider)
    {
        const FVoxelFeatureValue* Field = Graph.FindField(Owner, Key);
        FString Reason;
        if (!Field || !VoxelNumberProviders::CompileFloat(Graph, *Field, Provider, &Reason))
            return CarverFail(OutError, FString::Printf(TEXT("%s: %s: %s"), *Out.Id, Key, *Reason));
        return true;
    };
    const FVoxelFeatureValue* YField = Graph.FindField(Node, TEXT("y"));
    if (!Number(Node, TEXT("probability"), Out.Probability)) return Error(TEXT("no probability"));
    if (!YField || !VoxelNumberProviders::CompileHeight(Graph, *YField, Out.Y, OutError)) return false;
    if (Type == TEXT("minecraft:cave"))
    {
        Out.Kind = EVoxelCarverKind::Cave;
        const FVoxelFeatureValue* CountField = Graph.FindField(Node, TEXT("count"));
        if (!CountField || !VoxelNumberProviders::CompileInt(Graph, *CountField, Out.Count, OutError)) return false;
        if (!Float(Node, TEXT("thickness"), Out.Thickness) || !Float(Node, TEXT("floor_level"), Out.FloorLevel) ||
            !Float(Node, TEXT("horizontal_radius_multiplier"), Out.HorizontalRadius) ||
            !Float(Node, TEXT("vertical_radius_multiplier"), Out.VerticalRadius) ||
            !Float(Node, TEXT("room_vertical_radius_multiplier"), Out.RoomVerticalRadius))
            return false;
        if (Graph.FindField(Node, TEXT("start_vertical_radius_multiplier")) &&
            !Float(Node, TEXT("start_vertical_radius_multiplier"), Out.StartVerticalRadius))
            return false;
        const FVoxelFeatureValue* Weird = Graph.FindField(Node, TEXT("weird_thickness_bias"));
        Out.bWeirdThicknessBias = Weird && Weird->Number != 0;
        return true;
    }
    if (Type == TEXT("minecraft:canyon"))
    {
        Out.Kind = EVoxelCarverKind::Canyon;
        const FVoxelFeatureValue* ShapeField = Graph.FindField(Node, TEXT("shape"));
        const int32 Shape = ShapeField ? Graph.GetNode(*ShapeField) : INDEX_NONE;
        if (Shape == INDEX_NONE) return Error(TEXT("no shape"));
        double Smoothness = 0, Default = 0, Center = 0, Scale = 0;
        if (!Float(Node, TEXT("vertical_rotation"), Out.VerticalRotation) ||
            !Float(Shape, TEXT("distance_factor"), Out.DistanceFactor) ||
            !Float(Shape, TEXT("thickness"), Out.Thickness) ||
            !Float(Shape, TEXT("horizontal_radius_factor"), Out.HorizontalRadiusFactor))
            return false;
        if (!Number(Shape, TEXT("width_smoothness"), Smoothness) || Smoothness < 1 ||
            !Number(Shape, TEXT("vertical_radius_default_factor"), Default) ||
            !Number(Shape, TEXT("vertical_radius_center_factor"), Center) || !Number(Shape, TEXT("y_scale"), Scale))
            return Error(TEXT("shape: missing numbers"));
        Out.WidthSmoothness = int32(Smoothness);
        Out.VerticalRadiusDefault = float(Default);
        Out.VerticalRadiusCenter = float(Center);
        Out.YScale = float(Scale);
        return true;
    }
    return Error(*FString::Printf(TEXT("carver type %s is not built"), *FString(Type)));
}

TConstArrayView<int32> FVoxelCarvePath::GetSteps(FIntPoint Column) const
{
    if (Column.X < Min.X || Column.X > Max.X || Column.Y < Min.Y || Column.Y > Max.Y) return {};
    const int32 Cell = (Column.Y - Min.Y) * (Max.X - Min.X + 1) + Column.X - Min.X;
    return TConstArrayView<int32>(StepIndex.GetData() + ColumnFirst[Cell], ColumnFirst[Cell + 1] - ColumnFirst[Cell]);
}

int64 FVoxelCarvePath::GetAllocatedSize() const
{
    return sizeof(*this) + Steps.GetAllocatedSize() + Widths.GetAllocatedSize() + ColumnFirst.GetAllocatedSize() +
        StepIndex.GetAllocatedSize();
}

FVoxelCarvePathCache::FVoxelCarvePathCache(int32 Slots)
    : SlotCount(int32(FMath::RoundUpToPowerOfTwo(uint32(FMath::Max(1, Slots))))), Table(MakeUnique<FSlot[]>(SlotCount))
{
}

int32 FVoxelCarvePathCache::GetSlot(int32 Carver, FIntPoint Start) const
{
    return int32(VoxelRandom::Mix64((uint64(uint32(Start.X)) << 32 | uint32(Start.Y)) ^ (uint64(Carver) << 61)) &
        (SlotCount - 1));
}

TSharedPtr<const FVoxelCarvePath, ESPMode::ThreadSafe> FVoxelCarvePathCache::Find(int32 Carver, FIntPoint Start) const
{
    FSlot& Slot = Table[GetSlot(Carver, Start)];
    UE::TUniqueLock Guard(Slot.Lock);
    if (Slot.Carver == Carver && Slot.Start == Start) return Slot.Path;
    return nullptr;
}

void FVoxelCarvePathCache::Store(
    int32 Carver, FIntPoint Start, TSharedPtr<const FVoxelCarvePath, ESPMode::ThreadSafe> Path) const
{
    FSlot& Slot = Table[GetSlot(Carver, Start)];
    TSharedPtr<const FVoxelCarvePath, ESPMode::ThreadSafe> Old;
    {
        UE::TUniqueLock Guard(Slot.Lock);
        Old = MoveTemp(Slot.Path);
        Slot.Carver = Carver;
        Slot.Start = Start;
        Slot.Path = MoveTemp(Path);
    }
    // Old is freed here, outside the lock.
}

void FVoxelCarvePathCache::Reset() const
{
    for (int32 Index = 0; Index < SlotCount; ++Index)
    {
        TSharedPtr<const FVoxelCarvePath, ESPMode::ThreadSafe> Old;
        UE::TUniqueLock Guard(Table[Index].Lock);
        Old = MoveTemp(Table[Index].Path);
        Table[Index].Carver = INDEX_NONE;
    }
}

int64 FVoxelCarvePathCache::GetAllocatedSize() const
{
    int64 Bytes = int64(SlotCount) * sizeof(FSlot);
    for (int32 Index = 0; Index < SlotCount; ++Index)
    {
        UE::TUniqueLock Guard(Table[Index].Lock);
        if (Table[Index].Path) Bytes += Table[Index].Path->GetAllocatedSize();
    }
    return Bytes;
}

bool FVoxelCarvers::Initialize(const FVoxelFeatureGraph& Graph, uint64 InSeed, FString* OutError)
{
    Configs.Reset();
    if (Graph.Biomes.IsEmpty()) return CarverFail(OutError, TEXT("carvers: no biomes"));
    // The per-biome rule is not built: every biome must list the same carvers, in the same order.
    const TArray<int32>& First = Graph.Biomes[0].Carvers;
    for (const FVoxelBiomeDecoration& Biome : Graph.Biomes)
        if (Biome.Carvers != First)
            return CarverFail(OutError,
                FString::Printf(TEXT("carvers: %s lists other carvers than %s (the per-biome rule is not built)"),
                    *Biome.Biome, *Graph.Biomes[0].Biome));
    TArray<FVoxelCarverConfig> Compiled;
    for (const int32 Entry : First)
        if (!FVoxelCarverConfig::Compile(Graph, Entry, Compiled.AddDefaulted_GetRef(), OutError)) return false;
    return Initialize(Compiled, InSeed, OutError);
}

bool FVoxelCarvers::Initialize(TConstArrayView<FVoxelCarverConfig> InConfigs, uint64 InSeed, FString* OutError)
{
    Configs.Reset();
    StartSalts.Reset();
    PathSalts.Reset();
    if (InConfigs.IsEmpty()) return CarverFail(OutError, TEXT("carvers: none listed"));
    Seed = InSeed;
    for (const FVoxelCarverConfig& Config : InConfigs)
    {
        const FTCHARToUTF8 Start(*(TEXT("carver/") + Config.Id)), Path(*(TEXT("carver/") + Config.Id + TEXT("/path")));
        StartSalts.Add(VoxelRandom::Fnv1a64(reinterpret_cast<const uint8*>(Start.Get()), Start.Length()));
        PathSalts.Add(VoxelRandom::Fnv1a64(reinterpret_cast<const uint8*>(Path.Get()), Path.Length()));
    }
    Configs = InConfigs;
    return true;
}

bool FVoxelCarvers::IsStart(int32 Carver, FIntPoint Start) const
{
    return VoxelRandom::ToUnitDouble(VoxelRandom::Hash64(Seed, StartSalts[Carver], Start.X, Start.Y)) <
        Configs[Carver].Probability;
}

void FVoxelCarvers::BuildCave(
    const FVoxelCarverConfig& Config, FVoxelXoroshiro& Random, FIntPoint Start, TArray<FVoxelCarveStep>& Out) const
{
    TArray<FCarverTunnel, TInlineAllocator<16>> Pending;
    const int32 Systems = Config.Count.Sample(Random);
    for (int32 System = 0; System < Systems; ++System)
    {
        const double X = CarverColumn * Random.NextDouble(), Y = CarverColumn * Random.NextDouble();
        const double Z = Config.Y.Sample(Random);
        const double Horizontal = Config.HorizontalRadius.Sample(Random);
        const double Vertical = Config.VerticalRadius.Sample(Random);
        const float Floor = Config.FloorLevel.Sample(Random);
        int32 Trunks = 1;
        if (Random.NextInt(CarverRoomOdds) == 0)
        {
            const double Radius = CarverRoomRadius + CarverRoomRadiusSpan * Random.NextDouble();
            FVoxelCarveStep& Room = Out.AddDefaulted_GetRef();
            Room.X = float(X);
            Room.Y = float(Y);
            Room.Z = float(Z);
            Room.Horizontal = float(Radius);
            Room.Vertical = float(Radius * Config.RoomVerticalRadius.Sample(Random));
            Room.Floor = Floor;
            Trunks += Random.NextInt(CarverRoomTrunks);
        }
        for (int32 Trunk = 0; Trunk < Trunks; ++Trunk)
        {
            FCarverTunnel& Tunnel = Pending.AddDefaulted_GetRef();
            Tunnel.X = X;
            Tunnel.Y = Y;
            Tunnel.Z = Z;
            Tunnel.Yaw = UE_DOUBLE_TWO_PI * Random.NextDouble();
            Tunnel.Pitch = (Random.NextDouble() - 0.5) * CarverPitchStart;
            Tunnel.Thickness = Config.Thickness.Sample(Random);
            if (Config.bWeirdThicknessBias && Random.NextInt(CarverWeirdOdds) == 0)
            {
                const double A = Random.NextDouble();
                Tunnel.Thickness *= A * Random.NextDouble() * CarverWeirdScale + 1;
            }
            Tunnel.VerticalScale = Config.StartVerticalRadius.Sample(Random);
            Tunnel.First = 0;
            Tunnel.Length = CarverTunnelLength - Random.NextInt(CarverTunnelLengthSpan);
            Tunnel.bMayBranch = true;
        }
        // Each tunnel walks with its own stream, so a branch does not shift its trunk's later steps.
        while (!Pending.IsEmpty())
        {
            FCarverTunnel Tunnel = Pending.Pop(EAllowShrinking::No);
            FVoxelXoroshiro Walk(Random.Next());
            const int32 BranchAt = Tunnel.bMayBranch && Tunnel.Thickness > CarverBranchMinThickness
                ? Tunnel.Length / 4 + Walk.NextInt(FMath::Max(1, Tunnel.Length / 2))
                : -1;
            const bool bSteep = Walk.NextInt(CarverSteepOdds) == 0;
            double YawRate = 0, PitchRate = 0;
            for (int32 Index = Tunnel.First; Index < Tunnel.Length; ++Index)
            {
                const double Radius =
                    CarverRadiusBase + FMath::Sin(UE_DOUBLE_PI * Index / Tunnel.Length) * Tunnel.Thickness;
                const double Cos = FMath::Cos(Tunnel.Pitch);
                Tunnel.X += FMath::Cos(Tunnel.Yaw) * Cos;
                Tunnel.Y += FMath::Sin(Tunnel.Yaw) * Cos;
                Tunnel.Z += FMath::Sin(Tunnel.Pitch);
                Tunnel.Pitch =
                    Tunnel.Pitch * (bSteep ? CarverPitchHoldSteep : CarverPitchHold) + PitchRate * CarverTurn;
                Tunnel.Yaw += YawRate * CarverTurn;
                PitchRate *= CarverPitchKeep;
                YawRate *= CarverYawKeep;
                const double PA = Walk.NextDouble(), PB = Walk.NextDouble(), PC = Walk.NextDouble();
                PitchRate += (PA - PB) * PC * CarverPitchKick;
                const double YA = Walk.NextDouble(), YB = Walk.NextDouble(), YC = Walk.NextDouble();
                YawRate += (YA - YB) * YC * CarverYawKick;
                if (Index == BranchAt)
                {
                    for (const double Side : {-UE_DOUBLE_HALF_PI, UE_DOUBLE_HALF_PI})
                    {
                        FCarverTunnel& Branch = Pending.AddDefaulted_GetRef();
                        Branch = Tunnel;
                        Branch.Yaw = Tunnel.Yaw + Side;
                        Branch.Pitch = Tunnel.Pitch / 3;
                        Branch.Thickness = CarverBranchThicknessBase + CarverBranchThickness * Walk.NextDouble();
                        Branch.VerticalScale = 1;
                        Branch.First = Index;
                        Branch.bMayBranch = false;
                    }
                    break;
                }
                if (Walk.NextInt(CarverSkipOdds) == 0) continue;
                FVoxelCarveStep& Step = Out.AddDefaulted_GetRef();
                Step.X = float(Tunnel.X);
                Step.Y = float(Tunnel.Y);
                Step.Z = float(Tunnel.Z);
                Step.Horizontal = float(Radius * Horizontal);
                Step.Vertical = float(Radius * Vertical * (Index == Tunnel.First ? Tunnel.VerticalScale : 1));
                Step.Floor = Floor;
            }
        }
    }
}

void FVoxelCarvers::BuildCanyon(const FVoxelCarverConfig& Config, FVoxelXoroshiro& Random, FIntPoint Start,
    TArray<FVoxelCarveStep>& Out, TArray<float>& Widths) const
{
    double X = CarverColumn * Random.NextDouble(), Y = CarverColumn * Random.NextDouble();
    double Z = Config.Y.Sample(Random);
    double Yaw = UE_DOUBLE_TWO_PI * Random.NextDouble();
    double Pitch = Config.VerticalRotation.Sample(Random);
    const double Thickness = Config.Thickness.Sample(Random);
    const double Factor = Config.HorizontalRadiusFactor.Sample(Random);
    const int32 Length = FMath::Max(1, FMath::RoundToInt32(CarverCanyonLength * Config.DistanceFactor.Sample(Random)));
    // The ledges: each height's width factor holds for a few heights, then changes (width_smoothness).
    Widths.SetNumUninitialized(VoxelConstants::Height);
    float Width = 1;
    for (int32 Level = 0; Level < VoxelConstants::Height; ++Level)
    {
        if (Level == 0 || Random.NextInt(Config.WidthSmoothness) == 0)
        {
            const double A = Random.NextDouble();
            Width = float(1 + A * A * CarverLedge);
        }
        Widths[Level] = Width;
    }
    double YawRate = 0, PitchRate = 0;
    for (int32 Index = 0; Index < Length; ++Index)
    {
        const double Middle = FMath::Sin(UE_DOUBLE_PI * Index / Length);
        const double Horizontal = (CarverCanyonRadiusBase + Middle * Thickness) * Factor;
        const double Vertical =
            Horizontal * Config.YScale * (Config.VerticalRadiusDefault + Config.VerticalRadiusCenter * Middle);
        const double Cos = FMath::Cos(Pitch);
        X += FMath::Cos(Yaw) * Cos;
        Y += FMath::Sin(Yaw) * Cos;
        Z += FMath::Sin(Pitch);
        Pitch = Pitch * CarverCanyonPitchHold + PitchRate * CarverCanyonTurn;
        Yaw += YawRate * CarverCanyonTurn;
        PitchRate *= CarverCanyonPitchKeep;
        YawRate *= CarverCanyonYawKeep;
        const double PA = Random.NextDouble(), PB = Random.NextDouble(), PC = Random.NextDouble();
        PitchRate += (PA - PB) * PC * CarverCanyonPitchKick;
        const double YA = Random.NextDouble(), YB = Random.NextDouble(), YC = Random.NextDouble();
        YawRate += (YA - YB) * YC * CarverCanyonYawKick;
        if (Random.NextInt(CarverCanyonSkipOdds) == 0) continue;
        FVoxelCarveStep& Step = Out.AddDefaulted_GetRef();
        Step.X = float(X);
        Step.Y = float(Y);
        Step.Z = float(Z);
        Step.Horizontal = float(Horizontal);
        Step.Vertical = float(Vertical);
    }
}

void FVoxelCarvers::BuildPath(int32 Carver, FIntPoint Start, FVoxelCarvePath& Out) const
{
    const FVoxelCarverConfig& Config = Configs[Carver];
    Out = FVoxelCarvePath();
    Out.Kind = Config.Kind;
    Out.Start = Start;
    FVoxelXoroshiro Random(VoxelRandom::Hash64(Seed, PathSalts[Carver], Start.X, Start.Y));
    TArray<FVoxelCarveStep> Steps;
    if (Config.Kind == EVoxelCarverKind::Cave)
        BuildCave(Config, Random, Start, Steps);
    else
        BuildCanyon(Config, Random, Start, Steps, Out.Widths);

    // Keep the steps that reach the world and stay within Reach of the start; then index them by column.
    TArray<FIntPoint> Boxes;
    Out.Min = FIntPoint(MAX_int32, MAX_int32);
    Out.Max = FIntPoint(MIN_int32, MIN_int32);
    for (const FVoxelCarveStep& Step : Steps)
    {
        if (Step.Z + Step.Vertical + 1 < VoxelConstants::MinZ + 1 || Step.Z - Step.Vertical > VoxelConstants::MaxZ)
            continue;
        FIntPoint Low, High;
        CarverStepColumns(Step, Start, Low, High);
        if (Low.X < Start.X - Reach || Low.Y < Start.Y - Reach || High.X > Start.X + Reach || High.Y > Start.Y + Reach)
        {
            ++Out.Dropped;
            continue;
        }
        Out.Steps.Add(Step);
        Boxes.Add(Low);
        Boxes.Add(High);
        Out.Min = FIntPoint(FMath::Min(Out.Min.X, Low.X), FMath::Min(Out.Min.Y, Low.Y));
        Out.Max = FIntPoint(FMath::Max(Out.Max.X, High.X), FMath::Max(Out.Max.Y, High.Y));
    }
    if (Out.Steps.IsEmpty())
    {
        Out.Min = FIntPoint::ZeroValue;
        Out.Max = FIntPoint(-1, -1);
        return;
    }
    const int32 Width = Out.Max.X - Out.Min.X + 1, Cells = Width * (Out.Max.Y - Out.Min.Y + 1);
    Out.ColumnFirst.SetNumZeroed(Cells + 1);
    for (int32 Step = 0; Step < Out.Steps.Num(); ++Step)
        for (int32 CY = Boxes[2 * Step].Y; CY <= Boxes[2 * Step + 1].Y; ++CY)
            for (int32 CX = Boxes[2 * Step].X; CX <= Boxes[2 * Step + 1].X; ++CX)
                ++Out.ColumnFirst[(CY - Out.Min.Y) * Width + CX - Out.Min.X + 1];
    for (int32 Cell = 0; Cell < Cells; ++Cell)
        Out.ColumnFirst[Cell + 1] += Out.ColumnFirst[Cell];
    Out.StepIndex.SetNumUninitialized(Out.ColumnFirst[Cells]);
    TArray<int32> Next(Out.ColumnFirst.GetData(), Cells);
    for (int32 Step = 0; Step < Out.Steps.Num(); ++Step)
        for (int32 CY = Boxes[2 * Step].Y; CY <= Boxes[2 * Step + 1].Y; ++CY)
            for (int32 CX = Boxes[2 * Step].X; CX <= Boxes[2 * Step + 1].X; ++CX)
                Out.StepIndex[Next[(CY - Out.Min.Y) * Width + CX - Out.Min.X]++] = Step;
}

void FVoxelCarvers::Voxelize(const FVoxelCarveStep& Step, EVoxelCarverKind Kind, TConstArrayView<float> Widths,
    FIntPoint Column, TArrayView<uint64> Mask, FIntPoint Start, bool bSkipTest)
{
    check(Mask.Num() == MaskWords);
    // The column's lowest corner, relative to the start's (exact: whole blocks within the reach).
    const double X0 = double(Column.X - Start.X) * CarverColumn, Y0 = double(Column.Y - Start.Y) * CarverColumn;
    const double CX = Step.X, CY = Step.Y, CZ = Step.Z, Horizontal = Step.Horizontal, Vertical = Step.Vertical;
    // A zero radius would divide 0 by 0 below, and NaN passes every "outside" test.
    if (!(Horizontal > 0 && Vertical > 0)) return;
    const int32 MinX = FMath::Max(0, FMath::FloorToInt32(CX - Horizontal - X0) - 1);
    const int32 MaxX = FMath::Min(CarverColumn - 1, FMath::FloorToInt32(CX + Horizontal - X0) + 1);
    const int32 MinY = FMath::Max(0, FMath::FloorToInt32(CY - Horizontal - Y0) - 1);
    const int32 MaxY = FMath::Min(CarverColumn - 1, FMath::FloorToInt32(CY + Horizontal - Y0) + 1);
    const int32 MinZ = FMath::Max(VoxelConstants::MinZ + 1, FMath::FloorToInt32(CZ - Vertical));
    const int32 MaxZ = FMath::Min(VoxelConstants::MaxZ, FMath::FloorToInt32(CZ + Vertical) + 1);
    if (MinX > MaxX || MinY > MaxY || MinZ > MaxZ) return;
    const bool bCanyon = Kind == EVoxelCarverKind::Canyon && bSkipTest;
    check(!bCanyon || Widths.Num() == VoxelConstants::Height);
    const double Floor = Step.Floor;
    uint64* const Bits = Mask.GetData();
    for (int32 LocalY = MinY; LocalY <= MaxY; ++LocalY)
    {
        const double DY = (Y0 + LocalY + 0.5 - CY) / Horizontal;
        for (int32 LocalX = MinX; LocalX <= MaxX; ++LocalX)
        {
            const double DX = (X0 + LocalX + 0.5 - CX) / Horizontal;
            const double Across = DX * DX + DY * DY;
            if (Across >= 1) continue;
            for (int32 Z = MinZ; Z <= MaxZ; ++Z)
            {
                if (bSkipTest)
                {
                    const double DZ = (Z - 0.5 - CZ) / Vertical;
                    if (bCanyon ? Across * Widths[Z - VoxelConstants::MinZ] + DZ * DZ >= 1
                                : DZ <= Floor || Across + DZ * DZ >= 1)
                        continue;
                }
                const int32 Index = FVoxelColumn::GetDenseIndex(LocalX, LocalY, Z);
                Bits[Index >> 6] |= uint64(1) << (Index & 63);
            }
        }
    }
}

void FVoxelCarvers::Mark(FIntPoint Column, TArrayView<uint64> Mask, const FVoxelCarvePathCache* Cache,
    FVoxelCarveStats* Stats, int32 InReach) const
{
    check(Mask.Num() == MaskWords);
    FMemory::Memzero(Mask.GetData(), MaskWords * sizeof(uint64));
    FVoxelCarveStats Local;
    // The starts first (one timer for all of them), then each start's path and cells.
    const double Begin = Stats ? FPlatformTime::Seconds() : 0;
    TArray<FIntVector, TInlineAllocator<256>> Starts;
    for (int32 Carver = 0; Carver < Configs.Num(); ++Carver)
        for (int32 SY = Column.Y - InReach; SY <= Column.Y + InReach; ++SY)
            for (int32 SX = Column.X - InReach; SX <= Column.X + InReach; ++SX)
                if (IsStart(Carver, FIntPoint(SX, SY))) Starts.Emplace(SX, SY, Carver);
    Local.Starts = Starts.Num();
    double Previous = Stats ? FPlatformTime::Seconds() : 0;
    Local.StartsMs = Previous - Begin;
    FVoxelCarvePath Built;
    for (const FIntVector& Entry : Starts)
    {
        const FIntPoint Start(Entry.X, Entry.Y);
        TSharedPtr<const FVoxelCarvePath, ESPMode::ThreadSafe> Shared = Cache ? Cache->Find(Entry.Z, Start) : nullptr;
        const FVoxelCarvePath* Path = Shared.Get();
        if (Path)
            ++Local.PathsFound;
        else if (Cache)
        {
            const TSharedRef<FVoxelCarvePath, ESPMode::ThreadSafe> New =
                MakeShared<FVoxelCarvePath, ESPMode::ThreadSafe>();
            BuildPath(Entry.Z, Start, *New);
            Shared = New;
            Cache->Store(Entry.Z, Start, Shared);
            Path = Shared.Get();
            ++Local.PathsBuilt;
        }
        else
        {
            BuildPath(Entry.Z, Start, Built);
            Path = &Built;
            ++Local.PathsBuilt;
        }
        double Now = Stats ? FPlatformTime::Seconds() : 0;
        Local.PathsMs += Now - Previous;
        Previous = Now;
        const TConstArrayView<int32> Steps = Path->GetSteps(Column);
        for (const int32 Step : Steps)
            Voxelize(Path->Steps[Step], Path->Kind, Path->Widths, Column, Mask, Start);
        Local.Steps += Steps.Num();
        Now = Stats ? FPlatformTime::Seconds() : 0;
        Local.MarkMs += Now - Previous;
        Previous = Now;
    }
    if (Stats)
    {
        for (int32 Word = 0; Word < MaskWords; ++Word)
            Local.Marked += FMath::CountBits64(Mask[Word]);
        Local.StartsMs *= 1000;
        Local.PathsMs *= 1000;
        Local.MarkMs *= 1000;
        *Stats = Local;
    }
}
