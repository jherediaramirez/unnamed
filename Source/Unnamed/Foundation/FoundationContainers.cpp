#include "Foundation/FoundationContainers.h"
#include "HAL/IConsoleManager.h"

namespace
{
    TAutoConsoleVariable<int32> CVarFurnaceTick(TEXT("foundation.Furnace"), 1,
        TEXT("1: furnaces, smokers and blast furnaces open their menus and, in loaded columns, burn fuel, cook and "
             "light up, whether or not their screen is open (T1 steps 8a, 8b). 0: as before 8a: no menu (a right-click "
             "places against them) and no ticks."));
    TAutoConsoleVariable<int32> CVarFurnaceAmbience(TEXT("foundation.Furnace.Ambience"), 1,
        TEXT("1: a lit furnace, smoker or blast furnace plays its sound (T1 step 8b; rates provisional). 0: silent."));
    TAutoConsoleVariable<int32> CVarBarrel(TEXT("foundation.Barrel"), 1,
        TEXT("1: a barrel opens its 27-slot menu and shows its open state while viewed (T1 step 9). 0: as before: no "
             "menu (a right-click places against it)."));

    struct FFurnaceFuelRow
    {
        const TCHAR* Item;
        int32 FurnaceTicks, SmokerTicks, BlastTicks;
    };

    struct FFurnaceCookingRow
    {
        const TCHAR* Id;
        EFoundationCookingKind Kind;
        const TCHAR* Result;
        int32 Count, CookTicks;
        float Experience;
        const TCHAR* Ingredients;
    };

    // Two passes over the generated file: the fuels (cooking rows expand to nothing), then the recipes.
#define FOUNDATION_FUEL(Item, Furnace, Smoker, Blast) {Item, Furnace, Smoker, Blast},
#define FOUNDATION_COOKING(...)
    const FFurnaceFuelRow FurnaceFuelRows[] = {
#include "Foundation/FoundationCookingData.inl"
    };
#undef FOUNDATION_FUEL
#undef FOUNDATION_COOKING
#define FOUNDATION_FUEL(...)
#define FOUNDATION_COOKING(Id, Kind, Result, Count, Ticks, Xp, Ingredients)                                            \
    {Id, EFoundationCookingKind::Kind, Result, Count, Ticks, Xp, Ingredients},
    const FFurnaceCookingRow FurnaceCookingRows[] = {
#include "Foundation/FoundationCookingData.inl"
    };
#undef FOUNDATION_FUEL
#undef FOUNDATION_COOKING

    void ShrinkFurnaceSlot(FFoundationItemInstance& Stack)
    {
        ++Stack.Revision;
        if (--Stack.Count <= 0) Stack = {};
    }
}

void FFoundationCooking::Build(TFunctionRef<int32(FName Item)> FindItem)
{
    Recipes.Reset();
    Fuels.Reset();
    ByIngredient.Reset();
    for (const FFurnaceFuelRow& Row : FurnaceFuelRows)
        if (const int32 Item = FindItem(Row.Item); Item != INDEX_NONE)
            Fuels.Add(Item, FIntVector(Row.FurnaceTicks, Row.SmokerTicks, Row.BlastTicks));
    for (const FFurnaceCookingRow& Row : FurnaceCookingRows)
    {
        FFoundationCookingRecipe Recipe;
        Recipe.Id = Row.Id;
        Recipe.Kind = Row.Kind;
        Recipe.Result = FindItem(Row.Result);
        Recipe.Count = Row.Count;
        Recipe.CookTicks = Row.CookTicks;
        Recipe.Experience = Row.Experience;
        TArray<FString> Names;
        FString(Row.Ingredients).ParseIntoArray(Names, TEXT(" "));
        for (const FString& Name : Names)
            if (const int32 Item = FindItem(FName(*Name)); Item != INDEX_NONE) Recipe.Ingredients.Add(Item);
        if (Recipe.Result == INDEX_NONE || Recipe.Ingredients.IsEmpty()) continue;
        const int32 Index = Recipes.Add(MoveTemp(Recipe));
        for (const int32 Item : Recipes[Index].Ingredients)
            ByIngredient.Add({uint8(Row.Kind), Item}, Index);
    }
    bBuilt = true;
}

int32 FFoundationCooking::GetBurnTicks(int32 Item, EFoundationCookingKind Kind) const
{
    const FIntVector* Ticks = Fuels.Find(Item);
    if (!Ticks) return 0;
    return Kind == EFoundationCookingKind::Smoking ? Ticks->Y
        : Kind == EFoundationCookingKind::Blasting ? Ticks->Z
                                                   : Ticks->X;
}

int32 FFoundationCooking::Find(EFoundationCookingKind Kind, int32 Item) const
{
    const int32* Recipe = ByIngredient.Find({uint8(Kind), Item});
    return Recipe ? *Recipe : INDEX_NONE;
}

const FFoundationCookingRecipe* FFoundationCooking::Get(int32 Recipe) const
{
    return Recipes.IsValidIndex(Recipe) ? &Recipes[Recipe] : nullptr;
}

FFoundationBlockEntity& FFoundationBlockEntities::FindOrAdd(
    const FIntVector& Cell, EFoundationBlockEntityKind Kind, FName Block)
{
    FFoundationBlockEntity* Found = Entities.Find(Cell);
    if (Found && Found->Kind == Kind && Found->Block == Block) return *Found;
    FFoundationBlockEntity& Entity = Entities.Add(Cell);
    Entity.Kind = Kind;
    Entity.Block = Block;
    Entity.Items.SetNum(FFoundationBlockEntity::GetSlots(Kind));
    return Entity;
}

bool FFoundationBlockEntities::Remove(const FIntVector& Cell, FFoundationBlockEntity* OutRemoved)
{
    FFoundationBlockEntity Removed;
    if (!Entities.RemoveAndCopyValue(Cell, Removed)) return false;
    if (OutRemoved) *OutRemoved = MoveTemp(Removed);
    return true;
}

FFoundationItemInstance FFoundationBlockEntities::MakeStack(int32 Item, int32 Count)
{
    FFoundationItemInstance Stack;
    Stack.InstanceId = NextInstanceId++;
    Stack.Item = Item;
    Stack.Count = Count;
    return Stack;
}

bool FoundationFurnace::IsOn()
{
    return CVarFurnaceTick.GetValueOnAnyThread() != 0;
}

bool FoundationBarrel::IsOn()
{
    return CVarBarrel.GetValueOnAnyThread() != 0;
}

bool FoundationFurnace::IsAmbienceOn()
{
    return CVarFurnaceAmbience.GetValueOnAnyThread() != 0;
}

int32 FoundationFurnace::StepAmbienceClock(float& Clock, float DeltaTime, bool bRunning)
{
    constexpr float TicksPerSecond = 20;
    if (!bRunning)
    {
        Clock = 0;
        return 0;
    }
    Clock += DeltaTime;
    const int32 Ticks = FMath::Min(FMath::FloorToInt32(Clock * TicksPerSecond), 4);
    Clock = FMath::Min(Clock - Ticks / TicksPerSecond, 1 / TicksPerSecond);
    return Ticks;
}

EFoundationCookingKind FoundationFurnace::GetCookingKind(FName Block)
{
    static const FName Smoker(TEXT("smoker")), BlastFurnace(TEXT("blast_furnace"));
    return Block == Smoker      ? EFoundationCookingKind::Smoking
        : Block == BlastFurnace ? EFoundationCookingKind::Blasting
                                : EFoundationCookingKind::Smelting;
}

int32 FoundationFurnace::GetCookTotal(const FFoundationCookingRecipe* Recipe, EFoundationCookingKind Kind)
{
    return (Recipe ? Recipe->CookTicks : 200) / GetSpeed(Kind);
}

bool FoundationFurnace::Tick(FFoundationBlockEntity& Furnace, const FFoundationFurnaceRules& Rules,
    FFoundationBlockEntities& Store, EFoundationCookingKind Kind)
{
    check(Rules.Cooking);
    const FFoundationCooking& Cooking = *Rules.Cooking;
    FFoundationItemInstance& Input = Furnace.Items[FFoundationBlockEntity::FurnaceInput];
    FFoundationItemInstance& Fuel = Furnace.Items[FFoundationBlockEntity::FurnaceFuel];
    FFoundationItemInstance& Output = Furnace.Items[FFoundationBlockEntity::FurnaceResult];
    const bool bWasLit = Furnace.IsLit();
    bool bChanged = false;
    if (Furnace.IsLit()) --Furnace.LitTime;
    const int32 RecipeIndex = Input.IsEmpty() ? INDEX_NONE : Cooking.Find(Kind, Input.Item);
    const FFoundationCookingRecipe* Recipe = Cooking.Get(RecipeIndex);
    // Whether the input cooks into the output slot: empty, or the same item with room for the result.
    auto CanBurn = [&]
    {
        if (!Recipe || Input.IsEmpty()) return false;
        if (Output.IsEmpty()) return true;
        if (Output.Item != Recipe->Result || Output.Damage != 0) return false;
        const int32 Max = FMath::Min(64, Rules.GetMaxStack ? FMath::Max(1, Rules.GetMaxStack(Output.Item)) : 64);
        return Output.Count + Recipe->Count <= Max;
    };
    if (Furnace.IsLit() || (!Fuel.IsEmpty() && !Input.IsEmpty()))
    {
        // Lights on the tick the fuel goes in (burn boundaries: lit at tick 1, the fuel and the cooking start
        // together).
        if (!Furnace.IsLit() && CanBurn())
        {
            Furnace.LitTime = Furnace.LitDuration = Fuel.IsEmpty() ? 0 : Cooking.GetBurnTicks(Fuel.Item, Kind);
            if (Furnace.IsLit())
            {
                bChanged = true;
                const int32 Used = Fuel.Item;
                ShrinkFurnaceSlot(Fuel);
                // A lava bucket leaves its bucket (the item's crafting remainder; minecraft.wiki "Furnace", "Fuel").
                if (Fuel.IsEmpty() && Used == Rules.LavaBucket && Rules.Bucket != INDEX_NONE)
                    Fuel = Store.MakeStack(Rules.Bucket, 1);
            }
        }
        if (Furnace.IsLit() && CanBurn())
        {
            if (Furnace.CookTotal <= 0) Furnace.CookTotal = GetCookTotal(Recipe, Kind);
            // The first output on the cook total's tick (survival.r3.furnace_ticks: 200 for a furnace, 100 for the
            // smoker and blast furnace).
            if (++Furnace.CookProgress >= Furnace.CookTotal)
            {
                Furnace.CookProgress = 0;
                // A wet sponge dried over an empty bucket fills it (minecraft.wiki "Sponge").
                if (Input.Item == Rules.WetSponge && !Fuel.IsEmpty() && Fuel.Item == Rules.Bucket &&
                    Rules.WaterBucket != INDEX_NONE)
                    Fuel = Store.MakeStack(Rules.WaterBucket, 1);
                if (Output.IsEmpty())
                    Output = Store.MakeStack(Recipe->Result, Recipe->Count);
                else
                {
                    Output.Count += Recipe->Count;
                    ++Output.Revision;
                }
                ShrinkFurnaceSlot(Input);
                ++Furnace.RecipesUsed.FindOrAdd(RecipeIndex);
                // The next item's cook time (the default with nothing left to cook).
                Furnace.CookTotal =
                    GetCookTotal(Cooking.Get(Input.IsEmpty() ? INDEX_NONE : Cooking.Find(Kind, Input.Item)), Kind);
            }
            bChanged = true;
        }
        else if (Furnace.CookProgress != 0)
        {
            Furnace.CookProgress = 0;
            bChanged = true;
        }
    }
    else if (!Furnace.IsLit() && Furnace.CookProgress > 0)
    {
        // Cooling: unfinished progress loses 2 a tick, clamped at 0 (survival.r3.furnace_ticks).
        Furnace.CookProgress = FMath::Clamp(Furnace.CookProgress - 2, 0, FMath::Max(Furnace.CookTotal, 0));
        bChanged = true;
    }
    return bChanged || bWasLit != Furnace.IsLit();
}

void FoundationFurnace::OnInputChanged(
    FFoundationBlockEntity& Furnace, const FFoundationCooking& Cooking, EFoundationCookingKind Kind)
{
    const FFoundationItemInstance& Input = Furnace.Items[FFoundationBlockEntity::FurnaceInput];
    const FFoundationCookingRecipe* Recipe = Cooking.Get(Input.IsEmpty() ? INDEX_NONE : Cooking.Find(Kind, Input.Item));
    Furnace.CookTotal = GetCookTotal(Recipe, Kind);
    Furnace.CookProgress = 0;
}

int32 FoundationFurnace::RollExperience(
    const TMap<int32, int32>& RecipesUsed, const FFoundationCooking& Cooking, FRandomStream& Random)
{
    int32 Total = 0;
    for (const auto& [Recipe, Count] : RecipesUsed)
        if (const FFoundationCookingRecipe* Found = Cooking.Get(Recipe))
        {
            // In float, as the recipe's experience is (10 x 0.1 is a whole 1, with no roll).
            const float Points = float(Count) * Found->Experience;
            const int32 Whole = FMath::FloorToInt32(Points);
            const float Fraction = Points - float(Whole);
            Total += Whole + (Fraction > 0 && Random.FRand() < Fraction ? 1 : 0);
        }
    return Total;
}
