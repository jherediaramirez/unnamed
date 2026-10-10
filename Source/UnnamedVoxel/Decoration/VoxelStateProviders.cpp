#include "Decoration/VoxelStateProviders.h"
#include "Blocks/VoxelBlockTable.h"
#include "Decoration/VoxelBlockPredicates.h"
#include "Decoration/VoxelDecorationContext.h"
#include "Decoration/VoxelFeatureGraph.h"
#include "Decoration/VoxelFeatureOrder.h"
#include "Density/VoxelDensityGraph.h"

namespace
{
    bool StateProviderFail(FString* OutError, const FString& Message)
    {
        if (OutError) *OutError = Message;
        return false;
    }

    double ReadNumber(const FVoxelFeatureGraph& Graph, int32 Node, const TCHAR* Key, double Default = 0)
    {
        const FVoxelFeatureValue* Value = Graph.FindField(Node, Key);
        return Value && Value->Type == EVoxelFeatureValueType::Number ? Value->Number : Default;
    }

    // A noise value in [-1, 1) to one of Count indices, linearly (ours: the wiki leaves the mapping open).
    int32 MapToIndex(double Noise, int32 Count)
    {
        return FMath::Clamp(int32(FMath::FloorToDouble((Noise + 1) * 0.5 * Count)), 0, Count - 1);
    }
}

void FVoxelStateProviders::Initialize(
    const FVoxelBlockTable& InTable, const FVoxelBlockPredicates& InPredicates, uint64 InWorldSeed)
{
    *this = FVoxelStateProviders();
    Table = &InTable;
    Tests = &InPredicates;
    WorldSeed = InWorldSeed;
}

int32 FVoxelStateProviders::AddNoise(const FVoxelFeatureGraph& Graph, int32 Node, uint64 DataSeed, const FString& Name)
{
    FVoxelDensityNoise Description;
    Description.Name = Name;
    Description.BaseOctave = int32(ReadNumber(Graph, Node, TEXT("base_octave")));
    Description.BaseAmplitude = ReadNumber(Graph, Node, TEXT("base_amplitude"), 1);
    Description.AmplitudeModifiers = {1.0};
    return Noises.Add(FVoxelNormalNoise(VoxelFeatureSeeds::GetPatternSeed(WorldSeed, int64(DataSeed)), Description));
}

bool FVoxelStateProviders::Compile(const FVoxelFeatureGraph& Graph, const FVoxelFeatureValue& Value,
    FVoxelBlockTags& Tags, FVoxelBlockPredicates& Predicates, int32& OutProvider, FString& OutSkip, FString* OutError)
{
    using VoxelPredicateReading::ReadState;
    const int32 Node = Graph.GetNode(Value);
    const auto AddState = [&](const FVoxelFeatureValue& StateValue) -> bool
    {
        const int32 State = ReadState(Graph, StateValue, *Table);
        if (State == INDEX_NONE)
        {
            OutSkip = TEXT("a provider state the table lacks");
            return false;
        }
        States.Add(uint16(State));
        return true;
    };
    // A bare state.
    if (Value.Type == EVoxelFeatureValueType::String ||
        (Node != INDEX_NONE && Graph.Nodes[Node].Kind == EVoxelFeatureKind::BlockState))
    {
        const int32 State = ReadState(Graph, Value, *Table);
        if (State == INDEX_NONE)
        {
            OutSkip = TEXT("a provider state the table lacks");
            return true;
        }
        FNode Out;
        Out.State = uint16(State);
        OutProvider = Nodes.Add(Out);
        return true;
    }
    if (Node == INDEX_NONE || Graph.Nodes[Node].Kind != EVoxelFeatureKind::StateProvider)
        return StateProviderFail(OutError, TEXT("not a block state provider"));
    const FString Name = Value.Type == EVoxelFeatureValueType::Ref
        ? Graph.Entries[int32(EVoxelFeatureRegistry::Provider)][Value.Index].Id
        : FString(TEXT("inline provider"));
    const FStringView Type = Graph.GetType(Node);
    const FVoxelFeatureValue* StateList = Graph.FindField(Node, TEXT("states"));
    const auto AddList = [&](const FVoxelFeatureValue* List, int32& OutFirst, int32& OutCount) -> bool
    {
        OutFirst = States.Num();
        OutCount = 0;
        for (int32 Item = 0; List && List->Type == EVoxelFeatureValueType::List && Item < List->Count;
            ++Item, ++OutCount)
            if (!AddState(Graph.Items[List->Index + Item])) return false;
        if (OutCount == 0) OutSkip = TEXT("a provider with no states");
        return OutCount > 0;
    };
    FNode Out;
    if (Type == TEXT("minecraft:weighted"))
    {
        Out.Op = EOp::Weighted;
        const FVoxelFeatureValue* Entries = Graph.FindField(Node, TEXT("entries"));
        Out.First = States.Num();
        for (int32 Item = 0; Entries && Entries->Type == EVoxelFeatureValueType::List && Item < Entries->Count; ++Item)
        {
            const int32 Entry = Graph.GetNode(Graph.Items[Entries->Index + Item]);
            const FVoxelFeatureValue* Data = Graph.FindField(Entry, TEXT("data"));
            const int32 Weight = int32(ReadNumber(Graph, Entry, TEXT("weight")));
            if (!Data || Weight <= 0) return StateProviderFail(OutError, TEXT("weighted entry without data or weight"));
            if (!AddState(*Data)) return true;
            Weights.SetNumZeroed(States.Num());
            Weights.Last() = Weight;
            Out.Total += Weight;
            ++Out.Count;
        }
        if (Out.Count == 0) return StateProviderFail(OutError, TEXT("weighted provider without entries"));
    }
    else if (Type == TEXT("minecraft:rule_based"))
    {
        Out.Op = EOp::RuleBased;
        const FVoxelFeatureValue* Fallback = Graph.FindField(Node, TEXT("fallback"));
        const FVoxelFeatureValue* RuleList = Graph.FindField(Node, TEXT("rules"));
        TArray<FIntPoint> Compiled;
        for (int32 Item = 0; RuleList && RuleList->Type == EVoxelFeatureValueType::List && Item < RuleList->Count;
            ++Item)
        {
            const int32 Rule = Graph.GetNode(Graph.Items[RuleList->Index + Item]);
            const FVoxelFeatureValue* If = Graph.FindField(Rule, TEXT("if_true"));
            const FVoxelFeatureValue* Then = Graph.FindField(Rule, TEXT("then"));
            FIntPoint Pair(INDEX_NONE, INDEX_NONE);
            if (!If || !Then) return StateProviderFail(OutError, TEXT("rule_based rule without if_true or then"));
            if (!Predicates.Compile(Graph, Graph.GetNode(*If), Tags, Pair.X, OutSkip, OutError)) return false;
            if (!OutSkip.IsEmpty() || !Compile(Graph, *Then, Tags, Predicates, Pair.Y, OutSkip, OutError))
                return OutSkip.IsEmpty() ? false : true;
            if (!OutSkip.IsEmpty()) return true;
            Compiled.Add(Pair);
        }
        // The fallback is optional (the trees' soil_beneath_tree has none): without it, no rule leaves the cell as it
        // is.
        int32 FallbackProvider = INDEX_NONE;
        if (Fallback && !Compile(Graph, *Fallback, Tags, Predicates, FallbackProvider, OutSkip, OutError)) return false;
        if (!OutSkip.IsEmpty()) return true;
        Out.Fallback = FallbackProvider;
        Out.First = Rules.Num();
        Out.Count = Compiled.Num();
        Rules.Append(Compiled);
    }
    else if (Type == TEXT("minecraft:randomized_int"))
    {
        Out.Op = EOp::RandomizedInt;
        const FVoxelFeatureValue* Source = Graph.FindField(Node, TEXT("source"));
        const FVoxelFeatureValue* Property = Graph.FindField(Node, TEXT("property"));
        const FVoxelFeatureValue* ValuesField = Graph.FindField(Node, TEXT("values"));
        if (!Source || !Property || Property->Type != EVoxelFeatureValueType::String || !ValuesField)
            return StateProviderFail(OutError, TEXT("randomized_int without source, property or values"));
        // An int provider type not built yet skips the placed feature, as everywhere else.
        FString IntError;
        if (!VoxelNumberProviders::CompileInt(Graph, *ValuesField, Out.Values, &IntError))
        {
            OutSkip = IntError;
            return true;
        }
        // Only a bare source state is built (kelp and sea pickles); each value's state is found once here.
        const int32 Base = ReadState(Graph, *Source, *Table);
        if (Base == INDEX_NONE)
        {
            OutSkip = TEXT("randomized_int over a source that is not a bare state");
            return true;
        }
        Out.First = States.Num();
        Out.Count = Out.Values.Max - Out.Values.Min + 1;
        for (int32 Number = Out.Values.Min; Number <= Out.Values.Max; ++Number)
        {
            const int32 State =
                Table->WithValue(uint16(Base), FName(Graph.Strings[Property->Index]), FName(*FString::FromInt(Number)));
            if (State == INDEX_NONE)
            {
                OutSkip = TEXT("randomized_int value the block lacks");
                return true;
            }
            States.Add(uint16(State));
        }
    }
    else if (Type == TEXT("minecraft:noise") || Type == TEXT("minecraft:noise_threshold") ||
        Type == TEXT("minecraft:dual_noise"))
    {
        const uint64 DataSeed = uint64(int64(ReadNumber(Graph, Node, TEXT("seed"))));
        const FVoxelFeatureValue* NoiseField = Graph.FindField(Node, TEXT("noise"));
        if (!NoiseField) return StateProviderFail(OutError, TEXT("noise provider without noise"));
        Out.Noise = AddNoise(Graph, Graph.GetNode(*NoiseField), DataSeed, Name + TEXT("/noise"));
        Out.Scale = ReadNumber(Graph, Node, TEXT("scale"), 1);
        if (Type == TEXT("minecraft:noise_threshold"))
        {
            Out.Op = EOp::NoiseThreshold;
            const FVoxelFeatureValue* Default = Graph.FindField(Node, TEXT("default_state"));
            const int32 State = Default ? ReadState(Graph, *Default, *Table) : INDEX_NONE;
            if (State == INDEX_NONE)
            {
                OutSkip = TEXT("a provider state the table lacks");
                return true;
            }
            Out.State = uint16(State);
            Out.Threshold = ReadNumber(Graph, Node, TEXT("threshold"));
            Out.Chance = float(ReadNumber(Graph, Node, TEXT("high_chance")));
            if (!AddList(Graph.FindField(Node, TEXT("low_states")), Out.First, Out.Count) ||
                !AddList(Graph.FindField(Node, TEXT("high_states")), Out.Second, Out.SecondCount))
                return true;
        }
        else
        {
            Out.Op = Type == TEXT("minecraft:noise") ? EOp::Noise : EOp::DualNoise;
            if (!AddList(StateList, Out.First, Out.Count)) return true;
            if (Out.Op == EOp::DualNoise)
            {
                const FVoxelFeatureValue* Slow = Graph.FindField(Node, TEXT("slow_noise"));
                const FVoxelFeatureValue* Variety = Graph.FindField(Node, TEXT("variety"));
                if (!Slow || !Variety || Variety->Type != EVoxelFeatureValueType::List || Variety->Count != 2)
                    return StateProviderFail(OutError, TEXT("dual_noise without slow_noise or variety"));
                Out.SlowNoise = AddNoise(Graph, Graph.GetNode(*Slow), DataSeed, Name + TEXT("/slow_noise"));
                Out.SlowScale = ReadNumber(Graph, Node, TEXT("slow_scale"), 1);
                const int32 A = FMath::Clamp(int32(Graph.Items[Variety->Index].Number), 1, Out.Count);
                const int32 B = FMath::Clamp(int32(Graph.Items[Variety->Index + 1].Number), 1, Out.Count);
                Out.Values = FVoxelIntProvider::MakeUniform(FMath::Min(A, B), FMath::Max(A, B));
            }
        }
    }
    else
    {
        OutSkip = FString::Printf(TEXT("provider %s"), *FString(Type));
        return true;
    }
    if (!OutSkip.IsEmpty()) return true;
    OutProvider = Nodes.Add(Out);
    return true;
}

double FVoxelStateProviders::GetNoise(int32 Provider, FIntVector Position) const
{
    const FNode& Node = Nodes[Provider];
    if (Node.Noise == INDEX_NONE) return 0;
    // The reference's y is the noise's vertical axis.
    return Noises[Node.Noise].Sample(Position.X * Node.Scale, Position.Z * Node.Scale, Position.Y * Node.Scale);
}

uint16 FVoxelStateProviders::SampleThreshold(int32 Provider, double NoiseValue, FVoxelXoroshiro& Random) const
{
    const FNode& Node = Nodes[Provider];
    if (NoiseValue < Node.Threshold) return States[Node.First + Random.NextInt(Node.Count)];
    if (Random.NextFloat() < Node.Chance) return States[Node.Second + Random.NextInt(Node.SecondCount)];
    return Node.State;
}

uint16 FVoxelStateProviders::Sample(
    int32 Provider, FVoxelXoroshiro& Random, FIntVector Position, const FVoxelDecorationContext& Context) const
{
    const FNode& Node = Nodes[Provider];
    switch (Node.Op)
    {
    case EOp::Simple:
        return Node.State;
    case EOp::Weighted:
    {
        int32 Pick = Random.NextInt(Node.Total);
        for (int32 Entry = Node.First; Entry < Node.First + Node.Count; ++Entry)
            if ((Pick -= Weights[Entry]) < 0) return States[Entry];
        return States[Node.First + Node.Count - 1];
    }
    case EOp::RuleBased:
        for (int32 Rule = Node.First; Rule < Node.First + Node.Count; ++Rule)
            if (Tests->Test(Rules[Rule].X, Context, Position)) return Sample(Rules[Rule].Y, Random, Position, Context);
        return Node.Fallback == INDEX_NONE ? Context.GetState(Position.X, Position.Y, Position.Z)
                                           : Sample(Node.Fallback, Random, Position, Context);
    case EOp::RandomizedInt:
        return States[Node.First + Node.Values.Sample(Random) - Node.Values.Min];
    case EOp::Noise:
        return States[Node.First + MapToIndex(GetNoise(Provider, Position), Node.Count)];
    case EOp::NoiseThreshold:
        return SampleThreshold(Provider, GetNoise(Provider, Position), Random);
    case EOp::DualNoise:
    {
        // Ours: the slow noise sets how many states the spot offers (the variety) and from where in the list they
        // run (a second read of it, far off); the fast noise picks among those.
        const FVoxelNormalNoise& Slow = Noises[Node.SlowNoise];
        const double SX = Position.X * Node.SlowScale, SY = Position.Z * Node.SlowScale,
                     SZ = Position.Y * Node.SlowScale;
        const int32 Variety =
            Node.Values.Min + MapToIndex(Slow.Sample(SX, SY, SZ), Node.Values.Max - Node.Values.Min + 1);
        const int32 Start = MapToIndex(Slow.Sample(SX + 4096.5, SY, SZ - 4096.5), Node.Count);
        const int32 Pick = MapToIndex(GetNoise(Provider, Position), Variety);
        return States[Node.First + (Start + Pick) % Node.Count];
    }
    }
    return 0;
}

void FVoxelStateProviders::GetStates(int32 Provider, TArray<uint16>& Out) const
{
    const FNode& Node = Nodes[Provider];
    switch (Node.Op)
    {
    case EOp::Simple:
        Out.AddUnique(Node.State);
        return;
    case EOp::RuleBased:
        for (int32 Rule = Node.First; Rule < Node.First + Node.Count; ++Rule)
            GetStates(Rules[Rule].Y, Out);
        if (Node.Fallback != INDEX_NONE) GetStates(Node.Fallback, Out);
        return;
    case EOp::NoiseThreshold:
        Out.AddUnique(Node.State);
        for (int32 Index = 0; Index < Node.SecondCount; ++Index)
            Out.AddUnique(States[Node.Second + Index]);
        break;
    default:
        break;
    }
    for (int32 Index = 0; Index < Node.Count; ++Index)
        Out.AddUnique(States[Node.First + Index]);
}
