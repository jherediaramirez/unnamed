#pragma once

#include "CoreMinimal.h"
#include "GameplayTagContainer.h"

// Hoe tilling and shovel flattening (survival B5): using a hoe or a shovel on a dirt-like block swaps it for farmland,
// dirt or a dirt path, for one point of wear (FFoundationAuthority::TillOn).
namespace FoundationTilling
{
    // #minecraft:hoes and #minecraft:shovels (Item.Hoes, Item.Shovels): data (Config/Tags/MinecraftItemTags.ini), so
    // they are looked up, as the axes' tag is (FoundationStripping::GetAxesTag).
    UNNAMED_API FGameplayTag GetHoesTag();
    UNNAMED_API FGameplayTag GetShovelsTag();

    // Wear per till or flatten (wiki "Hoe": "uses 1 durability"; wiki "Shovel": "losing one durability point").
    constexpr int32 WearPerUse = 1;

    // One conversion: the block it gives. Unless bAnyFace, it needs a hit face other than the bottom and air above;
    // bAnyFace rows (the hoe's rooted dirt) have neither rule. Drop: an item that comes out too (hanging roots).
    struct FConversion
    {
        FName From;
        FName To;
        bool bAnyFace = false;
        FName Drop;
    };

    // The hoe's conversions (wiki "Hoe", "Rooted Dirt", fetched 2026-10-15): dirt, grass block and dirt path till to
    // farmland, coarse dirt to dirt, all with air above; rooted dirt turns to dirt and drops hanging roots, with no
    // face or air rule on the wiki (provisional, provisional-values.md).
    UNNAMED_API TConstArrayView<FConversion> GetHoeTable();
    // The shovel's (wiki "Shovel", "Dirt Path"): grass block, dirt, coarse dirt, mycelium, podzol and rooted dirt
    // flatten to a dirt path, used on the top or a side with air above.
    UNNAMED_API TConstArrayView<FConversion> GetShovelTable();
    // The row for Block in Table, else null.
    UNNAMED_API const FConversion* Find(TConstArrayView<FConversion> Table, FName Block);
}
