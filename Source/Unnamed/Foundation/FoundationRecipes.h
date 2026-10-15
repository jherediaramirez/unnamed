#pragma once

#include "CoreMinimal.h"

// Crafting recipes (survival step 6), UObject-free: Reference/minecraft-items-26.3's recipes.json (the 26.3 jar's
// crafting_shaped and crafting_shapeless recipes) resolved to item indices. Matching follows minecraft.wiki
// ("Recipe", "Crafting"): a shaped pattern matches anywhere in the grid and mirrored left to right; a shapeless
// recipe matches its ingredients in any arrangement. Any grid size: the inventory's 2x2 and the crafting table's 3x3.

namespace FoundationCrafting
{
    // foundation.Crafting: 1 (the default) crafting grids yield results; 0 the grid takes and holds items and the
    // result stays empty.
    UNNAMED_API bool IsEnabled();
}

// What a match makes.
struct FFoundationRecipeResult
{
    int32 Item = INDEX_NONE;
    int32 Count = 0;

    bool IsEmpty() const { return Item == INDEX_NONE || Count <= 0; }
};

struct UNNAMED_API FFoundationRecipe
{
    FName Id;
    bool bShapeless = false;
    // Shaped: the pattern's size with its blank edge rows and columns trimmed (vanilla shrinks patterns the same
    // way), and its cells row-major; an empty cell holds no items. Shapeless: Width and Height 0.
    int32 Width = 0, Height = 0;
    // Shaped: Width * Height cells. Shapeless: one entry per ingredient. Each lists the item indices it accepts,
    // sorted.
    TArray<TArray<int32>> Cells;
    FFoundationRecipeResult Result;
};

// The recipes, in recipes.json's order (by recipe id). A grid that two recipes match makes the first one's result:
// provisional (the wiki does not say which wins; 26.3's data has no such pair among today's items, see the tests).
class UNNAMED_API FFoundationRecipeBook
{
public:
    TArray<FFoundationRecipe> Recipes;

    // The first recipe the grid matches, or INDEX_NONE. Grid holds Width * Height item indices row-major,
    // INDEX_NONE for an empty slot.
    int32 Match(TConstArrayView<int32> Grid, int32 Width, int32 Height) const;
    FFoundationRecipeResult Craft(TConstArrayView<int32> Grid, int32 Width, int32 Height) const;

    // Trims a shaped recipe's blank edges and sorts every cell's items; Add calls it.
    static void Normalize(FFoundationRecipe& Recipe);
    void Add(FFoundationRecipe Recipe);

    static bool MatchesShaped(const FFoundationRecipe& Recipe, TConstArrayView<int32> Grid, int32 Width, int32 Height);
    static bool MatchesShapeless(const FFoundationRecipe& Recipe, TConstArrayView<int32> Grid);
};
