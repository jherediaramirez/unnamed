#include "Misc/AutomationTest.h"
#include "Foundation/FoundationAuthority.h"
#include "Foundation/FoundationContainer.h"
#include "Foundation/FoundationInteractionSettings.h"
#include "Foundation/FoundationRecipes.h"

#if WITH_DEV_AUTOMATION_TESTS
namespace
{
    constexpr EAutomationTestFlags RecipeTestFlags =
        EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter;

    // Pinned from the reference: Reference/minecraft-items-26.3/recipes.json resolved against today's items (86 Tier
    // A items and the imported blocks' items): 272 shaped and 41 shapeless recipes resolve, 125 of them fit 2x2 as the
    // jar writes them (the step 6 record, from C:\dev\sandbox\inv6\proof.txt), and 4 more once vanilla's pattern shrink
    // drops their blank edge rows and columns (7 shaped recipes have them). A new item or block import changes these.
    // Survival B1 (all 887 block items): C:\dev\sandbox\b1-blocks\scripts\recipe_counts.py recomputes these from the
    // reference (and the old 272/41/129 at 079fe3e).
    // Survival 20a (the 18 mob drops appended to the item reference): the same script gives 518/56/166 (515/56/164
    // before). Survival T1 8a2 (the 10 cooking outputs appended): the same script gives 531/69/180
    // (C:\dev\sandbox\inv8a2\recipe_counts.txt).
    constexpr int32 RecipeResolvedShaped = 531;
    constexpr int32 RecipeResolvedShapeless = 69;
    constexpr int32 RecipeFit2x2 = 180;

    const FFoundationRules& RecipeRules()
    {
        static FFoundationRules Rules = []
        {
            FFoundationRules Built;
            UFoundationInteractionSettings::Get().BuildRules(Built);
            return Built;
        }();
        return Rules;
    }

    // A recipe laid into a Size x Size grid at an offset (shaped; mirrored flips it left to right) or in order from
    // the first slot (shapeless; reversed from the last), one item of each ingredient (its first, or with bLast its
    // last).
    TArray<int32> RecipeGrid(
        const FFoundationRecipe& Recipe, int32 Size, int32 OffsetX, int32 OffsetY, bool bFlip, bool bLast = false)
    {
        TArray<int32> Grid;
        Grid.Init(INDEX_NONE, Size * Size);
        auto Item = [bLast](const TArray<int32>& Cell)
        {
            return Cell.IsEmpty() ? INDEX_NONE : bLast ? Cell.Last() : Cell[0];
        };
        if (Recipe.bShapeless)
            for (int32 Index = 0; Index < Recipe.Cells.Num(); ++Index)
                Grid[bFlip ? Size * Size - 1 - Index : Index] = Item(Recipe.Cells[Index]);
        else
            for (int32 Y = 0; Y < Recipe.Height; ++Y)
                for (int32 X = 0; X < Recipe.Width; ++X)
                    Grid[(OffsetY + Y) * Size + OffsetX + (bFlip ? Recipe.Width - 1 - X : X)] =
                        Item(Recipe.Cells[Y * Recipe.Width + X]);
        return Grid;
    }

    bool RecipeFits(const FFoundationRecipe& Recipe, int32 Size)
    {
        return Recipe.bShapeless ? Recipe.Cells.Num() <= Size * Size : Recipe.Width <= Size && Recipe.Height <= Size;
    }

    struct FRecipeLab
    {
        FFoundationAuthority A;
        FFoundationVitals Vitals;
        int32 Player = 0;
        uint32 Sequence = 1;

        FRecipeLab()
        {
            UFoundationInteractionSettings::Get().BuildRules(A.Rules);
            A.World.SetRegistry(FFoundationBlockRegistry::GetDefault());
            A.Options.bFullInventory = true;
            A.Options.bInfiniteSupply = false;
            Player = A.AddPlayer();
            A.Reset();
            // An empty inventory: the loadout gone.
            FFoundationInventory& I = Inv();
            for (int32 Slot = 0; Slot < FFoundationInventory::Slots; ++Slot)
                I.SetSlot(Slot, {});
        }
        FFoundationInventory& Inv() { return A.FindPlayer(Player)->Inventory; }
        int32 Item(const TCHAR* Id) const { return A.Rules.FindItem(Id); }
        FFoundationResult Click(int32 Slot, int32 Button, EFoundationClickType Type)
        {
            FFoundationCommand Command;
            Command.SessionId = A.GetSessionId();
            Command.Sequence = Sequence++;
            Command.Type = EFoundationCommandType::ContainerClick;
            Command.ContainerId = FFoundationContainerMenu::PlayerContainer;
            Command.Slot = Slot;
            Command.Button = Button;
            Command.ClickType = Type;
            return A.Execute(Player, Command, Vitals);
        }
        FFoundationResult Close()
        {
            FFoundationCommand Command;
            Command.SessionId = A.GetSessionId();
            Command.Sequence = Sequence++;
            Command.Type = EFoundationCommandType::ContainerClose;
            return A.Execute(Player, Command, Vitals);
        }
        FFoundationItemInstance Menu(int32 Slot) { return A.GetPlayerMenu(*A.FindPlayer(Player))[Slot]; }
        bool Holds(const FFoundationItemInstance& Stack, const TCHAR* Id, int32 Count) const
        {
            return Count == 0 ? Stack.IsEmpty() : !Stack.IsEmpty() && Stack.Item == Item(Id) && Stack.Count == Count;
        }
    };
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFoundationRecipeMatchTest, "Unnamed.Foundation.Recipes.Match", RecipeTestFlags)

bool FFoundationRecipeMatchTest::RunTest(const FString&)
{
    const FFoundationRecipeBook& Book = RecipeRules().Recipes;
    int32 Shaped = 0, Shapeless = 0, Fit2x2 = 0;
    for (const FFoundationRecipe& Recipe : Book.Recipes)
    {
        ++(Recipe.bShapeless ? Shapeless : Shaped);
        Fit2x2 += RecipeFits(Recipe, 2) ? 1 : 0;
    }
    TestEqual(TEXT("shaped recipes resolved"), Shaped, RecipeResolvedShaped);
    TestEqual(TEXT("shapeless recipes resolved"), Shapeless, RecipeResolvedShapeless);
    TestEqual(TEXT("recipes that fit 2x2"), Fit2x2, RecipeFit2x2);

    // Every recipe, from its own pattern, in every grid it fits (2x2 and 3x3), at every offset, as is and mirrored
    // (shapeless: in order and reversed), with the first and the last item of every ingredient. A grid another
    // recipe also matches (the first wins) is counted: there is none among today's items.
    int32 Checked = 0, Wrong = 0, Ambiguous = 0;
    for (int32 Index = 0; Index < Book.Recipes.Num(); ++Index)
    {
        const FFoundationRecipe& Recipe = Book.Recipes[Index];
        for (const int32 Size : {2, 3})
        {
            if (!RecipeFits(Recipe, Size)) continue;
            const int32 MaxX = Recipe.bShapeless ? 0 : Size - Recipe.Width;
            const int32 MaxY = Recipe.bShapeless ? 0 : Size - Recipe.Height;
            for (int32 Y = 0; Y <= MaxY; ++Y)
                for (int32 X = 0; X <= MaxX; ++X)
                    for (const bool bFlip : {false, true})
                        for (const bool bLast : {false, true})
                        {
                            const TArray<int32> Grid = RecipeGrid(Recipe, Size, X, Y, bFlip, bLast);
                            const int32 Found = Book.Match(Grid, Size, Size);
                            ++Checked;
                            if (Found == Index) continue;
                            const bool bOwn = Recipe.bShapeless
                                ? FFoundationRecipeBook::MatchesShapeless(Recipe, Grid)
                                : FFoundationRecipeBook::MatchesShaped(Recipe, Grid, Size, Size);
                            if (Found == INDEX_NONE || !bOwn)
                            {
                                if (++Wrong <= 5)
                                    AddError(FString::Printf(TEXT("%s does not match its own grid (%dx%d at %d,%d%s)"),
                                        *Recipe.Id.ToString(), Size, Size, X, Y,
                                        bFlip ? TEXT(", mirrored") : TEXT("")));
                            }
                            else if (++Ambiguous <= 5)
                                AddInfo(FString::Printf(TEXT("%s's grid makes %s first"), *Recipe.Id.ToString(),
                                    *Book.Recipes[Found].Id.ToString()));
                        }
        }
    }
    TestEqual(TEXT("recipes that miss their own grid"), Wrong, 0);
    TestEqual(TEXT("grids two recipes match"), Ambiguous, 0);
    TestTrue(TEXT("grids checked"), Checked > Book.Recipes.Num());
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFoundationRecipeNegativeTest, "Unnamed.Foundation.Recipes.Negative", RecipeTestFlags)

bool FFoundationRecipeNegativeTest::RunTest(const FString&)
{
    const FFoundationRules& Rules = RecipeRules();
    const int32 Planks = Rules.FindItem(TEXT("oak_planks")), Log = Rules.FindItem(TEXT("oak_log"));
    const int32 Stone = Rules.FindItem(TEXT("stone")), Dirt = Rules.FindItem(TEXT("dirt"));
    const int32 Stick = Rules.FindItem(TEXT("stick")), Table = Rules.FindItem(TEXT("crafting_table"));
    if (!TestTrue(TEXT("items exist"),
            Planks != INDEX_NONE && Log != INDEX_NONE && Stone != INDEX_NONE && Dirt != INDEX_NONE &&
                Stick != INDEX_NONE && Table != INDEX_NONE))
        return false;
    constexpr int32 N = INDEX_NONE;
    auto Makes = [&Rules](TArray<int32> Grid, int32 Size) { return Rules.Recipes.Craft(Grid, Size, Size); };
    TestTrue(TEXT("an empty grid makes nothing"), Makes({N, N, N, N}, 2).IsEmpty());
    const FFoundationRecipeResult Sticks = Makes({Planks, N, Planks, N}, 2);
    TestTrue(TEXT("two planks in a column make 4 sticks"), Sticks.Item == Stick && Sticks.Count == 4);
    // Vanilla 26.3's oak_pressure_plate (recipes.json) is two planks in a row; diagonal planks below match no recipe
    // (C:\dev\sandbox\b1-blocks\scripts\recipe_counts.py's probes).
    const FFoundationRecipeResult Plate = Makes({Planks, Planks, N, N}, 2);
    TestTrue(TEXT("two planks in a row make an oak pressure plate"),
        Plate.Item == Rules.FindItem(TEXT("oak_pressure_plate")) && Plate.Item != INDEX_NONE && Plate.Count == 1);
    TestTrue(TEXT("two planks diagonal make nothing"), Makes({Planks, N, N, Planks}, 2).IsEmpty());
    TestTrue(
        TEXT("planks with a gap row make nothing (3x3)"), Makes({Planks, N, N, N, N, N, Planks, N, N}, 3).IsEmpty());
    TestTrue(TEXT("stone in a column makes nothing"), Makes({Stone, N, Stone, N}, 2).IsEmpty());
    TestTrue(TEXT("three planks make nothing"), Makes({Planks, Planks, Planks, N}, 2).IsEmpty());
    const FFoundationRecipeResult Planked = Makes({N, N, N, Log}, 2);
    TestTrue(TEXT("a log anywhere makes 4 planks"), Planked.Item == Planks && Planked.Count == 4);
    TestTrue(TEXT("a log and dirt make nothing"), Makes({Log, Dirt, N, N}, 2).IsEmpty());
    TestTrue(TEXT("two logs make nothing"), Makes({Log, Log, N, N}, 2).IsEmpty());
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFoundationRecipeChainTest, "Unnamed.Foundation.Recipes.Chain", RecipeTestFlags)

bool FFoundationRecipeChainTest::RunTest(const FString&)
{
    // The pinned chain through container clicks: a log, 4 planks, a crafting table and 4 sticks.
    FRecipeLab L;
    FFoundationInventory& I = L.Inv();
    I.SetSlot(0, I.MakeStack(L.Item(TEXT("oak_log")), 2));
    constexpr int32 Result = FFoundationContainerMenu::ResultSlot;
    const int32 Hotbar0 = FFoundationContainerMenu::FirstHotbar;
    // Right-click places one log in the grid's last slot.
    L.Click(Hotbar0, 0, EFoundationClickType::Pickup);
    L.Click(4, 1, EFoundationClickType::Pickup);
    L.Click(Hotbar0, 0, EFoundationClickType::Pickup);
    TestTrue(TEXT("one log in the grid, one back in the hotbar"),
        L.Holds(L.Inv().Craft[3], TEXT("oak_log"), 1) && L.Holds(L.Inv().Hotbar[0], TEXT("oak_log"), 1) &&
            L.Inv().Carried.IsEmpty());
    TestTrue(TEXT("the result shows 4 oak planks"), L.Holds(L.Menu(Result), TEXT("oak_planks"), 4));
    const int32 Revision = L.Inv().Revision;
    // Right on the result takes one whole craft, never half (a count-4 result: Codex review 10, finding 3).
    TestTrue(TEXT("taking the result commits"), L.Click(Result, 1, EFoundationClickType::Pickup).IsCommitted());
    TestTrue(TEXT("4 planks on the cursor, the log used"),
        L.Holds(L.Inv().Carried, TEXT("oak_planks"), 4) && L.Inv().Craft[3].IsEmpty() && L.Menu(Result).IsEmpty());
    TestTrue(TEXT("a craft bumps the inventory revision"), L.Inv().Revision > Revision);
    // One plank in each grid slot: a crafting table.
    for (int32 Slot = 1; Slot <= 4; ++Slot)
        L.Click(Slot, 1, EFoundationClickType::Pickup);
    TestTrue(TEXT("the result shows a crafting table"), L.Holds(L.Menu(Result), TEXT("crafting_table"), 1));
    L.Click(Result, 0, EFoundationClickType::Pickup);
    TestTrue(TEXT("the crafting table on the cursor, the grid empty"),
        L.Holds(L.Inv().Carried, TEXT("crafting_table"), 1) && L.Inv().Craft[0].IsEmpty() &&
            L.Inv().Craft[1].IsEmpty() && L.Inv().Craft[2].IsEmpty() && L.Inv().Craft[3].IsEmpty());
    L.Click(FFoundationContainerMenu::FirstMain, 0, EFoundationClickType::Pickup);
    // The second log, then sticks from two planks in a column.
    L.Click(Hotbar0, 0, EFoundationClickType::Pickup);
    L.Click(1, 0, EFoundationClickType::Pickup);
    L.Click(Result, 0, EFoundationClickType::Pickup);
    L.Click(2, 1, EFoundationClickType::Pickup);
    L.Click(4, 1, EFoundationClickType::Pickup);
    TestTrue(TEXT("two planks in the right column make 4 sticks"), L.Holds(L.Menu(Result), TEXT("stick"), 4));
    // Planks on the cursor: the sticks cannot join them, so the click does nothing.
    const FFoundationResult Refused = L.Click(Result, 0, EFoundationClickType::Pickup);
    TestTrue(TEXT("another item on the cursor: no craft"),
        L.Holds(L.Inv().Carried, TEXT("oak_planks"), 2) && L.Holds(L.Inv().Craft[1], TEXT("oak_planks"), 1) &&
            L.Holds(L.Menu(Result), TEXT("stick"), 4));
    TestTrue(TEXT("that click is accepted and changes nothing"), Refused.IsCommitted());
    L.Click(FFoundationContainerMenu::FirstMain + 1, 0, EFoundationClickType::Pickup);
    L.Click(Result, 0, EFoundationClickType::Pickup);
    TestTrue(TEXT("4 sticks"), L.Holds(L.Inv().Carried, TEXT("stick"), 4));
    // A placed result: the result slot never takes the cursor stack.
    L.Click(1, 0, EFoundationClickType::Pickup);
    TestTrue(TEXT("sticks placed in the grid make nothing alone"),
        L.Holds(L.Inv().Craft[0], TEXT("stick"), 4) && L.Inv().Carried.IsEmpty());
    L.Click(1, 0, EFoundationClickType::Pickup);
    L.Click(Result, 0, EFoundationClickType::Pickup);
    TestTrue(TEXT("the result slot takes nothing"), L.Holds(L.Inv().Carried, TEXT("stick"), 4));
    TestTrue(TEXT("the crafting table and the planks are in the main grid"),
        L.Holds(L.Inv().Main[0], TEXT("crafting_table"), 1) && L.Holds(L.Inv().Main[1], TEXT("oak_planks"), 2));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFoundationRecipeShiftTest, "Unnamed.Foundation.Recipes.ShiftCraft", RecipeTestFlags)

bool FFoundationRecipeShiftTest::RunTest(const FString&)
{
    // Shift on the result crafts while all of it fits, into the hotbar from its last slot (provisional order).
    FRecipeLab L;
    FFoundationInventory& I = L.Inv();
    I.Craft[0] = I.MakeStack(L.Item(TEXT("oak_log")), 20);
    constexpr int32 Result = FFoundationContainerMenu::ResultSlot;
    L.Click(Result, 0, EFoundationClickType::QuickMove);
    TestTrue(TEXT("20 logs: 80 planks, 64 in hotbar 8 and 16 in hotbar 7"),
        L.Holds(L.Inv().Hotbar[8], TEXT("oak_planks"), 64) && L.Holds(L.Inv().Hotbar[7], TEXT("oak_planks"), 16) &&
            L.Inv().Craft[0].IsEmpty());
    // No room for a whole result: nothing is crafted.
    FFoundationInventory& Full = L.Inv();
    for (int32 Slot = 0; Slot < FFoundationInventory::OffhandSlot; ++Slot)
        if (Slot < FFoundationInventory::FirstArmor) Full.SetSlot(Slot, Full.MakeStack(L.Item(TEXT("dirt")), 64));
    Full.SetSlot(0, Full.MakeStack(L.Item(TEXT("oak_planks")), 61));
    Full.Craft[0] = Full.MakeStack(L.Item(TEXT("oak_log")), 2);
    L.Click(Result, 0, EFoundationClickType::QuickMove);
    TestTrue(TEXT("3 of room for 4 planks: no craft"),
        L.Holds(L.Inv().Craft[0], TEXT("oak_log"), 2) && L.Holds(L.Inv().Hotbar[0], TEXT("oak_planks"), 61));
    // A number key crafts into an empty hotbar slot only.
    L.Inv().SetSlot(4, {});
    L.Click(Result, 4, EFoundationClickType::Swap);
    TestTrue(TEXT("key 5 crafts into hotbar 4"),
        L.Holds(L.Inv().Hotbar[4], TEXT("oak_planks"), 4) && L.Holds(L.Inv().Craft[0], TEXT("oak_log"), 1));
    L.Click(Result, 4, EFoundationClickType::Swap);
    TestTrue(TEXT("not into a full slot"), L.Holds(L.Inv().Craft[0], TEXT("oak_log"), 1));
    // Q crafts and throws: the throw is T2's, so the click is refused and nothing changes.
    const FFoundationResult Thrown = L.Click(Result, 0, EFoundationClickType::Throw);
    TestTrue(TEXT("Q on the result waits for the throw"),
        Thrown.Reason == EFoundationReason::DropDeferred && L.Holds(L.Inv().Craft[0], TEXT("oak_log"), 1));
    // A shift-click on a grid slot moves it to the main grid and hotbar.
    L.Inv().SetSlot(9, {});
    L.Click(1, 0, EFoundationClickType::QuickMove);
    TestTrue(TEXT("shift on the grid moves to the inventory"),
        L.Inv().Craft[0].IsEmpty() && L.Holds(L.Inv().Main[0], TEXT("oak_log"), 1));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFoundationRecipeCloseTest, "Unnamed.Foundation.Recipes.Close", RecipeTestFlags)

bool FFoundationRecipeCloseTest::RunTest(const FString&)
{
    // Closing returns the cursor, then the grid in slot order, keeping identities.
    FRecipeLab L;
    FFoundationInventory& I = L.Inv();
    I.Craft[1] = I.MakeStack(L.Item(TEXT("oak_planks")), 3);
    I.Craft[2] = I.MakeStack(L.Item(TEXT("oak_log")), 1);
    I.Carried = I.MakeStack(L.Item(TEXT("stone")), 5);
    const int32 Planks = I.Craft[1].InstanceId;
    TestTrue(TEXT("close commits"), L.Close().IsCommitted());
    TestTrue(TEXT("cursor, then grid slots, into the hotbar"),
        L.Holds(L.Inv().Hotbar[0], TEXT("stone"), 5) && L.Holds(L.Inv().Hotbar[1], TEXT("oak_planks"), 3) &&
            L.Holds(L.Inv().Hotbar[2], TEXT("oak_log"), 1) && L.Inv().Hotbar[1].InstanceId == Planks);
    TestTrue(TEXT("the grid and cursor are empty"),
        L.Inv().Carried.IsEmpty() &&
            !L.Inv().Craft.ContainsByPredicate([](const FFoundationItemInstance& S) { return !S.IsEmpty(); }));
    // No room, foundation.Items.Entities 0: the close is refused and nothing moves.
    L.A.Options.bItemEntities = false;
    for (int32 Slot = 0; Slot < FFoundationInventory::FirstArmor; ++Slot)
        L.Inv().SetSlot(Slot, L.Inv().MakeStack(L.Item(TEXT("dirt")), 64));
    L.Inv().Craft[0] = L.Inv().MakeStack(L.Item(TEXT("oak_log")), 1);
    const FFoundationResult Refused = L.Close();
    TestTrue(TEXT("a grid without room waits for the throw"),
        Refused.Reason == EFoundationReason::DropDeferred && L.Holds(L.Inv().Craft[0], TEXT("oak_log"), 1));
    // At 1 (items fixes): the cursor and each grid stack that does not fit are thrown, one entity each.
    L.A.Options.bItemEntities = true;
    L.Inv().Craft[3] = L.Inv().MakeStack(L.Item(TEXT("oak_planks")), 2);
    L.Inv().Carried = L.Inv().MakeStack(L.Item(TEXT("stone")), 4);
    const TArray<FFoundationItemEntity>& Thrown = L.A.GetItemEntities();
    TestTrue(TEXT("switch 1: a grid without room is thrown"),
        L.Close().IsCommitted() && L.Inv().Carried.IsEmpty() &&
            !L.Inv().Craft.ContainsByPredicate([](const FFoundationItemInstance& S) { return !S.IsEmpty(); }) &&
            Thrown.Num() == 3 && Thrown[0].Item == L.Item(TEXT("stone")) && Thrown[0].Count == 4 &&
            Thrown[1].Item == L.Item(TEXT("oak_log")) && Thrown[1].Count == 1 &&
            Thrown[2].Item == L.Item(TEXT("oak_planks")) && Thrown[2].Count == 2 && Thrown[2].PickupDelay == 40);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFoundationRecipeSwitchTest, "Unnamed.Foundation.Recipes.Switch", RecipeTestFlags)

bool FFoundationRecipeSwitchTest::RunTest(const FString&)
{
    // foundation.Crafting 0 restores step 5b's menu (Codex review 10, finding 1): the grid and the result are no slots,
    // so clicks on them are refused and change nothing; what the grid already holds still comes back on close.
    FRecipeLab L;
    FFoundationInventory& I = L.Inv();
    I.SetSlot(0, I.MakeStack(L.Item(TEXT("oak_log")), 2));
    L.Click(FFoundationContainerMenu::FirstHotbar, 0, EFoundationClickType::Pickup);
    L.Click(1, 1, EFoundationClickType::Pickup);
    TestTrue(TEXT("on: one log in the grid"), L.Holds(L.Inv().Craft[0], TEXT("oak_log"), 1));
    L.A.Options.bCrafting = false;
    TestTrue(TEXT("off: a click on the grid is refused"),
        L.Click(2, 0, EFoundationClickType::Pickup).Reason == EFoundationReason::InvalidSlot);
    TestTrue(TEXT("off: the grid takes nothing"),
        L.Inv().Craft[1].IsEmpty() && L.Holds(L.Inv().Carried, TEXT("oak_log"), 1));
    TestTrue(TEXT("off: no result"), L.Menu(FFoundationContainerMenu::ResultSlot).IsEmpty());
    TestTrue(TEXT("off: a click on the result is refused"),
        L.Click(FFoundationContainerMenu::ResultSlot, 0, EFoundationClickType::Pickup).Reason ==
            EFoundationReason::InvalidSlot);
    TestTrue(TEXT("off: a shift-click on the grid is refused"),
        L.Click(1, 0, EFoundationClickType::QuickMove).Reason == EFoundationReason::InvalidSlot);
    L.A.Options.bCrafting = true;
    TestTrue(TEXT("on again: the result shows"),
        L.Holds(L.Menu(FFoundationContainerMenu::ResultSlot), TEXT("oak_planks"), 4));
    L.A.Options.bCrafting = false;
    TestTrue(TEXT("off: close commits"), L.Close().IsCommitted());
    TestTrue(TEXT("off: the grid's log and the cursor's come back"),
        L.Inv().Craft[0].IsEmpty() && L.Inv().Carried.IsEmpty() && L.Holds(L.Inv().Hotbar[0], TEXT("oak_log"), 2));
    return true;
}
#endif
