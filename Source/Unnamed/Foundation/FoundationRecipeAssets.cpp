#include "Foundation/FoundationRecipeAssets.h"
#include "Foundation/FoundationAuthority.h"
#include "UnnamedLog.h"
#include "UnnamedProjectSettings.h"

const UFoundationRecipeAssets* UFoundationRecipeAssets::LoadDefault()
{
    static const UFoundationRecipeAssets* Default = nullptr;
    static bool bTried = false;
    if (!bTried)
    {
        // Loaded once and kept for the process, like DA_Items: a reimport needs an editor restart.
        bTried = true;
        const FSoftObjectPath Path = UUnnamedProjectSettings::GetItemAssetPath(TEXT("DA_Recipes"));
        Default = Cast<UFoundationRecipeAssets>(Path.TryLoad());
        if (Default)
            const_cast<UFoundationRecipeAssets*>(Default)->AddToRoot();
        else
            UE_LOG(LogUnnamedFoundation, Warning,
                TEXT("Recipe asset %s is missing: nothing can be crafted. Run ImportRecipes.py."), *Path.ToString());
    }
    return Default;
}

int32 UFoundationRecipeAssets::AddRecipes(FFoundationRules& Rules) const
{
    return AddRecipes(Recipes, Rules);
}

int32 UFoundationRecipeAssets::AddRecipes(TConstArrayView<FFoundationRecipeAsset> Assets, FFoundationRules& Rules)
{
    TMap<FName, int32> Index;
    for (int32 Item = 0; Item < Rules.Items.Num(); ++Item)
        Index.Add(Rules.Items[Item].Id, Item);
    Rules.Recipes.Recipes.Reset();
    int32 Skipped = 0;
    for (const FFoundationRecipeAsset& Asset : Assets)
    {
        FFoundationRecipe Recipe;
        Recipe.Id = Asset.Id;
        Recipe.bShapeless = Asset.bShapeless;
        Recipe.Width = Asset.Width;
        Recipe.Height = Asset.Height;
        const int32* Result = Index.Find(Asset.ResultItem);
        bool bResolved = Result != nullptr && Asset.ResultCount > 0 &&
            (Asset.bShapeless ? !Asset.Cells.IsEmpty() : Asset.Cells.Num() == Asset.Width * Asset.Height);
        for (int32 Cell = 0; Cell < Asset.Cells.Num() && bResolved; ++Cell)
        {
            TArray<int32>& Items = Recipe.Cells.AddDefaulted_GetRef();
            for (const FName Id : Asset.Cells[Cell].Items)
                if (const int32* Item = Index.Find(Id)) Items.AddUnique(*Item);
            // An ingredient none of whose items exists yet; an empty shaped cell is fine.
            bResolved = !Items.IsEmpty() ||
                (!Asset.bShapeless && Asset.Cells[Cell].Items.IsEmpty() && Asset.Cells[Cell].Tag.IsNone());
        }
        if (!bResolved)
        {
            ++Skipped;
            continue;
        }
        Recipe.Result = {*Result, Asset.ResultCount};
        Rules.Recipes.Add(MoveTemp(Recipe));
    }
    return Skipped;
}
