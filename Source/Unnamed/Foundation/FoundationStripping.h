#pragma once

#include "CoreMinimal.h"
#include "GameplayTagContainer.h"

// Axe stripping (survival step 14): right-clicking a log, wood, stem, hyphae or bamboo block with an axe swaps it for
// its stripped block, keeping its properties (the axis), for one point of wear.
namespace FoundationStripping
{
    // #minecraft:axes, Item.Axes: data (Config/Tags/MinecraftItemTags.ini), so it is looked up, not declared natively.
    UNNAMED_API FGameplayTag GetAxesTag();

    // Wear per strip (wiki "Axe": stripping uses 1 durability).
    constexpr int32 WearPerStrip = 1;

    // The stripped block for a block id, else None. Vanilla's strip map lives in game code only, so the pairs follow
    // the wiki "Axe" page (log, wood, stem, hyphae and bamboo block); a test pins the targets against the block
    // reference's stripped_* blocks.
    UNNAMED_API FName FindStripped(FName Block);
    UNNAMED_API TConstArrayView<TPair<FName, FName>> GetTable();
}
