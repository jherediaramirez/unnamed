#include "Decoration/VoxelFeatureOrder.h"

#include "Decoration/VoxelFeatureGraph.h"

bool FVoxelFeatureOrder::Build(const FVoxelFeatureGraph& Graph, FString* OutError)
{
    const TArray<FVoxelFeatureEntry>& Placed = Graph.Entries[int32(EVoxelFeatureRegistry::PlacedFeature)];
    Steps.Reset();
    Steps.SetNum(FVoxelFeatureGraph::StepCount);
    StepOf.Init(INDEX_NONE, Placed.Num());
    IndexInStep.Init(INDEX_NONE, Placed.Num());
    for (const FVoxelBiomeDecoration& Biome : Graph.Biomes)
    {
        bool bValid = Biome.Steps.Num() == FVoxelFeatureGraph::StepCount;
        for (const TArray<int32>& Step : Biome.Steps)
            for (const int32 Entry : Step)
                bValid &= Placed.IsValidIndex(Entry);
        if (!bValid)
        {
            if (OutError) *OutError = FString::Printf(TEXT("biome %s: bad step lists"), *Biome.Biome);
            return false;
        }
    }
    for (int32 Step = 0; Step < FVoxelFeatureGraph::StepCount; ++Step)
    {
        // Edges from each feature to the one after it in a biome's list; a feature listed in two steps is an error.
        TMap<int32, TSet<int32>> Next;
        TMap<int32, int32> Incoming;
        for (const FVoxelBiomeDecoration& Biome : Graph.Biomes)
        {
            const TArray<int32>& List = Biome.Steps[Step];
            for (int32 Index = 0; Index < List.Num(); ++Index)
            {
                const int32 Entry = List[Index];
                if (StepOf[Entry] != INDEX_NONE && StepOf[Entry] != Step)
                {
                    if (OutError)
                        *OutError = FString::Printf(TEXT("%s is listed in steps %s and %s"), *Placed[Entry].Id,
                            FVoxelFeatureGraph::GetStepName(StepOf[Entry]), FVoxelFeatureGraph::GetStepName(Step));
                    return false;
                }
                StepOf[Entry] = Step;
                Incoming.FindOrAdd(Entry);
                if (Index + 1 < List.Num() && !Next.FindOrAdd(Entry).Contains(List[Index + 1]))
                {
                    Next[Entry].Add(List[Index + 1]);
                    ++Incoming.FindOrAdd(List[Index + 1]);
                }
            }
        }
        // Kahn's algorithm; of the features with nothing left before them, the smallest id goes first.
        const auto ById = [&Placed](int32 A, int32 B)
        { return Placed[A].Id.Compare(Placed[B].Id, ESearchCase::CaseSensitive) < 0; };
        TArray<int32> Ready;
        for (const TPair<int32, int32>& Pair : Incoming)
            if (Pair.Value == 0) Ready.Add(Pair.Key);
        TArray<int32>& Order = Steps[Step];
        while (Ready.Num())
        {
            Ready.Sort(ById);
            const int32 Entry = Ready[0];
            Ready.RemoveAt(0);
            IndexInStep[Entry] = Order.Add(Entry);
            if (const TSet<int32>* After = Next.Find(Entry))
                for (const int32 Following : *After)
                    if (--Incoming[Following] == 0) Ready.Add(Following);
        }
        if (Order.Num() != Incoming.Num())
        {
            if (OutError)
            {
                TArray<FString> Left;
                for (const TPair<int32, int32>& Pair : Incoming)
                    if (IndexInStep[Pair.Key] == INDEX_NONE) Left.Add(Placed[Pair.Key].Id);
                Left.Sort();
                *OutError = FString::Printf(TEXT("the %s lists contradict each other; left: %s"),
                    FVoxelFeatureGraph::GetStepName(Step), *FString::Join(Left, TEXT(", ")));
            }
            return false;
        }
    }
    return true;
}

uint64 VoxelFeatureSeeds::GetFeatureSalt(FStringView Id)
{
    const auto Utf8 = StringCast<UTF8CHAR>(Id.GetData(), Id.Len());
    return VoxelRandom::Mix64(VoxelRandom::Fnv1a64(reinterpret_cast<const uint8*>(Utf8.Get()), Utf8.Length()));
}
