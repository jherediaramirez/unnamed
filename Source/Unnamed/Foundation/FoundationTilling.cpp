#include "Foundation/FoundationTilling.h"

namespace FoundationTilling
{
    namespace
    {
        // Cached once found; asked again until the tag ini has loaded.
        FGameplayTag RequestTillingTag(FGameplayTag& Cache, const TCHAR* Name)
        {
            if (!Cache.IsValid()) Cache = FGameplayTag::RequestGameplayTag(FName(Name), false);
            return Cache;
        }
    }

    FGameplayTag GetHoesTag()
    {
        static FGameplayTag Tag;
        return RequestTillingTag(Tag, TEXT("Item.Hoes"));
    }

    FGameplayTag GetShovelsTag()
    {
        static FGameplayTag Tag;
        return RequestTillingTag(Tag, TEXT("Item.Shovels"));
    }

    TConstArrayView<FConversion> GetHoeTable()
    {
        static const FConversion Table[] = {
            {TEXT("dirt"), TEXT("farmland")},
            {TEXT("grass_block"), TEXT("farmland")},
            {TEXT("dirt_path"), TEXT("farmland")},
            {TEXT("coarse_dirt"), TEXT("dirt")},
            {TEXT("rooted_dirt"), TEXT("dirt"), true, TEXT("hanging_roots")},
        };
        return Table;
    }

    TConstArrayView<FConversion> GetShovelTable()
    {
        static const FConversion Table[] = {
            {TEXT("grass_block"), TEXT("dirt_path")},
            {TEXT("dirt"), TEXT("dirt_path")},
            {TEXT("coarse_dirt"), TEXT("dirt_path")},
            {TEXT("mycelium"), TEXT("dirt_path")},
            {TEXT("podzol"), TEXT("dirt_path")},
            {TEXT("rooted_dirt"), TEXT("dirt_path")},
        };
        return Table;
    }

    const FConversion* Find(TConstArrayView<FConversion> Table, FName Block)
    {
        for (const FConversion& Row : Table)
            if (Row.From == Block) return &Row;
        return nullptr;
    }
}
