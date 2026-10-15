#if WITH_DEV_AUTOMATION_TESTS
#include "Dom/JsonObject.h"
#include "Foundation/FoundationBlockRegistry.h"
#include "Foundation/FoundationInteractionSettings.h"
#include "Foundation/FoundationItemAssets.h"
#include "Hud/HudAssets.h"
#include "Hud/HudCreative.h"
#include "Hud/HudDrawList.h"
#include "Hud/HudModel.h"
#include "Hud/HudPresenter.h"
#include "Misc/AutomationTest.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "UnnamedProjectSettings.h"

namespace
{
    constexpr EAutomationTestFlags HudItemTestFlags =
        EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter;

    TSharedPtr<FJsonObject> HudItemReadFacts(const TCHAR* Name)
    {
        FString Text;
        TSharedPtr<FJsonObject> Root;
        if (FFileHelper::LoadFileToString(
                Text, *(FPaths::ProjectDir() / TEXT("Reference/minecraft-items-26.3") / Name)))
            FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Text), Root);
        return Root;
    }

    // The black-box facts' colour names (TextColor.serialize) as minecraft.wiki "Formatting codes" gives them.
    FColor HudItemNamedColor(const FString& Name)
    {
        return Name == TEXT("white") ? HudTooltip::White
            : Name == TEXT("gray")   ? HudTooltip::Gray
            : Name == TEXT("blue")   ? HudTooltip::Blue
            : Name == TEXT("red")    ? HudTooltip::Red
            : Name == TEXT("yellow") ? HudTooltip::Yellow
                                     : FColor::Magenta;
    }
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHudItemsDurabilityBarTest, "Unnamed.Hud.Items.DurabilityBar", HudItemTestFlags)

bool FHudItemsDurabilityBarTest::RunTest(const FString&)
{
    // Every damage value of every max damage, against the server jar's own answers (ItemFacts.java).
    const TSharedPtr<FJsonObject> Facts = HudItemReadFacts(TEXT("facts/durability_bar.json"));
    if (!TestTrue(TEXT("facts/durability_bar.json"), Facts.IsValid())) return false;
    TMap<int32, TArray<FHudDurabilityBar>> Table;
    int32 Values = 0, Mismatches = 0;
    for (const TSharedPtr<FJsonValue>& Entry : Facts->GetArrayField(TEXT("maxima")))
    {
        const TSharedPtr<FJsonObject> Max = Entry->AsObject();
        const int32 MaxDamage = int32(Max->GetNumberField(TEXT("max_damage")));
        const TArray<TSharedPtr<FJsonValue>>& Visible = Max->GetArrayField(TEXT("visible"));
        const TArray<TSharedPtr<FJsonValue>>& Width = Max->GetArrayField(TEXT("width"));
        const TArray<TSharedPtr<FJsonValue>>& Color = Max->GetArrayField(TEXT("color"));
        if (!TestTrue(FString::Printf(TEXT("max %d: every damage value"), MaxDamage),
                Visible.Num() == MaxDamage + 1 && Width.Num() == MaxDamage + 1 && Color.Num() == MaxDamage + 1))
            continue;
        TArray<FHudDurabilityBar>& Rows = Table.Add(MaxDamage);
        for (int32 Damage = 0; Damage <= MaxDamage; ++Damage)
        {
            FHudDurabilityBar Expected;
            Expected.bVisible = Visible[Damage]->AsNumber() != 0;
            Expected.Width = int32(Width[Damage]->AsNumber());
            Expected.Color = FColor::FromHex(Color[Damage]->AsString());
            Expected.Color.A = 255;
            Rows.Add(Expected);
            // A hidden bar has no width or colour of its own here (the game still answers both): only its visibility
            // is compared.
            FHudDurabilityBar Got = HudModel::GetDurabilityBar(Damage, MaxDamage);
            if (!Expected.bVisible && !Got.bVisible)
            {
                Got.Width = Expected.Width;
                Got.Color = Expected.Color;
            }
            ++Values;
            if (!(Got == Expected) && ++Mismatches <= 5)
                AddError(FString::Printf(TEXT("max %d damage %d: visible %d width %d colour %s, expected %d %d %s"),
                    MaxDamage, Damage, Got.bVisible, Got.Width, *Got.Color.ToHex(), Expected.bVisible, Expected.Width,
                    *Expected.Color.ToHex()));
        }
    }
    // 31 max damage values since T2 19a2 added the armor pieces (7 and 4,261 before; facts/durability_bar.json).
    TestEqual(TEXT("31 max damage values, 10,335 damage values"), Values, 10335);
    TestEqual(TEXT("table mismatches"), Mismatches, 0);
    TestFalse(TEXT("an item that never wears has no bar"), HudModel::GetDurabilityBar(5, 0).bVisible);
    TestEqual(TEXT("damage past the max: no fill"), HudModel::GetDurabilityBar(300, 250).Width, 0);

    // The hotbar's pixels: the black 13 x 2 rectangle at the item's (2, 13) and the 1 px fill.
    const UHudAssets* Assets = LoadObject<UHudAssets>(nullptr, *UUnnamedProjectSettings::GetHudRegistryPath());
    if (!TestNotNull(TEXT("HUD registry"), Assets) || !TestTrue(TEXT("iron's table"), Table.Contains(250)))
        return false;
    FHudSnapshot S;
    S.Items = EHudDataSource::Authoritative;
    S.Counts.Init(1, 9);
    S.ItemViews.SetNum(9);
    const int32 Damages[] = {0, 1, 62, 125, 187, 249, 250, 25, 0};
    for (int32 Slot = 0; Slot < 9; ++Slot)
    {
        S.ItemViews[Slot].Icon = EHudItemIcon::Sprite;
        S.ItemViews[Slot].Sprite = TEXT("item/iron_pickaxe");
        S.ItemViews[Slot].Damage = Damages[Slot];
        S.ItemViews[Slot].MaxDamage = Slot == 8 ? 0 : 250;
    }
    FHudPresentation P;
    P.Update(S, 0, 0);
    const FHudLayout L = HudModel::GetLayout({640, 360}, 0, false);
    const TArray<FHudDraw> Draws = HudDrawList::Build(S, P, L, 0, Assets->Glyphs);
    for (int32 Slot = 0; Slot < 9; ++Slot)
    {
        const FIntPoint Bar = L.GetItem(Slot) + FIntPoint(2, 13);
        const FHudDraw* Back = Draws.FindByPredicate(
            [Bar](const FHudDraw& D) { return D.Sprite.IsNone() && D.Rect == FIntRect(Bar, Bar + FIntPoint(13, 2)); });
        const FHudDraw* Fill = Draws.FindByPredicate(
            [Bar](const FHudDraw& D) { return D.Sprite.IsNone() && D.Rect.Min == Bar && D.Rect.Height() == 1; });
        const FHudDurabilityBar& Expected = Table[250][Damages[Slot]];
        const FString What =
            FString::Printf(TEXT("slot %d, damage %d of %d: "), Slot, Damages[Slot], S.ItemViews[Slot].MaxDamage);
        if (Slot == 8 || !Expected.bVisible)
        {
            TestTrue(What + TEXT("no bar"), !Back && !Fill);
            continue;
        }
        TestTrue(What + TEXT("black back"), Back && Back->Color == FLinearColor::Black);
        if (Expected.Width == 0)
            TestNull(*(What + TEXT("no fill at zero width")), Fill);
        else
            TestTrue(What + TEXT("fill width and colour"),
                Fill && Fill->Rect.Width() == Expected.Width && Fill->Color == FLinearColor(Expected.Color));
    }
    // Spot values from the table (iron, 250): half worn is 7 px of yellow, one hit from breaking 0 px.
    TestTrue(TEXT("iron at 125: 7 px"), Table[250][125].Width == 7 && Table[250][249].Width == 0);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHudItemsIconsTest, "Unnamed.Hud.Items.Icons", HudItemTestFlags)

bool FHudItemsIconsTest::RunTest(const FString&)
{
    // The hotbar's icon choice per kind: a block item draws its 3D icon, a 26.3 item its GUI sprite, an item with
    // neither the token; damage and max damage reach the view.
    FFoundationRules Rules;
    UFoundationInteractionSettings::Get().BuildRules(Rules);
    FFoundationItemDefinition Bare;
    Bare.Id = TEXT("hud_items_test_bare");
    Bare.DisplayName = TEXT("Bare");
    const int32 BareIndex = Rules.Items.Add(Bare);
    const TCHAR* Ids[] = {TEXT("stone"), TEXT("iron_pickaxe"), TEXT("iron_sword"), TEXT("stick"), TEXT("iron_spear"),
        TEXT("water_bucket"), TEXT("apple")};
    FFoundationInventory Inventory;
    Inventory.Hotbar.SetNum(FFoundationInventory::HotbarSlots);
    for (int32 Slot = 0; Slot < UE_ARRAY_COUNT(Ids); ++Slot)
    {
        const int32 Item = Rules.FindItem(Ids[Slot]);
        if (!TestTrue(FString(TEXT("item ")) + Ids[Slot], Item != INDEX_NONE)) return false;
        Inventory.Hotbar[Slot] = {Slot + 1, Item, 1, Slot == 1 ? 125 : 0};
    }
    Inventory.Hotbar[7] = {8, BareIndex, 1, 0};
    FHudSnapshot S;
    UHudPresenter::ApplyInventory(S, Inventory, Rules, &FFoundationBlockRegistry::GetDefault().Get());
    TestTrue(TEXT("stone: its 3D icon"),
        S.ItemViews[0].Icon == EHudItemIcon::Block && S.ItemViews[0].Block == TEXT("stone") &&
            S.ItemViews[0].Sprite.IsNone());
    for (int32 Slot = 1; Slot < UE_ARRAY_COUNT(Ids); ++Slot)
        TestTrue(FString(TEXT("sprite of ")) + Ids[Slot],
            S.ItemViews[Slot].Icon == EHudItemIcon::Sprite &&
                S.ItemViews[Slot].Sprite == FName(FString(TEXT("item/")) + Ids[Slot]) &&
                S.ItemViews[Slot].Block.IsNone());
    TestTrue(
        TEXT("no sprite: the token"), S.ItemViews[7].Icon == EHudItemIcon::Token && S.ItemViews[7].Sprite.IsNone());
    TestTrue(TEXT("damage and max damage"),
        S.ItemViews[1].Damage == 125 && S.ItemViews[1].MaxDamage == 250 && S.ItemViews[3].MaxDamage == 0);
    TestTrue(TEXT("empty slot"), S.Counts[8] == 0 && S.ItemViews[8].Icon == EHudItemIcon::Token);

    // Drawn as the 16 x 16 sprite, untinted, on the item's corner.
    const UHudAssets* Assets = LoadObject<UHudAssets>(nullptr, *UUnnamedProjectSettings::GetHudRegistryPath());
    if (!TestNotNull(TEXT("HUD registry"), Assets)) return false;
    FHudPresentation P;
    P.Update(S, 0, 0);
    const FHudLayout L = HudModel::GetLayout({640, 360}, 0, false);
    const TArray<FHudDraw> Draws = HudDrawList::Build(S, P, L, 0, Assets->Glyphs);
    const FIntPoint At = L.GetItem(1);
    TestTrue(TEXT("pickaxe sprite drawn"),
        Draws.ContainsByPredicate(
            [At](const FHudDraw& D)
            {
                return D.Sprite == TEXT("item/iron_pickaxe") && D.Rect == FIntRect(At, At + FIntPoint(16, 16)) &&
                    D.Color == FLinearColor::White;
            }));
    TestTrue(TEXT("stone as a block icon"),
        Draws.ContainsByPredicate([](const FHudDraw& D) { return D.Block == TEXT("stone"); }));
    // Every sprite the views name has a GUI texture (the presenter loads one brush per DA_Items sprite).
    const UFoundationItemAssets* Items = UFoundationItemAssets::LoadDefault();
    if (TestNotNull(TEXT("DA_Items"), Items))
        for (const FFoundationItemAsset& Item : Items->Items)
            if (TestFalse(TEXT("sprites of ") + Item.Id.ToString(), Item.Textures.IsEmpty()))
                TestNotNull(*(TEXT("GUI texture ") + Item.Textures[0].ToString()),
                    UUnnamedProjectSettings::GetItemAssetPath(TEXT("Gui/") + Item.Textures[0].ToString()).TryLoad());
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHudItemsTooltipsTest, "Unnamed.Hud.Items.Tooltips", HudItemTestFlags)

bool FHudItemsTooltipsTest::RunTest(const FString&)
{
    const UFoundationItemAssets* Items = UFoundationItemAssets::LoadDefault();
    const TSharedPtr<FJsonObject> Facts = HudItemReadFacts(TEXT("facts/tooltips.json"));
    if (!TestTrue(TEXT("DA_Items and facts/tooltips.json"), Items && Facts)) return false;
    // Without a player, every item's lines match the server jar's getTooltipLines (ItemFacts.java), runs joined.
    int32 Checked = 0;
    for (const TSharedPtr<FJsonValue>& Entry : Facts->GetArrayField(TEXT("items")))
    {
        const TSharedPtr<FJsonObject> Tip = Entry->AsObject();
        const FFoundationItemAsset* Item = Items->FindItem(*Tip->GetStringField(TEXT("id")));
        if (!TestNotNull(*(TEXT("item ") + Tip->GetStringField(TEXT("id"))), Item)) continue;
        TArray<FHudTooltipLine> Expected;
        for (const TSharedPtr<FJsonValue>& Line : Tip->GetArrayField(TEXT("lines")))
        {
            FHudTooltipLine& Out = Expected.AddDefaulted_GetRef();
            for (const TSharedPtr<FJsonValue>& Run : Line->AsArray())
            {
                Out.Text += Run->AsArray()[0]->AsString();
                Out.Color = HudItemNamedColor(Run->AsArray()[1]->AsString());
            }
        }
        TArray<FHudTooltipLine> Got = {{Item->DisplayName, HudTooltip::GetNameColor(Item->Rarity)}};
        Got.Append(HudTooltip::GetModifierLines(Item->Modifiers, Items->Lang, false));
        ++Checked;
        if (!TestTrue(TEXT("no-player lines of ") + Item->Id.ToString(), Got == Expected))
            for (const FHudTooltipLine& Line : Got)
                AddInfo(FString::Printf(TEXT("  got \"%s\" %s"), *Line.Text, *Line.Color.ToHex()));
    }
    TestEqual(TEXT("143 tooltips"), Checked, 143);

    // With a player (the game's case): the base modifiers add its base values, in dark green after a space.
    auto Lines = [Items](const TCHAR* Id)
    {
        const FFoundationItemAsset* Item = Items->FindItem(Id);
        return Item ? HudTooltip::GetModifierLines(Item->Modifiers, Items->Lang, true) : TArray<FHudTooltipLine>();
    };
    const FHudTooltipLine Blank{FString(), HudTooltip::White}, Header{TEXT("When in Main Hand:"), HudTooltip::Gray};
    TestTrue(TEXT("iron pickaxe"),
        Lines(TEXT("iron_pickaxe")) ==
            TArray<FHudTooltipLine>{Blank, Header, {TEXT(" 4 Attack Damage"), HudTooltip::DarkGreen},
                {TEXT(" 1.2 Attack Speed"), HudTooltip::DarkGreen}});
    TestTrue(TEXT("netherite sword"),
        Lines(TEXT("netherite_sword")) ==
            TArray<FHudTooltipLine>{Blank, Header, {TEXT(" 8 Attack Damage"), HudTooltip::DarkGreen},
                {TEXT(" 1.6 Attack Speed"), HudTooltip::DarkGreen}});
    TestTrue(TEXT("diamond hoe: zero amounts still show the base values"),
        Lines(TEXT("diamond_hoe")) ==
            TArray<FHudTooltipLine>{Blank, Header, {TEXT(" 1 Attack Damage"), HudTooltip::DarkGreen},
                {TEXT(" 4 Attack Speed"), HudTooltip::DarkGreen}});
    TestTrue(TEXT("iron spear: two decimals"),
        Lines(TEXT("iron_spear")).Num() == 4 && Lines(TEXT("iron_spear"))[3].Text == TEXT(" 1.05 Attack Speed"));
    TestTrue(TEXT("stick: no lines"), Lines(TEXT("stick")).IsEmpty());
    TestTrue(TEXT("amounts"),
        HudTooltip::FormatAmount(4) == TEXT("4") && HudTooltip::FormatAmount(1.2) == TEXT("1.2") &&
            HudTooltip::FormatAmount(0.869565) == TEXT("0.87") && HudTooltip::FormatAmount(10.125) == TEXT("10.12") &&
            HudTooltip::FormatAmount(10.375) == TEXT("10.38"));
    // Layout (known issue: not yet confirmed by a vanilla capture): 10 px per line, 2 more under the name.
    TestTrue(TEXT("line offsets and heights"),
        HudTooltip::GetLineY(0) == 0 && HudTooltip::GetLineY(1) == 12 && HudTooltip::GetLineY(3) == 32 &&
            HudTooltip::GetHeight(1) == 8 && HudTooltip::GetHeight(5) == 50);
    return true;
}
#endif
