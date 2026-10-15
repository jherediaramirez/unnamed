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

void UFoundationMobAssets::FindLayers(FName Id, TArray<const FFoundationMobModelAsset*>& Out) const
{
    for (const FFoundationMobModelAsset& Model : Models)
        if (Model.Id == Id) Out.Add(&Model);
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
            // Once per process at Warning (every lab load rebuilds the rules; the skeleton's arrow waits for T2 19c).
            static TSet<FName> Warned;
            bool bAlreadyWarned = false;
            Warned.Add(LootTables[Index].Block, &bAlreadyWarned);
            if (!bAlreadyWarned)
            {
                UE_LOG(LogUnnamedFoundation, Warning, TEXT("The loot table entities/%s is skipped: %s is not defined"),
                    *LootTables[Index].Block.ToString(), *Missing.ToString());
            }
            ++Skipped;
            continue;
        }
        Rules.EntityLootTables.Add(LootTables[Index].Block, MoveTemp(Table));
    }
    return Skipped;
}

void UFoundationMobAssets::AddSpawns(FFoundationRules& Rules) const
{
    Rules.BiomeSpawns.Reset();
    for (const FFoundationBiomeSpawnAsset& Biome : BiomeSpawns)
    {
        FFoundationBiomeSpawns& Out = Rules.BiomeSpawns.Add(Biome.Biome);
        Out.Chance = Biome.Chance;
        for (const FFoundationMobSpawnAsset& Entry : Biome.Creatures)
        {
            EFoundationMobType Type;
            if (Entry.Weight > 0 && FoundationMobs::FindType(Entry.Mob, Type) &&
                FoundationMobs::GetKind(Type).bSpawnable)
                Out.Creatures.Add({Type, Entry.Weight, Entry.MinCount, FMath::Max(Entry.MinCount, Entry.MaxCount)});
        }
        for (const FFoundationMobSpawnAsset& Entry : Biome.Monsters)
        {
            if (Entry.Weight <= 0) continue;
            EFoundationMobType Type = EFoundationMobType::Zombie;
            const bool bBuilt = FoundationMobs::FindType(Entry.Mob, Type) && FoundationMobs::GetKind(Type).bSpawnable;
            Out.Monsters.Add({Type, Entry.Weight, Entry.MinCount, FMath::Max(Entry.MinCount, Entry.MaxCount), !bBuilt});
        }
    }
}
