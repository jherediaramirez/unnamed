#include "Decoration/VoxelNumberProviders.h"
#include "Decoration/VoxelFeatureGraph.h"
#include "VoxelConstants.h"

namespace
{
    bool ProviderFail(FString* OutError, const FString& Message)
    {
        if (OutError) *OutError = Message;
        return false;
    }

    bool GetInt(const FVoxelFeatureGraph& Graph, int32 Node, const TCHAR* Key, int32& Out, FString* OutError)
    {
        const FVoxelFeatureValue* Value = Graph.FindField(Node, Key);
        if (!Value || Value->Type != EVoxelFeatureValueType::Number)
            return ProviderFail(
                OutError, FString::Printf(TEXT("%s: no number %s"), *FString(Graph.GetType(Node)), Key));
        Out = int32(Value->Number);
        return true;
    }
}

FVoxelIntProvider FVoxelIntProvider::MakeConstant(int32 Value)
{
    FVoxelIntProvider Out;
    Out.Min = Out.Max = Value;
    return Out;
}

FVoxelIntProvider FVoxelIntProvider::MakeUniform(int32 InMin, int32 InMax)
{
    if (InMax <= InMin) return MakeConstant(InMin);
    FVoxelIntProvider Out;
    Out.Type = EType::Uniform;
    Out.Min = InMin;
    Out.Max = InMax;
    return Out;
}

FVoxelIntProvider FVoxelIntProvider::MakeTrapezoid(int32 InMin, int32 InMax, int32 Plateau)
{
    const int32 Range = InMax - InMin;
    if (Plateau >= Range) return MakeUniform(InMin, InMax);
    FVoxelIntProvider Out;
    Out.Type = EType::Trapezoid;
    Out.Min = InMin;
    Out.Max = InMax;
    Out.SpanA = (Range - Plateau) / 2;
    Out.SpanB = Range - Out.SpanA;
    return Out;
}

void FVoxelIntProvider::GetDistribution(TArray<double>& Out) const
{
    Out.SetNumZeroed(Max - Min + 1);
    if (Type == EType::Constant)
        Out[0] = 1;
    else if (Type == EType::BiasedToBottom)
        for (int32 Inner1 = 1; Inner1 <= Out.Num(); ++Inner1)
            for (int32 Value = 0; Value < Inner1; ++Value)
                Out[Value] += 1. / Out.Num() / Inner1;
    else if (Type == EType::WeightedList)
        for (int32 Entry = 0; Entry < Entries.Num(); ++Entry)
        {
            TArray<double> Part;
            Inner[Entry].GetDistribution(Part);
            for (int32 Index = 0; Index < Part.Num(); ++Index)
                Out[Inner[Entry].Min + Index - Min] += Part[Index] * Entries[Entry].Y / SpanA;
        }
    else if (Type == EType::Clamped)
    {
        TArray<double> Source;
        Inner[0].GetDistribution(Source);
        for (int32 Index = 0; Index < Source.Num(); ++Index)
            Out[FMath::Clamp(Inner[0].Min + Index, Min, Max) - Min] += Source[Index];
    }
    else if (Type == EType::Uniform)
        for (double& P : Out)
            P = 1. / Out.Num();
    else
        for (int32 A = 0; A <= SpanA; ++A)
            for (int32 B = 0; B <= SpanB; ++B)
                Out[A + B] += 1. / ((SpanA + 1.) * (SpanB + 1.));
}

bool VoxelNumberProviders::CompileInt(
    const FVoxelFeatureGraph& Graph, const FVoxelFeatureValue& Value, FVoxelIntProvider& Out, FString* OutError)
{
    if (Value.Type == EVoxelFeatureValueType::Number)
    {
        Out = FVoxelIntProvider::MakeConstant(int32(Value.Number));
        return true;
    }
    const int32 Node = Graph.GetNode(Value);
    if (Node == INDEX_NONE || Graph.Nodes[Node].Kind != EVoxelFeatureKind::IntProvider)
        return ProviderFail(OutError, TEXT("not an int provider"));
    const FStringView Type = Graph.GetType(Node);
    int32 Min = 0, Max = 0, Plateau = 0;
    if (Type == TEXT("minecraft:constant"))
    {
        if (!GetInt(Graph, Node, TEXT("value"), Min, OutError)) return false;
        Out = FVoxelIntProvider::MakeConstant(Min);
        return true;
    }
    if (Type == TEXT("minecraft:uniform"))
    {
        if (!GetInt(Graph, Node, TEXT("min_inclusive"), Min, OutError) ||
            !GetInt(Graph, Node, TEXT("max_inclusive"), Max, OutError))
            return false;
        Out = FVoxelIntProvider::MakeUniform(Min, Max);
        return true;
    }
    if (Type == TEXT("minecraft:trapezoid"))
    {
        if (!GetInt(Graph, Node, TEXT("min"), Min, OutError) || !GetInt(Graph, Node, TEXT("max"), Max, OutError) ||
            !GetInt(Graph, Node, TEXT("plateau"), Plateau, OutError))
            return false;
        Out = FVoxelIntProvider::MakeTrapezoid(Min, Max, Plateau);
        return true;
    }
    if (Type == TEXT("minecraft:biased_to_bottom"))
    {
        if (!GetInt(Graph, Node, TEXT("min_inclusive"), Min, OutError) ||
            !GetInt(Graph, Node, TEXT("max_inclusive"), Max, OutError))
            return false;
        Out = FVoxelIntProvider::MakeUniform(Min, Max);
        if (Max > Min) Out.Type = FVoxelIntProvider::EType::BiasedToBottom;
        return true;
    }
    if (Type == TEXT("minecraft:weighted_list"))
    {
        const FVoxelFeatureValue* Distribution = Graph.FindField(Node, TEXT("distribution"));
        if (!Distribution || Distribution->Type != EVoxelFeatureValueType::List || Distribution->Count == 0)
            return ProviderFail(OutError, TEXT("weighted_list without a distribution"));
        Out = FVoxelIntProvider();
        Out.Type = FVoxelIntProvider::EType::WeightedList;
        Out.Min = MAX_int32;
        Out.Max = MIN_int32;
        for (int32 Item = 0; Item < Distribution->Count; ++Item)
        {
            const int32 Entry = Graph.GetNode(Graph.Items[Distribution->Index + Item]);
            const FVoxelFeatureValue* Data = Graph.FindField(Entry, TEXT("data"));
            const FVoxelFeatureValue* Weight = Graph.FindField(Entry, TEXT("weight"));
            FVoxelIntProvider Number;
            if (!Data || !Weight) return ProviderFail(OutError, TEXT("weighted_list entry without data or weight"));
            if (!CompileInt(Graph, *Data, Number, OutError)) return false;
            Out.Entries.Add(FIntPoint(Number.Min, int32(Weight->Number)));
            Out.SpanA += int32(Weight->Number);
            Out.Min = FMath::Min(Out.Min, Number.Min);
            Out.Max = FMath::Max(Out.Max, Number.Max);
            Out.Inner.Add(MoveTemp(Number));
        }
        if (Out.SpanA <= 0) return ProviderFail(OutError, TEXT("weighted_list with no weight"));
        return true;
    }
    if (Type == TEXT("minecraft:clamped"))
    {
        const FVoxelFeatureValue* Source = Graph.FindField(Node, TEXT("source"));
        FVoxelIntProvider Inner;
        if (!Source || !CompileInt(Graph, *Source, Inner, OutError) ||
            !GetInt(Graph, Node, TEXT("min_inclusive"), Min, OutError) ||
            !GetInt(Graph, Node, TEXT("max_inclusive"), Max, OutError))
            return false;
        Out = FVoxelIntProvider();
        Out.Type = FVoxelIntProvider::EType::Clamped;
        // The values it can give: the source's range clamped.
        Out.Min = FMath::Clamp(Inner.Min, Min, FMath::Max(Min, Max));
        Out.Max = FMath::Clamp(Inner.Max, Min, FMath::Max(Min, Max));
        Out.Inner.Add(MoveTemp(Inner));
        return true;
    }
    return ProviderFail(OutError, FString::Printf(TEXT("int provider %s is not compiled yet"), *FString(Type)));
}

bool VoxelNumberProviders::ResolveAnchor(const FVoxelFeatureGraph& Graph, int32 Node, int32& OutZ, FString* OutError)
{
    if (Node == INDEX_NONE || Graph.Nodes[Node].Kind != EVoxelFeatureKind::VerticalAnchor)
        return ProviderFail(OutError, TEXT("not a vertical anchor"));
    int32 Offset = 0;
    if (Graph.FindField(Node, TEXT("absolute"))) return GetInt(Graph, Node, TEXT("absolute"), OutZ, OutError);
    if (Graph.FindField(Node, TEXT("above_bottom")))
    {
        if (!GetInt(Graph, Node, TEXT("above_bottom"), Offset, OutError)) return false;
        OutZ = VoxelConstants::MinZ + Offset;
        return true;
    }
    if (Graph.FindField(Node, TEXT("below_top")))
    {
        if (!GetInt(Graph, Node, TEXT("below_top"), Offset, OutError)) return false;
        OutZ = VoxelConstants::MaxZ - Offset;
        return true;
    }
    return ProviderFail(OutError, TEXT("vertical anchor without absolute, above_bottom or below_top"));
}

bool VoxelNumberProviders::CompileHeight(
    const FVoxelFeatureGraph& Graph, const FVoxelFeatureValue& Value, FVoxelIntProvider& Out, FString* OutError)
{
    const int32 Node = Graph.GetNode(Value);
    if (Node != INDEX_NONE && Graph.Nodes[Node].Kind == EVoxelFeatureKind::VerticalAnchor)
    {
        int32 Z = 0;
        if (!ResolveAnchor(Graph, Node, Z, OutError)) return false;
        Out = FVoxelIntProvider::MakeConstant(Z);
        return true;
    }
    if (Node == INDEX_NONE || Graph.Nodes[Node].Kind != EVoxelFeatureKind::HeightProvider)
        return ProviderFail(OutError, TEXT("not a height provider"));
    const FStringView Type = Graph.GetType(Node);
    const FVoxelFeatureValue* MinValue = Graph.FindField(Node, TEXT("min_inclusive"));
    const FVoxelFeatureValue* MaxValue = Graph.FindField(Node, TEXT("max_inclusive"));
    int32 Min = 0, Max = 0;
    if (!MinValue || !MaxValue || !ResolveAnchor(Graph, Graph.GetNode(*MinValue), Min, OutError) ||
        !ResolveAnchor(Graph, Graph.GetNode(*MaxValue), Max, OutError))
        return ProviderFail(OutError, FString::Printf(TEXT("height provider %s: bad anchors"), *FString(Type)));
    if (Type == TEXT("minecraft:uniform"))
    {
        Out = FVoxelIntProvider::MakeUniform(Min, Max);
        return true;
    }
    if (Type == TEXT("minecraft:trapezoid"))
    {
        const FVoxelFeatureValue* Plateau = Graph.FindField(Node, TEXT("plateau"));
        Out = FVoxelIntProvider::MakeTrapezoid(Min, Max, Plateau ? int32(Plateau->Number) : 0);
        return true;
    }
    return ProviderFail(OutError, FString::Printf(TEXT("height provider %s is not compiled yet"), *FString(Type)));
}
