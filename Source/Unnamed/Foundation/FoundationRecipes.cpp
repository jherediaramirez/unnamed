#include "Foundation/FoundationRecipes.h"
#include "Algo/BinarySearch.h"
#include "HAL/IConsoleManager.h"

namespace
{
    TAutoConsoleVariable<int32> CVarCrafting(TEXT("foundation.Crafting"), 1,
        TEXT("1: crafting grids yield results (survival step 6). 0: the grid takes and holds items and the result "
             "stays empty."));

    bool RecipeAccepts(const TArray<int32>& Cell, int32 Item)
    {
        return Item == INDEX_NONE ? Cell.IsEmpty() : !Cell.IsEmpty() && Algo::BinarySearch(Cell, Item) != INDEX_NONE;
    }

    // Shapeless: every grid item to a distinct ingredient (a small bipartite match; at most 9 of each).
    bool RecipeAssign(const FFoundationRecipe& Recipe, TConstArrayView<int32> Items, int32 Next, uint32 Used)
    {
        if (Next == Items.Num()) return true;
        for (int32 Index = 0; Index < Recipe.Cells.Num(); ++Index)
            if (!(Used & 1u << Index) && RecipeAccepts(Recipe.Cells[Index], Items[Next]) &&
                RecipeAssign(Recipe, Items, Next + 1, Used | 1u << Index))
                return true;
        return false;
    }
}

bool FoundationCrafting::IsEnabled()
{
    return CVarCrafting.GetValueOnAnyThread() != 0;
}

void FFoundationRecipeBook::Normalize(FFoundationRecipe& Recipe)
{
    for (TArray<int32>& Cell : Recipe.Cells)
        Cell.Sort();
    if (Recipe.bShapeless || Recipe.Width <= 0 || Recipe.Height <= 0) return;
    int32 MinX = Recipe.Width, MinY = Recipe.Height, MaxX = -1, MaxY = -1;
    for (int32 Y = 0; Y < Recipe.Height; ++Y)
        for (int32 X = 0; X < Recipe.Width; ++X)
            if (!Recipe.Cells[Y * Recipe.Width + X].IsEmpty())
            {
                MinX = FMath::Min(MinX, X);
                MinY = FMath::Min(MinY, Y);
                MaxX = FMath::Max(MaxX, X);
                MaxY = FMath::Max(MaxY, Y);
            }
    if (MaxX < 0) return;
    const int32 Width = MaxX - MinX + 1, Height = MaxY - MinY + 1;
    if (Width == Recipe.Width && Height == Recipe.Height) return;
    TArray<TArray<int32>> Cells;
    for (int32 Y = MinY; Y <= MaxY; ++Y)
        for (int32 X = MinX; X <= MaxX; ++X)
            Cells.Add(MoveTemp(Recipe.Cells[Y * Recipe.Width + X]));
    Recipe.Cells = MoveTemp(Cells);
    Recipe.Width = Width;
    Recipe.Height = Height;
}

void FFoundationRecipeBook::Add(FFoundationRecipe Recipe)
{
    Normalize(Recipe);
    Recipes.Add(MoveTemp(Recipe));
}

bool FFoundationRecipeBook::MatchesShaped(
    const FFoundationRecipe& Recipe, TConstArrayView<int32> Grid, int32 Width, int32 Height)
{
    // The grid's filled bounding box must be the pattern's size; then the pattern as is, or mirrored.
    int32 MinX = Width, MinY = Height, MaxX = -1, MaxY = -1;
    for (int32 Y = 0; Y < Height; ++Y)
        for (int32 X = 0; X < Width; ++X)
            if (Grid[Y * Width + X] != INDEX_NONE)
            {
                MinX = FMath::Min(MinX, X);
                MinY = FMath::Min(MinY, Y);
                MaxX = FMath::Max(MaxX, X);
                MaxY = FMath::Max(MaxY, Y);
            }
    if (MaxX < 0 || MaxX - MinX + 1 != Recipe.Width || MaxY - MinY + 1 != Recipe.Height) return false;
    for (const bool bMirrored : {false, true})
    {
        bool bAll = true;
        for (int32 Y = 0; Y < Recipe.Height && bAll; ++Y)
            for (int32 X = 0; X < Recipe.Width && bAll; ++X)
            {
                const int32 PatternX = bMirrored ? Recipe.Width - 1 - X : X;
                bAll = RecipeAccepts(Recipe.Cells[Y * Recipe.Width + PatternX], Grid[(MinY + Y) * Width + MinX + X]);
            }
        if (bAll) return true;
    }
    return false;
}

bool FFoundationRecipeBook::MatchesShapeless(const FFoundationRecipe& Recipe, TConstArrayView<int32> Grid)
{
    TArray<int32, TInlineAllocator<9>> Items;
    for (const int32 Item : Grid)
        if (Item != INDEX_NONE) Items.Add(Item);
    return !Items.IsEmpty() && Items.Num() == Recipe.Cells.Num() && Items.Num() <= 32 &&
        RecipeAssign(Recipe, Items, 0, 0);
}

int32 FFoundationRecipeBook::Match(TConstArrayView<int32> Grid, int32 Width, int32 Height) const
{
    if (Width <= 0 || Height <= 0 || Grid.Num() != Width * Height) return INDEX_NONE;
    for (int32 Index = 0; Index < Recipes.Num(); ++Index)
    {
        const FFoundationRecipe& Recipe = Recipes[Index];
        if (Recipe.bShapeless ? MatchesShapeless(Recipe, Grid) : MatchesShaped(Recipe, Grid, Width, Height))
            return Index;
    }
    return INDEX_NONE;
}

FFoundationRecipeResult FFoundationRecipeBook::Craft(TConstArrayView<int32> Grid, int32 Width, int32 Height) const
{
    const int32 Index = Match(Grid, Width, Height);
    return Index == INDEX_NONE ? FFoundationRecipeResult() : Recipes[Index].Result;
}
