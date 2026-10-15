#include "Foundation/FoundationMobAssets.h"
#include "Foundation/FoundationAuthority.h"
#include "UnnamedLog.h"
#include "UnnamedProjectSettings.h"

const UFoundationMobAssets* UFoundationMobAssets::LoadDefault()
{
    static const UFoundationMobAssets* Default = nullptr;
    static bool bTried = false;
    if (!bTried)
    {
        // Loaded once and kept for the process, like DA_Items: a reimport needs an editor restart.
        bTried = true;
        const FSoftObjectPath Path = UUnnamedProjectSettings::GetEntityAssetPath(TEXT("DA_Mobs"));
        Default = Cast<UFoundationMobAssets>(Path.TryLoad());
        if (Default)
            const_cast<UFoundationMobAssets*>(Default)->AddToRoot();
        else
            UE_LOG(LogUnnamedFoundation, Warning,
                TEXT("Mob asset %s is missing: mobs drop nothing and are not drawn. Run ImportMobs.py."),
                *Path.ToString());
    }
    return Default;
}

const FFoundationMobModelAsset* UFoundationMobAssets::FindModel(FName Id) const
{
    return Models.FindByPredicate([Id](const FFoundationMobModelAsset& Model) { return Model.Id == Id; });
}

int32 UFoundationMobAssets::AddLoot(FFoundationRules& Rules) const
{
    Rules.EntityLootTables.Reset();
    int32 Skipped = 0;
    for (int32 Index = 0; Index < LootTables.Num(); ++Index)
    {
        FFoundationLootTable Table;
        if (const FName Missing = FoundationLootAssets::MakeTable(LootTables[Index], Index, Rules, Table);
            !Missing.IsNone())
        {
            UE_LOG(LogUnnamedFoundation, Warning, TEXT("The loot table entities/%s is skipped: %s is not defined"),
                *LootTables[Index].Block.ToString(), *Missing.ToString());
            ++Skipped;
            continue;
        }
        Rules.EntityLootTables.Add(LootTables[Index].Block, MoveTemp(Table));
    }
    return Skipped;
}
