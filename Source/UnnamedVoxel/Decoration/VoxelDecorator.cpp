#include "Decoration/VoxelDecorator.h"
#include "Biome/VoxelBiomeTable.h"
#include "Blocks/VoxelBlockTable.h"
#include "Decoration/VoxelDecorationContext.h"
#include "Decoration/VoxelFeatureGraph.h"
#include "Decoration/VoxelFeatureOrder.h"
#include "Storage/VoxelColumn.h"
#include "Terrain/VoxelTerrain.h"
#include "VoxelConstants.h"

namespace
{
    // Each try of a count modifier takes its own stream from (the stream it came from, its index), so a skipped or
    // failed try never shifts the ones after it (scout E's early-out; Factorio 2.0.34's lesson).
    constexpr uint64 TrySalt = VoxelRandom::Salt("decoration_try");

    bool DecoratorFail(FString* OutError, const FString& Message)
    {
        if (OutError) *OutError = Message;
        return false;
    }

    // What the heightmaps count about a state (FVoxelColumn's rules): a write that changes none of it leaves them.
    uint8 GetHeightClass(const FVoxelBlockTable& Table, uint16 State)
    {
        const FVoxelStateInfo& Info = Table.Get(State);
        return uint8(State != 0) | uint8(Info.bSolid) << 1 | uint8(Info.IsFluid()) << 2 | uint8(Info.bLeaves) << 3;
    }

    int32 FloorColumn(int32 Block)
    {
        return Block >> 4;
    }
}

FVoxelDecorator::FVoxelDecorator() = default;
FVoxelDecorator::~FVoxelDecorator() = default;

bool FVoxelDecorator::Initialize(const FVoxelFeatureGraph& Graph, const FVoxelBlockTable& InTable,
    const FVoxelBiomeTable& Biomes, uint64 InWorldSeed, const FVoxelDecorationOptions& InOptions, FString* OutError)
{
    *this = FVoxelDecorator();
    FVoxelFeatureOrder Order;
    if (!Order.Build(Graph, OutError)) return false;
    FVoxelBlockTags Tags(Graph, InTable);
    Table = &InTable;
    WorldSeed = InWorldSeed;
    Options = InOptions;

    TMap<int32, int32> FeatureOfEntry;
    for (int32 Step = 0; Step < Order.Steps.Num(); ++Step)
        for (int32 Index = 0; Index < Order.Steps[Step].Num(); ++Index)
        {
            const int32 Entry = Order.Steps[Step][Index];
            if (Index > 255)
            {
                Table = nullptr;
                return DecoratorFail(OutError,
                    FString::Printf(TEXT("step %d lists more than 256 placed features (the "
                                         "merge key holds 8 bits)"),
                        Step));
            }
            const FString& Id = Graph.Entries[int32(EVoxelFeatureRegistry::PlacedFeature)][Entry].Id;
            if (Options.Filter && !Options.Filter(Id)) continue;
            FFeature Feature;
            Feature.Id = Id;
            Feature.Step = Step;
            Feature.IndexInStep = Index;
            Feature.Key = FVoxelWriteBucket::MakeKey(Step, Index);
            Feature.Salt = VoxelFeatureSeeds::GetFeatureSalt(Id);
            FString Skip;
            if (!CompileFeature(Graph, Entry, Tags, Feature, Skip, OutError))
            {
                Table = nullptr;
                if (OutError) *OutError = FString::Printf(TEXT("placed feature %s: %s"), *Id, **OutError);
                return false;
            }
            if (!Skip.IsEmpty())
            {
                ++Skipped.FindOrAdd(Skip);
                continue;
            }
            FeatureOfEntry.Add(Entry, Features.Num());
            Features.Add(MoveTemp(Feature));
        }

    if (Options.Mutation == EVoxelDecorationMutation::SwappedOrder)
    {
        // The first two compiled features of one step trade places and keys.
        for (int32 Index = 0; Index + 1 < Features.Num(); ++Index)
            if (Features[Index].Step == Features[Index + 1].Step)
            {
                Swap(Features[Index].Key, Features[Index + 1].Key);
                Swap(Features[Index].IndexInStep, Features[Index + 1].IndexInStep);
                Features.Swap(Index, Index + 1);
                for (TPair<int32, int32>& Pair : FeatureOfEntry)
                    Pair.Value = Pair.Value == Index ? Index + 1 : Pair.Value == Index + 1 ? Index : Pair.Value;
                break;
            }
    }

    TMap<FString, int32> GraphBiome;
    for (int32 Biome = 0; Biome < Graph.Biomes.Num(); ++Biome)
        GraphBiome.Add(Graph.Biomes[Biome].Biome, Biome);
    FeaturesOfBiome.SetNum(Biomes.Biomes.Num());
    for (int32 Biome = 0; Biome < Biomes.Biomes.Num(); ++Biome)
    {
        FeaturesOfBiome[Biome].Init(false, Features.Num());
        const int32* Found = GraphBiome.Find(Biomes.Biomes[Biome].Name);
        if (!Found) continue;
        for (const TArray<int32>& Step : Graph.Biomes[*Found].Steps)
            for (const int32 Entry : Step)
                if (const int32* Feature = FeatureOfEntry.Find(Entry)) FeaturesOfBiome[Biome][*Feature] = true;
    }
    MissingBlocks = Tags.GetMissing();
    return true;
}

bool FVoxelDecorator::CompileFeature(const FVoxelFeatureGraph& Graph, int32 Entry, FVoxelBlockTags& Tags, FFeature& Out,
    FString& OutSkip, FString* OutError)
{
    const int32 Node = Graph.Entries[int32(EVoxelFeatureRegistry::PlacedFeature)][Entry].Node;
    const FVoxelFeatureValue* FeatureValue = Graph.FindField(Node, TEXT("feature"));
    const int32 FeatureNode = FeatureValue ? Graph.GetNode(*FeatureValue) : INDEX_NONE;
    if (FeatureNode == INDEX_NONE) return DecoratorFail(OutError, TEXT("no feature"));
    const FStringView Type = Graph.GetType(FeatureNode);
    if (Type != TEXT("minecraft:ore"))
    {
        OutSkip = FString(Type);
        return true;
    }

    // The placement: only the five modifiers the ores use are compiled so far.
    const FVoxelFeatureValue* Placement = Graph.FindField(Node, TEXT("placement"));
    for (int32 Item = 0; Placement && Item < Placement->Count; ++Item)
    {
        const int32 ModifierNode = Graph.GetNode(Graph.Items[Placement->Index + Item]);
        const FStringView ModifierType = Graph.GetType(ModifierNode);
        FModifier Modifier;
        if (ModifierType == TEXT("minecraft:count"))
        {
            const FVoxelFeatureValue* Count = Graph.FindField(ModifierNode, TEXT("count"));
            Modifier.Type = FModifier::EType::Count;
            if (!Count || !VoxelNumberProviders::CompileInt(Graph, *Count, Modifier.Provider, OutError)) return false;
        }
        else if (ModifierType == TEXT("minecraft:in_square"))
            Modifier.Type = FModifier::EType::InSquare;
        else if (ModifierType == TEXT("minecraft:height_range"))
        {
            const FVoxelFeatureValue* Height = Graph.FindField(ModifierNode, TEXT("height"));
            Modifier.Type = FModifier::EType::HeightRange;
            if (!Height || !VoxelNumberProviders::CompileHeight(Graph, *Height, Modifier.Provider, OutError))
                return false;
        }
        else if (ModifierType == TEXT("minecraft:rarity_filter"))
        {
            const FVoxelFeatureValue* Chance = Graph.FindField(ModifierNode, TEXT("chance"));
            Modifier.Type = FModifier::EType::RarityFilter;
            Modifier.Chance = Chance ? FMath::Max(1, int32(Chance->Number)) : 1;
        }
        else if (ModifierType == TEXT("minecraft:biome"))
            Modifier.Type = FModifier::EType::Biome;
        else
        {
            OutSkip = FString::Printf(TEXT("modifier %s"), *FString(ModifierType));
            return true;
        }
        Out.Modifiers.Add(Modifier);
    }
    Out.bPruneAtBiome = !Out.Modifiers.IsEmpty() && Out.Modifiers.Last().Type == FModifier::EType::Biome;

    FOre Ore;
    TArray<FVoxelOreTarget> Targets;
    if (!VoxelOreFeature::Compile(Graph, FeatureNode, Tags, *Table, Ore.Config, Targets, OutSkip, OutError))
        return false;
    if (!OutSkip.IsEmpty()) return true;
    Ore.Reach = VoxelOreFeature::GetReach(Ore.Config.Size);
    for (const FVoxelOreTarget& Target : Targets)
        Ore.bTargetsAir |= Target.Rule.CanMatch(0);
    Ore.Config.Targets = TargetLists.Add(MoveTemp(Targets));
    Out.Ore = Ores.Add(Ore);
    return true;
}

bool FVoxelDecorator::IsAboveEverything(
    const FOre& Ore, FIntPoint Column, FIntVector Position, const FVoxelDecorationContext& Context) const
{
    // A blob wholly above the highest block of every column it overlaps, or outside the world, replaces nothing.
    const int32 Low = Position.Z - Ore.Reach.Down;
    int32 Top = VoxelConstants::MinZ - 1;
    if (Position.Z + Ore.Reach.Up >= VoxelConstants::MinZ)
        for (int32 DY = FloorColumn(Position.Y - Ore.Reach.Horizontal) - Column.Y;
            DY <= FloorColumn(Position.Y + Ore.Reach.Horizontal) - Column.Y; ++DY)
            for (int32 DX = FloorColumn(Position.X - Ore.Reach.Horizontal) - Column.X;
                DX <= FloorColumn(Position.X + Ore.Reach.Horizontal) - Column.X; ++DX)
                if (FMath::Abs(DX) <= 1 && FMath::Abs(DY) <= 1)
                    Top = FMath::Max(Top, Context.GetTopZ(FVoxelWriteLog::GetBucket(DX, DY)));
    return Low > Top;
}

template <typename FPrune, typename FVisit>
void FVoxelDecorator::RunModifiers(const FFeature& Feature, int32 FeatureIndex, int32 Modifier, FIntVector Position,
    uint64 Seed, const FVoxelDecorationContext& Context, FPrune&& Prune, FVisit&& Visit) const
{
    if (Modifier == Feature.Modifiers.Num())
    {
        Visit(Position, Seed);
        return;
    }
    const FModifier& Step = Feature.Modifiers[Modifier];
    FVoxelXoroshiro Random(Seed);
    switch (Step.Type)
    {
    case FModifier::EType::Count:
    {
        const int32 Count = Step.Provider.Sample(Random);
        for (int32 Try = 0; Try < Count; ++Try)
            RunModifiers(Feature, FeatureIndex, Modifier + 1, Position, VoxelRandom::Hash64(Seed, TrySalt, Try, 0),
                Context, Prune, Visit);
        return;
    }
    case FModifier::EType::InSquare:
        Position.X += Random.NextInt(VoxelConstants::ColumnSize);
        Position.Y += Random.NextInt(VoxelConstants::ColumnSize);
        break;
    case FModifier::EType::HeightRange:
        Position.Z = Step.Provider.Sample(Random);
        break;
    case FModifier::EType::RarityFilter:
        if (Random.NextInt(Step.Chance) != 0) return;
        break;
    case FModifier::EType::Biome:
    {
        // The position's own biome must list the feature (the column's 3x3 only decided that it runs).
        if (Feature.bPruneAtBiome && Prune(Position)) return;
        const int32 Biome = Context.GetBiome(Position.X, Position.Y, Position.Z);
        if (!FeaturesOfBiome.IsValidIndex(Biome) || !FeaturesOfBiome[Biome][FeatureIndex]) return;
        RunModifiers(Feature, FeatureIndex, Modifier + 1, Position, Seed, Context, Prune, Visit);
        return;
    }
    }
    RunModifiers(Feature, FeatureIndex, Modifier + 1, Position, Random.Next(), Context, Prune, Visit);
}

void FVoxelDecorator::Decorate(FIntPoint Column, TConstArrayView<const FVoxelColumn*> Snapshots, FVoxelWriteLog& Out,
    FVoxelDecorateStats* Stats) const
{
    check(IsInitialized() && Snapshots.Num() == 9);
    FVoxelDecorationContext Context(Column, Snapshots);
    for (FVoxelWriteBucket& Bucket : Out.Buckets)
        Bucket = FVoxelWriteBucket();
    Out.Source = Column;
    Out.Dropped = 0;
    FVoxelDecorateStats Local;

    // A column runs the features any biome of its 3x3 lists; the biome modifier then checks each position's own.
    // Each column's own quarts (its storage also holds the next column's first row and line, left out here).
    uint64 Seen[4] = {};
    for (const FVoxelColumn* Snapshot : Snapshots)
    {
        const TConstArrayView<uint8> Quarts = Snapshot->GetBiomes().GetQuarts();
        if (Quarts.IsEmpty()) continue;
        constexpr int32 Width = FVoxelBiomeStorage::Width;
        for (int32 Layer = 0; Layer < FVoxelBiomeStorage::Layers; ++Layer)
            for (int32 Y = 0; Y < Width - 1; ++Y)
            {
                const uint8* Row = Quarts.GetData() + FVoxelBiomeStorage::GetIndex(0, Y, Layer);
                for (int32 X = 0; X < Width - 1; ++X)
                    Seen[Row[X] >> 6] |= uint64(1) << (Row[X] & 63);
            }
    }
    TBitArray<> Active(false, Features.Num());
    for (int32 Biome = 0; Biome < FMath::Min(FeaturesOfBiome.Num(), 256); ++Biome)
        if (Seen[Biome >> 6] >> (Biome & 63) & 1)
            Active.CombineWithBitwiseOR(FeaturesOfBiome[Biome], EBitwiseOperatorFlags::MaxSize);

    FVoxelXoroshiro Shared(VoxelRandom::Hash64(WorldSeed, VoxelFeatureSeeds::DecorationSalt, Column.X, Column.Y));
    TArray<FIntVector> Scratch;
    const FIntVector Start(
        Column.X * VoxelConstants::ColumnSize, Column.Y * VoxelConstants::ColumnSize, VoxelConstants::MinZ);
    for (int32 FeatureIndex = 0; FeatureIndex < Features.Num(); ++FeatureIndex)
    {
        if (!Active[FeatureIndex]) continue;
        const FFeature& Feature = Features[FeatureIndex];
        ++Local.Features;
        const uint64 Seed = Options.Mutation == EVoxelDecorationMutation::SharedStream
            ? Shared.Next()
            : VoxelFeatureSeeds::GetStreamSeed(WorldSeed, Feature.Salt, Column.X, Column.Y, Feature.Step);
        // Scout E's early-out, where the try's height is known: at the biome modifier, before its lookup, or at the
        // feature for placements without one.
        const FOre* PruneOre = Feature.Ore != INDEX_NONE && Options.bEarlyOut && !Ores[Feature.Ore].bTargetsAir
            ? &Ores[Feature.Ore]
            : nullptr;
        const auto Prune = [&](FIntVector Position)
        {
            if (!PruneOre) return false;
            ++Local.Tries;
            if (!IsAboveEverything(*PruneOre, Column, Position, Context)) return false;
            ++Local.Skipped;
            return true;
        };
        RunModifiers(Feature, FeatureIndex, 0, Start, Seed, Context, Prune,
            [&](FIntVector Position, uint64 TrySeed)
            {
                if (Feature.Ore == INDEX_NONE) return;
                const FOre& Ore = Ores[Feature.Ore];
                if (!Feature.bPruneAtBiome && Prune(Position)) return;
                if (!PruneOre) ++Local.Tries;
                FVoxelXoroshiro Random(TrySeed);
                Out.Dropped += VoxelOreFeature::Place(Ore.Config, TargetLists[Ore.Config.Targets], Position, Random,
                    Context, *Table, Options.bDiscard, Scratch,
                    [&](int32 X, int32 Y, int32 Z, uint16 State)
                    {
                        const int32 Bucket =
                            FVoxelWriteLog::GetBucket(FloorColumn(X) - Column.X, FloorColumn(Y) - Column.Y);
                        Out.Buckets[Bucket].Add(Feature.Key,
                            FVoxelWrite::Make(
                                X & 15, Y & 15, Z, State, EVoxelWriteMode::IfTargets, uint16(Ore.Config.Targets)));
                        ++Local.Placed;
                    });
            });
    }

    if (Stats)
    {
        Local.Writes = Out.GetWriteCount();
        Local.Dropped = Out.Dropped;
        Local.WorkingCells = Context.GetWrittenCells();
        Local.WorkingBytes = Context.GetAllocatedSize();
        Local.LogBytes = Out.GetAllocatedSize();
        *Stats = Local;
    }
}

void FVoxelDecorator::Finalize(const FVoxelColumn& Snapshot, TConstArrayView<const FVoxelWriteBucket*> Incoming,
    FVoxelColumn& Out, FVoxelFinalizeStats* Stats, TConstArrayView<int32> ArrivalOrder) const
{
    check(IsInitialized());
    Out = Snapshot;
    FVoxelFinalizeStats Local;
    // Sections a write reaches are expanded once, written in place, then packed again.
    TArray<uint16> Dense[VoxelConstants::SectionCount];
    TBitArray<> HeightCells(false, FVoxelColumn::Area);
    const auto Apply = [&](const FVoxelWrite& Write)
    {
        ++Local.Writes;
        const int32 Index = Write.GetDenseIndex();
        TArray<uint16>& States = Dense[Index >> 12];
        if (States.IsEmpty())
        {
            States.SetNumUninitialized(FVoxelSection::Volume);
            Out.GetSection(Index >> 12).Expand(States);
        }
        uint16& Cell = States.GetData()[Index & (FVoxelSection::Volume - 1)];
        uint16 State = Cell;
        switch (Write.GetMode())
        {
        case EVoxelWriteMode::Force:
            State = Write.State;
            break;
        case EVoxelWriteMode::IfTargets:
        {
            const int32 Block = Table->Get(Cell).Block;
            for (const FVoxelOreTarget& Target : TargetLists[Write.Argument])
                if (Target.Rule.Test(Block, Write.GetZ()))
                {
                    State = Target.State;
                    break;
                }
            break;
        }
        default:
            // IfReplaceable arrives with plants and trees (S8, S9).
            break;
        }
        if (State == Cell) return;
        ++Local.Applied;
        if (GetHeightClass(*Table, State) != GetHeightClass(*Table, Cell)) HeightCells[Index & 255] = true;
        Cell = State;
    };
    if (ArrivalOrder.IsEmpty())
        VoxelWriteMerge::Merge(Incoming, Apply);
    else
        for (const int32 Source : ArrivalOrder)
            if (Incoming.IsValidIndex(Source) && Incoming[Source])
                for (const FVoxelWrite& Write : Incoming[Source]->Writes)
                    Apply(Write);
    for (int32 Section = 0; Section < VoxelConstants::SectionCount; ++Section)
        if (!Dense[Section].IsEmpty())
        {
            Out.SetSectionStates(Section, Dense[Section]);
            ++Local.Sections;
        }
    for (TConstSetBitIterator<> Cell(HeightCells); Cell; ++Cell)
    {
        Out.UpdateHeights(Cell.GetIndex() & 15, Cell.GetIndex() >> 4, *Table);
        ++Local.HeightCells;
    }
    if (Stats) *Stats = Local;
}

void FVoxelDecorator::GenerateSnapshot(
    const IVoxelTerrain& Terrain, FIntPoint Column, FVoxelColumn& Out, TArray<uint16>& Dense) const
{
    if (Options.bFreezeLast)
        Terrain.GenerateSnapshot(Column, Out, Dense);
    else
        Terrain.Generate(Column, Out, Dense);
}

void FVoxelDecorator::FinishColumn(const IVoxelTerrain& Terrain, FVoxelColumn& Column) const
{
    if (Options.bFreezeLast) Terrain.FinishColumn(Column);
}

void FVoxelDecorator::DecorateAlone(const IVoxelTerrain& Terrain, FIntPoint Column, FVoxelColumn& Out) const
{
    TUniquePtr<FVoxelColumn> Snapshots[5][5];
    TArray<uint16> Dense;
    for (int32 DY = -2; DY <= 2; ++DY)
        for (int32 DX = -2; DX <= 2; ++DX)
        {
            Snapshots[DY + 2][DX + 2] = MakeUnique<FVoxelColumn>();
            GenerateSnapshot(Terrain, Column + FIntPoint(DX, DY), *Snapshots[DY + 2][DX + 2], Dense);
        }
    FVoxelWriteLog Logs[9];
    const FVoxelWriteBucket* Incoming[9] = {};
    for (int32 DY = -1; DY <= 1; ++DY)
        for (int32 DX = -1; DX <= 1; ++DX)
        {
            const FVoxelColumn* Around[9];
            for (int32 NY = -1; NY <= 1; ++NY)
                for (int32 NX = -1; NX <= 1; ++NX)
                    Around[FVoxelWriteLog::GetBucket(NX, NY)] = Snapshots[DY + NY + 2][DX + NX + 2].Get();
            FVoxelWriteLog& Log = Logs[FVoxelWriteLog::GetBucket(DX, DY)];
            Decorate(Column + FIntPoint(DX, DY), Around, Log);
            Incoming[FVoxelWriteLog::GetBucket(DX, DY)] = &Log.Buckets[FVoxelWriteLog::GetBucket(-DX, -DY)];
        }
    Finalize(*Snapshots[2][2], Incoming, Out);
    FinishColumn(Terrain, Out);
}

FString FVoxelDecorator::Describe() const
{
    TArray<int32> PerStep;
    PerStep.SetNumZeroed(FVoxelFeatureGraph::StepCount);
    for (const FFeature& Feature : Features)
        ++PerStep[Feature.Step];
    FString Steps;
    for (int32 Step = 0; Step < PerStep.Num(); ++Step)
        if (PerStep[Step])
            Steps += FString::Printf(TEXT("%s%s %d"), Steps.IsEmpty() ? TEXT("") : TEXT(", "),
                FVoxelFeatureGraph::GetStepName(Step), PerStep[Step]);
    int32 SkippedCount = 0;
    for (const TPair<FString, int32>& Pair : Skipped)
        SkippedCount += Pair.Value;
    FString Out = FString::Printf(TEXT("Decoration: %d placed features compiled (%d ores; %s), %d skipped (types not "
                                       "built yet)"),
        Features.Num(), Ores.Num(), *Steps, SkippedCount);
    if (!MissingBlocks.IsEmpty())
        Out += FString::Printf(
            TEXT("; %d block ids not in the table (ores: their targets left out)"), MissingBlocks.Num());
    return Out;
}

TArray<TPair<uint16, FString>> FVoxelDecorator::GetOreStates() const
{
    TArray<TPair<uint16, FString>> States;
    for (const TArray<FVoxelOreTarget>& Targets : TargetLists)
        for (const FVoxelOreTarget& Target : Targets)
            States.AddUnique({Target.State, Target.Id});
    return States;
}

TArray<FVoxelDecorator::FFeatureInfo> FVoxelDecorator::GetFeatureInfos() const
{
    TArray<FFeatureInfo> Infos;
    for (const FFeature& Feature : Features)
        Infos.Add({Feature.Id, Feature.Step, Feature.IndexInStep, Feature.Key});
    return Infos;
}
