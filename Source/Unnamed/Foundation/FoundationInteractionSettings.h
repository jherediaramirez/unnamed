#pragma once

#include "CoreMinimal.h"
#include "Engine/DeveloperSettings.h"
#include "Foundation/FoundationBlocks.h"
#include "Foundation/FoundationInteraction.h"
#include "Foundation/FoundationItems.h"
#include "FoundationInteractionSettings.generated.h"

struct FFoundationRules;

// Authorable interaction data (Project Settings > Game > Unnamed Interaction). The C++ defaults are the
// handoff baseline (TUNING-BASELINE.json); a deliberate change belongs in
// [/Script/Unnamed.FoundationInteractionSettings] in Config/DefaultGame.ini. Listing an array there
// replaces the C++ list, so copy the existing entries when adding one.
UCLASS(Config = Game, DefaultConfig, meta = (DisplayName = "Unnamed Interaction"))
class UNNAMED_API UFoundationInteractionSettings : public UDeveloperSettings
{
    GENERATED_BODY()
public:
    UFoundationInteractionSettings();

    static const UFoundationInteractionSettings& Get() { return *GetDefault<UFoundationInteractionSettings>(); }

    const FFoundationPlacementPreset* FindPreset(FName Name) const;
    // The authority's rules (reach comes from UFoundationProfile). Items: the block registry's block items, then
    // DA_Items (the other 26.3 items), then the project's Items; an id defined twice is an error.
    void BuildRules(FFoundationRules& Rules) const;
    // One item per block that has one (item id = block id), appended unless an item with that id already exists.
    static void AddBlockItems(const class FFoundationBlockRegistry& Registry, TArray<FFoundationItemDefinition>& Items);
    // Appends Added to Into; an id already there logs an error naming Source and is skipped. Returns the clash count.
    static int32 MergeItems(
        TArray<FFoundationItemDefinition>& Into, TConstArrayView<FFoundationItemDefinition> Added, const TCHAR* Source);

    // VanillaReference (initial default) and LucidFluid (experimental A/B).
    UPROPERTY(Config, EditAnywhere, Category = "Placement")
    TArray<FFoundationPlacementPreset> Presets;

    UPROPERTY(Config, EditAnywhere, Category = "Placement")
    FName DefaultPreset;

    // Project blocks, appended after the imported 26.3 blocks (Reference/minecraft-blocks-26.3). Empty by default.
    UPROPERTY(Config, EditAnywhere, Category = "Definitions")
    TArray<FFoundationBlockDefinition> Blocks;

    // Project items, appended after the 26.3 items (DA_Items). Empty by default.
    UPROPERTY(Config, EditAnywhere, Category = "Definitions")
    TArray<FFoundationItemDefinition> Items;

    // The empty slot's melee and mining profile.
    UPROPERTY(Config, EditAnywhere, Category = "Definitions")
    FFoundationItemDefinition EmptyHand;

    // Hotbar contents from slot 1 after every lab reset.
    UPROPERTY(Config, EditAnywhere, Category = "Lab")
    TArray<FName> Loadout;

    // The lab default: infinite placement supply and no tool wear.
    UPROPERTY(Config, EditAnywhere, Category = "Lab")
    bool bInfiniteSupplyDefault = true;

    // The finite verification fixture: this many blocks, with tool wear on.
    UPROPERTY(Config, EditAnywhere, Category = "Lab")
    int32 FiniteBlockCount = 64;

    UPROPERTY(Config, EditAnywhere, Category = "Lab")
    float TrainingTargetMaxHealth = 20;

    // Mining multipliers (26.3: not on ground x0.2; eyes in water, the submerged attribute 0.2).
    UPROPERTY(Config, EditAnywhere, Category = "Rules")
    float AirborneMiningMultiplier = .2f;

    UPROPERTY(Config, EditAnywhere, Category = "Rules")
    float SubmergedMiningMultiplier = .2f;
};
