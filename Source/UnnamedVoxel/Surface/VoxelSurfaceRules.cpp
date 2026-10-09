#include "Surface/VoxelSurfaceRules.h"

namespace
{
    bool SurfaceRulesFail(FString* OutError, const FString& Message)
    {
        if (OutError) *OutError = Message;
        return false;
    }

    int32 FindSurfaceName(const TArray<FVoxelSurfaceName>& Names, FStringView Name)
    {
        const FVoxelSurfaceName* Found = Names.FindByPredicate([Name](const FVoxelSurfaceName& Entry)
            { return FStringView(Entry.Name).Equals(Name, ESearchCase::CaseSensitive); });
        return Found ? Found->Index : INDEX_NONE;
    }

    // Names are unique (case-sensitively) and point at nodes that exist.
    bool CheckSurfaceNames(
        const TArray<FVoxelSurfaceName>& Names, int32 NodeCount, const TCHAR* What, FString* OutError)
    {
        for (int32 Index = 0; Index < Names.Num(); ++Index)
        {
            const FVoxelSurfaceName& Entry = Names[Index];
            if (Entry.Name.IsEmpty() || Entry.Index < 0 || Entry.Index >= NodeCount)
                return SurfaceRulesFail(
                    OutError, FString::Printf(TEXT("named %s %d is empty or out of range"), What, Index));
            for (int32 Other = 0; Other < Index; ++Other)
                if (Names[Other].Name.Equals(Entry.Name, ESearchCase::CaseSensitive))
                    return SurfaceRulesFail(OutError, FString::Printf(TEXT("%s %s is named twice"), What, *Entry.Name));
        }
        return true;
    }
}

int32 FVoxelSurfaceRules::FindRule(FStringView Name) const
{
    return FindSurfaceName(NamedRules, Name);
}

int32 FVoxelSurfaceRules::FindCondition(FStringView Name) const
{
    return FindSurfaceName(NamedConditions, Name);
}

int32 FVoxelSurfaceRules::AddString(const FString& Value)
{
    const int32 Found = Strings.IndexOfByPredicate(
        [&Value](const FString& Entry) { return Entry.Equals(Value, ESearchCase::CaseSensitive); });
    return Found != INDEX_NONE ? Found : Strings.Add(Value);
}

bool FVoxelSurfaceRules::Validate(FString* OutError) const
{
    const auto IsString = [this](int32 Index) { return Strings.IsValidIndex(Index) && !Strings[Index].IsEmpty(); };
    if (!Rules.IsValidIndex(Root)) return SurfaceRulesFail(OutError, TEXT("no root rule"));
    for (int32 Index = 0; Index < Rules.Num(); ++Index)
    {
        const FVoxelSurfaceRule& Rule = Rules[Index];
        const FString Where = FString::Printf(TEXT("rule %d (%s)"), Index,
            Rule.Op < EVoxelSurfaceRuleOp::Count ? GetRuleOpName(Rule.Op) : TEXT("unknown"));
        bool bValid = true;
        switch (Rule.Op)
        {
        case EVoxelSurfaceRuleOp::Sequence:
            bValid = Rule.Count >= 1 && Rule.First >= 0 && Rule.First + Rule.Count <= RuleList.Num();
            for (int32 Entry = 0; bValid && Entry < Rule.Count; ++Entry)
                bValid = RuleList[Rule.First + Entry] >= 0 && RuleList[Rule.First + Entry] < Index;
            break;
        case EVoxelSurfaceRuleOp::Condition:
            bValid = Conditions.IsValidIndex(Rule.Condition) && Rule.Then >= 0 && Rule.Then < Index;
            break;
        case EVoxelSurfaceRuleOp::Block:
            bValid = IsString(Rule.Block);
            break;
        case EVoxelSurfaceRuleOp::OreVein:
            bValid = IsString(Rule.Block) && IsString(Rule.RawOreBlock) && IsString(Rule.FillerBlock) &&
                IsString(Rule.Density) && IsString(Rule.Richness) && IsString(Rule.FillerGap) &&
                Rule.RawOreChance >= 0 && Rule.RawOreChance <= 1;
            break;
        case EVoxelSurfaceRuleOp::Bandlands:
            break;
        default:
            bValid = false;
            break;
        }
        if (!bValid) return SurfaceRulesFail(OutError, Where + TEXT(": bad fields or a child out of order"));
    }
    for (int32 Index = 0; Index < Conditions.Num(); ++Index)
    {
        const FVoxelSurfaceCondition& Condition = Conditions[Index];
        const FString Where = FString::Printf(TEXT("condition %d (%s)"), Index,
            Condition.Op < EVoxelSurfaceConditionOp::Count ? GetConditionOpName(Condition.Op) : TEXT("unknown"));
        const auto IsAnchor = [](const FVoxelVerticalAnchor& Anchor)
        { return Anchor.Kind <= EVoxelAnchorKind::BelowTop; };
        bool bValid = true;
        switch (Condition.Op)
        {
        case EVoxelSurfaceConditionOp::Biome:
            bValid = Condition.Count >= 1 && Condition.First >= 0 && Condition.First + Condition.Count <= Strings.Num();
            for (int32 Entry = 0; bValid && Entry < Condition.Count; ++Entry)
                bValid = IsString(Condition.First + Entry);
            break;
        case EVoxelSurfaceConditionOp::NoiseThreshold:
            bValid = IsString(Condition.First) && !FMath::IsNaN(Condition.MinThreshold) &&
                !FMath::IsNaN(Condition.MaxThreshold);
            break;
        case EVoxelSurfaceConditionOp::VerticalGradient:
            bValid = IsString(Condition.First) && IsAnchor(Condition.Anchor) && IsAnchor(Condition.FalseAnchor);
            break;
        case EVoxelSurfaceConditionOp::YAbove:
            bValid = IsAnchor(Condition.Anchor);
            break;
        case EVoxelSurfaceConditionOp::Not:
            bValid = Condition.Input >= 0 && Condition.Input < Index;
            break;
        case EVoxelSurfaceConditionOp::Water:
        case EVoxelSurfaceConditionOp::StoneDepth:
        case EVoxelSurfaceConditionOp::Steep:
        case EVoxelSurfaceConditionOp::Hole:
        case EVoxelSurfaceConditionOp::AbovePreliminarySurface:
            break;
        default:
            bValid = false;
            break;
        }
        if (!bValid) return SurfaceRulesFail(OutError, Where + TEXT(": bad fields or an input out of order"));
    }
    return CheckSurfaceNames(NamedRules, Rules.Num(), TEXT("rule"), OutError) &&
        CheckSurfaceNames(NamedConditions, Conditions.Num(), TEXT("condition"), OutError);
}

const TCHAR* FVoxelSurfaceRules::GetRuleOpName(EVoxelSurfaceRuleOp Op)
{
    static const TCHAR* const Names[] = {
        TEXT("sequence"), TEXT("condition"), TEXT("block"), TEXT("ore_vein"), TEXT("bandlands")};
    static_assert(UE_ARRAY_COUNT(Names) == int32(EVoxelSurfaceRuleOp::Count), "One name per rule op");
    return Op < EVoxelSurfaceRuleOp::Count ? Names[int32(Op)] : TEXT("");
}

const TCHAR* FVoxelSurfaceRules::GetConditionOpName(EVoxelSurfaceConditionOp Op)
{
    static const TCHAR* const Names[] = {TEXT("biome"), TEXT("noise_threshold"), TEXT("vertical_gradient"),
        TEXT("y_above"), TEXT("water"), TEXT("stone_depth"), TEXT("not"), TEXT("steep"), TEXT("hole"),
        TEXT("above_preliminary_surface")};
    static_assert(UE_ARRAY_COUNT(Names) == int32(EVoxelSurfaceConditionOp::Count), "One name per condition op");
    return Op < EVoxelSurfaceConditionOp::Count ? Names[int32(Op)] : TEXT("");
}
