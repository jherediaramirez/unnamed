#include "Foundation/FoundationStripping.h"

namespace FoundationStripping
{
    FGameplayTag GetAxesTag()
    {
        // Cached once found; asked again until the tag ini has loaded.
        static FGameplayTag Tag;
        if (!Tag.IsValid()) Tag = FGameplayTag::RequestGameplayTag(TEXT("Item.Axes"), false);
        return Tag;
    }

    TConstArrayView<TPair<FName, FName>> GetTable()
    {
        using FPair = TPair<FName, FName>;
        static const FPair Table[] = {
            FPair(TEXT("oak_log"), TEXT("stripped_oak_log")),
            FPair(TEXT("oak_wood"), TEXT("stripped_oak_wood")),
            FPair(TEXT("spruce_log"), TEXT("stripped_spruce_log")),
            FPair(TEXT("spruce_wood"), TEXT("stripped_spruce_wood")),
            FPair(TEXT("birch_log"), TEXT("stripped_birch_log")),
            FPair(TEXT("birch_wood"), TEXT("stripped_birch_wood")),
            FPair(TEXT("jungle_log"), TEXT("stripped_jungle_log")),
            FPair(TEXT("jungle_wood"), TEXT("stripped_jungle_wood")),
            FPair(TEXT("acacia_log"), TEXT("stripped_acacia_log")),
            FPair(TEXT("acacia_wood"), TEXT("stripped_acacia_wood")),
            FPair(TEXT("cherry_log"), TEXT("stripped_cherry_log")),
            FPair(TEXT("cherry_wood"), TEXT("stripped_cherry_wood")),
            FPair(TEXT("dark_oak_log"), TEXT("stripped_dark_oak_log")),
            FPair(TEXT("dark_oak_wood"), TEXT("stripped_dark_oak_wood")),
            FPair(TEXT("pale_oak_log"), TEXT("stripped_pale_oak_log")),
            FPair(TEXT("pale_oak_wood"), TEXT("stripped_pale_oak_wood")),
            FPair(TEXT("mangrove_log"), TEXT("stripped_mangrove_log")),
            FPair(TEXT("mangrove_wood"), TEXT("stripped_mangrove_wood")),
            FPair(TEXT("poplar_log"), TEXT("stripped_poplar_log")),
            FPair(TEXT("poplar_wood"), TEXT("stripped_poplar_wood")),
            FPair(TEXT("crimson_stem"), TEXT("stripped_crimson_stem")),
            FPair(TEXT("crimson_hyphae"), TEXT("stripped_crimson_hyphae")),
            FPair(TEXT("warped_stem"), TEXT("stripped_warped_stem")),
            FPair(TEXT("warped_hyphae"), TEXT("stripped_warped_hyphae")),
            FPair(TEXT("bamboo_block"), TEXT("stripped_bamboo_block")),
        };
        return Table;
    }

    FName FindStripped(FName Block)
    {
        for (const TPair<FName, FName>& Pair : GetTable())
            if (Pair.Key == Block) return Pair.Value;
        return NAME_None;
    }
}
