#pragma once

#include "CoreMinimal.h"
#include "Foundation/FoundationMobs.h"
#include "Math/RandomStream.h"

// Farm extras (survival T6 20e1): sheep colours, dyeing and shearing, milking, and the farm animals' cold and warm
// variants. Pure functions over the jar's data; the authority applies them (UseEntity, SpawnMob, DropMobLoot, EatGrass)
// and the view draws them. "Provisional" values have a row in
// C:\dev\tools\shift-managers\survival\provisional-values.md.
namespace FoundationMobFarm
{
    // foundation.Mobs.Farm: 1 (the default) colours, variants, shearing, dyeing and milking; 0 every sheep is white and
    // keeps its wool, every farm animal is temperate and a use on a mob passes, as before 20e1.
    UNNAMED_API bool IsOn();

    // The 16 colours in the jar's order (loot_table/shearing/sheep.json; DyeColor's ids 0-15). FFoundationMob::Color
    // indexes it; 0 is white.
    constexpr int32 ColorCount = 16;
    UNNAMED_API FName GetColorName(int32 Color);
    // The colour of a dye item ("red_dye"); INDEX_NONE for any other item.
    UNNAMED_API int32 FindDyeColor(FName Item);
    // A naturally spawned sheep's colour (minecraft.wiki "Sheep", provisional): black, gray and light gray 5% each,
    // brown 3%, then pink 1 in 500 of the rest (0.164%), else white (81.836%).
    UNNAMED_API int32 RollSheepColor(FRandomStream& Random);
    // The wool and undercoat tint, linear (provisional: minecraft.wiki "Dye" texture colours x WoolShade; white is
    // WhiteWool).
    UNNAMED_API FLinearColor GetWoolTint(int32 Color);
    constexpr float WoolShade = .75f;
    constexpr uint32 WhiteWool = 0xE6E6E6;

    // [jar cow_variant, pig_variant, chicken_variant] a farm animal spawned in a biome tagged
    // spawns_cold_variant_farm_animals is cold, in spawns_warm_variant_farm_animals warm (priority 1), else temperate
    // (priority 0). Biome is the full id ("minecraft:snowy_plains").
    UNNAMED_API EFoundationMobVariant GetVariant(FName Biome);
    // Cow, pig and chicken have variants.
    UNNAMED_API bool HasVariants(EFoundationMobType Type);
    // DA_Mobs' model id of a variant: the main id for temperate ("cow"), else "cold_cow" / "warm_cow" (MobImportData.py
    // VARIANTS; the warm pig and chicken keep the main geometry with their own texture).
    UNNAMED_API FName GetModelId(EFoundationMobType Type, EFoundationMobVariant Variant);

    // The colour and variant stream of a mob (apart from Mob.Random and the spawn streams, so the 20b-20c draws keep
    // their order).
    UNNAMED_API int32 MakeSeed(int32 MobId, int32 LootSeed);

    // Shearing [jar loot_table/shearing/sheep.json]: an alive adult sheep with its wool; the shears wear ShearWear
    // (minecraft.wiki "Shears", provisional). DA_Mobs holds the table as ShearTable.
    constexpr int32 ShearWear = 1;
    inline const TCHAR* const ShearTable = TEXT("shearing/sheep");
    // Provisional (minecraft.wiki "Sheep"): the shorn wool pops ShearUpCm above the sheep's feet with a small random
    // push (ShearSidewaysCm sideways, up to ShearPopCm up, per step).
    constexpr double ShearUpCm = 100, ShearSidewaysCm = 10, ShearPopCm = 5;
}
