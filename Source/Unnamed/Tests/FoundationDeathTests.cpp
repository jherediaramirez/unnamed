#include "Misc/AutomationTest.h"
#include "Dom/JsonObject.h"
#include "Foundation/FoundationAuthority.h"
#include "Foundation/FoundationBlockRegistry.h"
#include "Foundation/FoundationDeath.h"
#include "Foundation/FoundationInteractionSettings.h"
#include "Foundation/FoundationPawn.h"
#include "Foundation/FoundationVitals.h"
#include "Hud/HudDrawList.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Serialization/JsonReader.h"

#if WITH_DEV_AUTOMATION_TESTS
// Survival step 11a: death. Values are literals from the survival findings (section "R6 Hurt and death"), the value
// report and en_us.json (evidence ids in the comments), never the constants under test.
namespace DeathTestsLocal
{
    // The project's rules and blocks, one survival player with the full inventory and finite supply.
    struct FDeathLab
    {
        FFoundationAuthority A;
        int32 Player = 0;

        FDeathLab()
        {
            UFoundationInteractionSettings::Get().BuildRules(A.Rules);
            A.World.SetRegistry(FFoundationBlockRegistry::Build(
                FFoundationBlockRegistry::GetDefault()->GetAssets(), TArray<FFoundationBlockDefinition>()));
            A.World.BoundsMin = FIntVector(-20, -20, 0);
            A.World.BoundsMax = FIntVector(19, 19, 15);
            A.Options.bInfiniteSupply = false;
            A.Options.bFullInventory = true;
            A.Options.bItemEntities = true;
            A.Options.bExperienceOrbs = true;
            Player = A.AddPlayer();
            A.Reset();
            FFoundationPlayerFacts Facts;
            Facts.FeetCm = FVector(100, 200, 0);
            Facts.EyeCm = Facts.FeetCm + FVector(0, 0, 162);
            Facts.Step = 1;
            A.SetFacts(Player, Facts);
        }

        FFoundationPlayer& P() { return *A.FindPlayer(Player); }
    };
}
using namespace DeathTestsLocal;

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFoundationDeathMessagesTest, "Unnamed.Foundation.Death.Messages",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FFoundationDeathMessagesTest::RunTest(const FString& Parameters)
{
    // Every cause's message is the 26.3 jar's en_us.json key and text, verbatim.
    const FString Path = FPaths::ProjectDir() / TEXT("Reference/minecraft-menu/assets/minecraft/lang/en_us.json");
    FString Json;
    TSharedPtr<FJsonObject> Lang;
    if (!TestTrue(TEXT("en_us.json reads"), FFileHelper::LoadFileToString(Json, *Path)) ||
        !TestTrue(TEXT("en_us.json parses"),
            FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Json), Lang) && Lang.IsValid()))
        return false;
    const auto Pin = [&](const FFoundationVitals::FDeathText& Text)
    {
        FString Vanilla;
        if (TestTrue(
                FString::Printf(TEXT("%s is a vanilla key"), Text.Key), Lang->TryGetStringField(Text.Key, Vanilla)))
            TestEqual(Text.Key, FString(Text.Template), Vanilla);
    };
    const TPair<EFoundationDamage, const TCHAR*> Causes[] = {
        {EFoundationDamage::Generic, TEXT("death.attack.generic")},
        {EFoundationDamage::MobAttack, TEXT("death.attack.mob")},
        {EFoundationDamage::Fall, TEXT("death.attack.fall")},
        {EFoundationDamage::Drown, TEXT("death.attack.drown")},
        {EFoundationDamage::Starve, TEXT("death.attack.starve")},
        {EFoundationDamage::Kill, TEXT("death.attack.genericKill")},
        {EFoundationDamage::PlayerAttack, TEXT("death.attack.player")},
        {EFoundationDamage::Lava, TEXT("death.attack.lava")},
        {EFoundationDamage::OnFire, TEXT("death.attack.onFire")},
    };
    for (const TPair<EFoundationDamage, const TCHAR*>& Cause : Causes)
    {
        const FFoundationVitals::FDeathText Text = FFoundationVitals::GetCauseText(Cause.Key);
        TestEqual(FString::Printf(TEXT("cause %d's key"), int32(Cause.Key)), FString(Text.Key), FString(Cause.Value));
        Pin(Text);
    }
    // minecraft.wiki "Death messages": more than 5 blocks fell from a high place, else hit the ground too hard.
    const FFoundationVitals::FDeathText High = FFoundationVitals::GetDeathText(EFoundationDamage::Fall, 600);
    TestEqual(TEXT("a 6-block fall"), FString(High.Key), FString(TEXT("death.fell.accident.generic")));
    Pin(High);
    TestEqual(TEXT("a 4-block fall"), FString(FFoundationVitals::GetDeathText(EFoundationDamage::Fall, 400).Key),
        FString(TEXT("death.attack.fall")));
    // No attacker name: mob and player attacks read the generic message.
    TestEqual(TEXT("a nameless mob"), FString(FFoundationVitals::GetDeathText(EFoundationDamage::MobAttack, 0).Key),
        FString(TEXT("death.attack.generic")));
    TestEqual(TEXT("a named mob"),
        FFoundationVitals::FormatDeathMessage(
            FFoundationVitals::GetDeathText(EFoundationDamage::MobAttack, 0, TEXT("Zombie")), TEXT("Player"),
            TEXT("Zombie")),
        FString(TEXT("Player was slain by Zombie")));

    // A death fills the player's name and keeps the score (the experience held at death) past the respawn.
    FFoundationVitals Vitals;
    Vitals.AddExperience(30);
    Vitals.FallDistanceCm = 800;
    Vitals.Hurt(EFoundationDamage::Fall, 100);
    TestTrue(TEXT("dead"), Vitals.IsDead());
    TestEqual(TEXT("the fall's message"), Vitals.DeathMessage, FString(TEXT("Player fell from a high place")));
    TestEqual(TEXT("the score"), Vitals.DeathScore, 30);
    Vitals.Respawn();
    TestEqual(TEXT("the score after the respawn"), Vitals.DeathScore, 30);
    TestEqual(TEXT("experience after the respawn"), Vitals.TotalExperience, 0);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFoundationDeathDropsTest, "Unnamed.Foundation.Death.Drops",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FFoundationDeathDropsTest::RunTest(const FString& Parameters)
{
    FDeathLab L;
    FFoundationInventory& Inventory = L.P().Inventory;
    Inventory.bFull = true;
    const int32 Block = L.A.Rules.FindItem(TEXT("stone"));
    const int32 Pickaxe = L.A.Rules.FindItem(TEXT("iron_pickaxe"));
    if (!TestTrue(TEXT("items exist"), Block != INDEX_NONE && Pickaxe != INDEX_NONE)) return false;
    // All 41 slots and the carried stack.
    for (int32 Slot = 0; Slot < FFoundationInventory::Slots; ++Slot)
        Inventory.SetSlot(Slot, Inventory.MakeStack(Slot % 2 ? Pickaxe : Block, Slot % 2 ? 1 : 1 + Slot));
    Inventory.Carried = Inventory.MakeStack(Block, 7);
    int32 Stacks = 1, Items = 7;
    for (int32 Slot = 0; Slot < FFoundationInventory::Slots; ++Slot)
        if (const FFoundationItemInstance* Stack = Inventory.GetSlot(Slot); Stack && !Stack->IsEmpty())
        {
            ++Stacks;
            Items += Stack->Count;
        }
    TestEqual(TEXT("a full inventory"), Stacks, 42);
    const int32 Before = L.A.GetItemEntities().Num();
    const FVector Feet(100, 200, 0);
    L.A.DropInventoryOnDeath(L.Player, Feet, 1234);

    // [survival.r6.death_items] one entity per stack, pickup delay 40, 0.2 blocks per tick up; [items.drop.
    // random_scatter] at most 0.5 blocks per tick sideways (cm per tick: x 100).
    const TArray<FFoundationItemEntity>& Entities = L.A.GetItemEntities();
    TestEqual(TEXT("one entity per stack"), Entities.Num() - Before, Stacks);
    int32 Dropped = 0;
    bool bScattered = false;
    for (int32 Index = Before; Index < Entities.Num(); ++Index)
    {
        const FFoundationItemEntity& Entity = Entities[Index];
        Dropped += Entity.Count;
        TestEqual(TEXT("pickup delay"), Entity.PickupDelay, 40);
        TestEqual(TEXT("upward speed"), Entity.VelocityCm.Z, 20.);
        TestTrue(TEXT("sideways at most 0.5 b/t"), Entity.VelocityCm.Size2D() <= 50. + 1e-6);
        bScattered |= Entity.VelocityCm.Size2D() > 1.;
        // From 0.3 blocks below the 1.62-block eye.
        TestEqual(TEXT("spawn height"), Entity.LocalCm.Z, Feet.Z + 132., 1e-6);
    }
    TestTrue(TEXT("scattered sideways"), bScattered);
    TestEqual(TEXT("every item dropped"), Dropped, Items);
    for (int32 Slot = 0; Slot < FFoundationInventory::Slots; ++Slot)
        TestTrue(FString::Printf(TEXT("slot %d empty"), Slot), Inventory.GetSlot(Slot)->IsEmpty());
    TestTrue(TEXT("carried empty"), Inventory.Carried.IsEmpty());

    // The same seed throws the same way; infinite supply drops nothing.
    FDeathLab Again;
    Again.P().Inventory.bFull = true;
    for (int32 Slot = 0; Slot < FFoundationInventory::Slots; ++Slot)
        Again.P().Inventory.SetSlot(
            Slot, Again.P().Inventory.MakeStack(Slot % 2 ? Pickaxe : Block, Slot % 2 ? 1 : 1 + Slot));
    Again.P().Inventory.Carried = Again.P().Inventory.MakeStack(Block, 7);
    const int32 AgainBefore = Again.A.GetItemEntities().Num();
    Again.A.DropInventoryOnDeath(Again.Player, Feet, 1234);
    TestEqual(
        TEXT("first velocity repeats"), Again.A.GetItemEntities()[AgainBefore].VelocityCm, Entities[Before].VelocityCm);
    FDeathLab Creative;
    Creative.A.Options.bInfiniteSupply = true;
    Creative.P().Inventory.SetSlot(0, Creative.P().Inventory.MakeStack(Block, 3));
    const int32 CreativeBefore = Creative.A.GetItemEntities().Num();
    Creative.A.DropInventoryOnDeath(Creative.Player, Feet, 1);
    TestEqual(TEXT("infinite supply drops nothing"), Creative.A.GetItemEntities().Num(), CreativeBefore);
    // An open crafting table's grid drops with the inventory; without item entities nothing drops.
    FDeathLab Table;
    Table.P().OpenMenu.Grid.SetNum(9);
    Table.P().OpenMenu.Grid[4] = Table.P().Inventory.MakeStack(Block, 5);
    const int32 TableBefore = Table.A.GetItemEntities().Num();
    Table.A.DropInventoryOnDeath(Table.Player, Feet, 2);
    bool bGridDropped = false;
    for (int32 Index = TableBefore; Index < Table.A.GetItemEntities().Num(); ++Index)
        bGridDropped |= Table.A.GetItemEntities()[Index].Item == Block && Table.A.GetItemEntities()[Index].Count == 5;
    TestTrue(TEXT("the open grid drops"), bGridDropped);
    TestTrue(TEXT("the open grid is empty"), Table.P().OpenMenu.Grid[4].IsEmpty());
    FDeathLab NoEntities;
    NoEntities.A.Options.bItemEntities = false;
    NoEntities.P().Inventory.SetSlot(0, NoEntities.P().Inventory.MakeStack(Block, 3));
    NoEntities.A.DropInventoryOnDeath(NoEntities.Player, Feet, 3);
    TestFalse(TEXT("without item entities the stack stays"), NoEntities.P().Inventory.GetSlot(0)->IsEmpty());
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFoundationDeathRespawnPointTest, "Unnamed.Foundation.Death.RespawnPoint",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FFoundationDeathRespawnPointTest::RunTest(const FString& Parameters)
{
    FDeathLab L;
    TestFalse(TEXT("unset at first"), L.A.GetRespawnPoint(L.Player).IsSet());
    L.A.SetRespawnPoint(L.Player, FIntVector(3, -4, 5));
    const TOptional<FIntVector> Point = L.A.GetRespawnPoint(L.Player);
    TestTrue(TEXT("set"), Point.IsSet() && *Point == FIntVector(3, -4, 5));
    L.A.SetRespawnPoint(L.Player, FIntVector(1, 2, 3));
    TestTrue(TEXT("replaced"), L.A.GetRespawnPoint(L.Player).GetValue() == FIntVector(1, 2, 3));
    L.A.ClearRespawnPoint(L.Player);
    TestFalse(TEXT("cleared"), L.A.GetRespawnPoint(L.Player).IsSet());
    TestFalse(TEXT("an unknown player has none"), L.A.GetRespawnPoint(L.Player + 100).IsSet());
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFoundationDeathXpTest, "Unnamed.Foundation.Death.Xp",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FFoundationDeathXpTest::RunTest(const FString& Parameters)
{
    // [survival.r6.death_xp] min(7 x level, 100): 0, 7, 35, 98, 100, 100.
    for (const TPair<int32, int32>& Case :
        TArray<TPair<int32, int32>>{{0, 0}, {1, 7}, {5, 35}, {14, 98}, {15, 100}, {30, 100}})
        TestEqual(
            FString::Printf(TEXT("level %d"), Case.Key), FoundationDeath::GetExperienceDrop(Case.Key), Case.Value);
    // The orbs a level-5 death drops are worth 35 points.
    FDeathLab L;
    const int32 Before = L.A.GetExperienceOrbs().Num();
    L.A.SpawnExperience(FVector(100, 200, 25), FoundationDeath::GetExperienceDrop(5), 7);
    int32 Points = 0;
    for (int32 Index = Before; Index < L.A.GetExperienceOrbs().Num(); ++Index)
        Points += L.A.GetExperienceOrbs()[Index].Value;
    TestEqual(TEXT("level 5's orbs"), Points, 35);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFoundationDeathScreenTest, "Unnamed.Foundation.Death.Screen",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FFoundationDeathScreenTest::RunTest(const FString& Parameters)
{
    // Fixed-advance glyphs (6 px), as the chat's draw-list test.
    TArray<FHudGlyph> Glyphs;
    for (int32 Code = 32; Code < 127; ++Code)
    {
        FHudGlyph Glyph;
        Glyph.Code = Code;
        Glyph.Origin = FIntPoint(Code % 16 * 8, Code / 16 * 8);
        Glyph.InkWidth = Code == ' ' ? 0 : 5;
        Glyph.Advance = 6;
        Glyphs.Add(Glyph);
    }
    const FHudLayout Layout = HudModel::GetLayout({640, 360}, 0, false);
    FHudSnapshot S;
    S.Resources = EHudDataSource::Authoritative;
    S.bDeathScreen = true;
    S.DeathMessage = TEXT("Player died");
    S.DeathScore = 7;
    auto Build = [&] { return HudDrawList::Build(S, FHudPresentation{}, Layout, 0, Glyphs); };
    auto Has = [](const TArray<FHudDraw>& Draws, TFunctionRef<bool(const FHudDraw&)> Match)
    { return Draws.ContainsByPredicate([&Match](const FHudDraw& Draw) { return Match(Draw); }); };
    auto Glyph = [](const FHudDraw& Draw, FIntPoint Min, int32 Size, FLinearColor Color)
    {
        return Draw.Sprite == TEXT("font/ascii") && Draw.Rect == FIntRect(Min, Min + FIntPoint(Size)) &&
            Draw.Color.Equals(Color);
    };
    auto Hearts = [](const FHudDraw& Draw)
    { return Draw.Sprite.ToString().StartsWith(TEXT("gui/sprites/hud/heart/")); };

    // [survival.r6.death_screen] the gradient: 360 bands of 640 x 1, top 0x60500000, bottom 0xA0803030 (ARGB).
    const TArray<FHudDraw> Draws = Build();
    TArray<const FHudDraw*> Bands;
    for (const FHudDraw& Draw : Draws)
        if (Draw.Sprite.IsNone() && Draw.Block.IsNone() && Draw.Rect.Width() == 640 && Draw.Rect.Height() == 1)
            Bands.Add(&Draw);
    if (TestEqual(TEXT("one band per GUI row"), Bands.Num(), 360))
    {
        for (int32 Row = 0; Row < 360; ++Row)
            if (Bands[Row]->Rect.Min != FIntPoint(0, Row))
            {
                AddError(FString::Printf(TEXT("band %d is not at row %d"), Row, Row));
                break;
            }
        TestTrue(TEXT("top 0x60500000"), Bands[0]->Color.Equals(FLinearColor(FColor(0x50, 0x00, 0x00, 0x60))));
        TestTrue(TEXT("bottom 0xA0803030"), Bands.Last()->Color.Equals(FLinearColor(FColor(0x80, 0x30, 0x30, 0xA0))));
    }
    // [survival.r6.death_layout] the title at scale 2, centred on 320 with its top at 60 ("You Died!" is 54 px wide at
    // scale 1: 2 x (160 - 27)); its shadow 2 px down-right. The cause centred at y 85, the score at y 100 ("Score: "
    // white, the number yellow 0xFFFF55).
    TestTrue(
        TEXT("title"), Has(Draws, [&](const FHudDraw& D) { return Glyph(D, {266, 60}, 16, FLinearColor::White); }));
    TestTrue(TEXT("title shadow"),
        Has(Draws,
            [&](const FHudDraw& D) { return D.Sprite == TEXT("font/ascii") && D.Rect.Min == FIntPoint(268, 62); }));
    TestTrue(
        TEXT("cause"), Has(Draws, [&](const FHudDraw& D) { return Glyph(D, {320 - 33, 85}, 8, FLinearColor::White); }));
    TestTrue(TEXT("score label"),
        Has(Draws, [&](const FHudDraw& D) { return Glyph(D, {320 - 24, 100}, 8, FLinearColor::White); }));
    TestTrue(TEXT("score number"),
        Has(Draws, [&](const FHudDraw& D)
            { return Glyph(D, {320 - 24 + 42, 100}, 8, FLinearColor(FColor(0xFF, 0xFF, 0x55))); }));
    // The buttons: 200 x 20 at (220, 162) and (220, 186).
    const FIntRect Respawn = HudDrawList::GetDeathButton({640, 360}, EHudDeathButton::Respawn);
    const FIntRect Title = HudDrawList::GetDeathButton({640, 360}, EHudDeathButton::TitleScreen);
    TestEqual(TEXT("Respawn"), Respawn, FIntRect(FIntPoint(220, 162), FIntPoint(220 + 200, 162 + 20)));
    TestEqual(TEXT("Title Screen"), Title, FIntRect(FIntPoint(220, 186), FIntPoint(220 + 200, 186 + 20)));
    auto Button = [](const TArray<FHudDraw>& In, const FIntRect& Rect, const TCHAR* Sprite)
    { return In.ContainsByPredicate([&](const FHudDraw& D) { return D.Rect == Rect && D.Sprite == FName(Sprite); }); };
    // Disabled before tick 20, enabled from it; the hovered button is highlighted only when enabled.
    TestFalse(TEXT("tick 19: disabled"), FoundationDeath::AreButtonsEnabled(19));
    TestTrue(TEXT("tick 20: enabled"), FoundationDeath::AreButtonsEnabled(20));
    S.DeathHover = EHudDeathButton::Respawn;
    S.bDeathButtonsEnabled = FoundationDeath::AreButtonsEnabled(19);
    const TArray<FHudDraw> Disabled = Build();
    TestTrue(TEXT("disabled Respawn"), Button(Disabled, Respawn, TEXT("gui/sprites/widget/button_disabled")));
    TestTrue(TEXT("disabled Title Screen"), Button(Disabled, Title, TEXT("gui/sprites/widget/button_disabled")));
    S.bDeathButtonsEnabled = FoundationDeath::AreButtonsEnabled(20);
    const TArray<FHudDraw> Enabled = Build();
    TestTrue(TEXT("hovered Respawn"), Button(Enabled, Respawn, TEXT("gui/sprites/widget/button_highlighted")));
    TestTrue(TEXT("Title Screen"), Button(Enabled, Title, TEXT("gui/sprites/widget/button")));
    // The hit test: left/top edges inside, right/bottom outside, nothing in the gap between the buttons.
    const FIntPoint Points[] = {
        {220, 162}, {419, 181}, {420, 170}, {219, 170}, {300, 183}, {300, 186}, {300, 205}, {300, 206}};
    const EHudDeathButton Hits[] = {EHudDeathButton::Respawn, EHudDeathButton::Respawn, EHudDeathButton::None,
        EHudDeathButton::None, EHudDeathButton::None, EHudDeathButton::TitleScreen, EHudDeathButton::TitleScreen,
        EHudDeathButton::None};
    for (int32 Index = 0; Index < UE_ARRAY_COUNT(Points); ++Index)
        TestEqual(FString::Printf(TEXT("hit %d,%d"), Points[Index].X, Points[Index].Y),
            int32(HudDrawList::HitDeathButton({640, 360}, Points[Index])), int32(Hits[Index]));
    // The HUD stays under the screen, the crosshair hides; F1 hides the HUD but not the screen.
    TestTrue(TEXT("hearts under the screen"), Has(Draws, Hearts));
    auto Crosshair = [](const FHudDraw& D) { return D.Sprite == TEXT("gui/sprites/hud/crosshair"); };
    TestFalse(TEXT("no crosshair"), Has(Draws, Crosshair));
    S.bHideHud = true;
    const TArray<FHudDraw> Hidden = Build();
    TestFalse(TEXT("F1: no hearts"), Has(Hidden, Hearts));
    TestTrue(TEXT("F1: the screen draws"), Button(Hidden, Respawn, TEXT("gui/sprites/widget/button_highlighted")));
    // Alive: the crosshair is back; with F1 and the chat open only the chat (its input bar) draws.
    S.bDeathScreen = false;
    S.bHideHud = false;
    TestTrue(TEXT("alive: crosshair"), Has(Build(), Crosshair));
    S.bHideHud = S.bChatOpen = true;
    const TArray<FHudDraw> Chat = Build();
    TestFalse(TEXT("F1 + chat: no hearts"), Has(Chat, Hearts));
    TestFalse(TEXT("F1 + chat: no crosshair"), Has(Chat, Crosshair));
    TestTrue(TEXT("F1 + chat: the input bar"),
        Has(Chat, [](const FHudDraw& D) { return D.Sprite.IsNone() && D.Rect == FIntRect(2, 346, 638, 358); }));
    return true;
}
#endif

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFoundationDeathArmorWearTest, "Unnamed.Foundation.Death.ArmorWear",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FFoundationDeathArmorWearTest::RunTest(const FString& Parameters)
{
    // Codex review 27, finding 1: the killing hit wears the armor before the drops, so a chestplate at 527 of 528
    // breaks on a lethal 4-point hit (wear max(1, floor(4 / 4)) = 1) instead of dropping intact.
    FDeathLab L;
    L.A.Options.bToolWear = true;
    FFoundationItemDefinition Piece;
    Piece.Id = TEXT("lab_chestplate");
    Piece.MaxStack = 1;
    Piece.Equipment = EFoundationEquipmentSlot::Chest;
    Piece.Armor = 8;
    Piece.MaxDurability = 528;
    L.A.Rules.Items.Add(Piece);
    const int32 Chest = L.A.Rules.Items.Num() - 1;
    FFoundationInventory& Inventory = L.P().Inventory;
    Inventory.bFull = true;
    Inventory.Armor[2] = Inventory.MakeStack(Chest, 1);
    Inventory.Armor[2].Damage = 527;
    FFoundationVitals Vitals;
    Vitals.WornArmor = 8;
    Vitals.Health = 1;
    TestTrue(TEXT("the hit kills"), Vitals.Hurt(EFoundationDamage::MobAttack, 4) && Vitals.IsDead());
    TestEqual(TEXT("the hit owes 1 wear"), Vitals.PendingArmorWear, 1);
    AFoundationPawn::DropOnDeath(L.A, L.Player, Vitals, FVector(100, 200, 0), 1234);
    TestEqual(TEXT("the wear is spent"), Vitals.PendingArmorWear, 0);
    TestTrue(TEXT("the chestplate broke, not dropped"),
        Inventory.Armor[2].IsEmpty() &&
            !L.A.GetItemEntities().ContainsByPredicate(
                [Chest](const FFoundationItemEntity& E) { return E.Item == Chest; }));
    // With one more point left (526) the piece survives the wear and drops at 527.
    FDeathLab Spare;
    Spare.A.Options.bToolWear = true;
    Spare.A.Rules.Items.Add(Piece);
    FFoundationInventory& SpareInventory = Spare.P().Inventory;
    SpareInventory.bFull = true;
    const int32 SpareChest = Spare.A.Rules.Items.Num() - 1;
    SpareInventory.Armor[2] = SpareInventory.MakeStack(SpareChest, 1);
    SpareInventory.Armor[2].Damage = 526;
    FFoundationVitals SpareVitals;
    SpareVitals.PendingArmorWear = 1;
    AFoundationPawn::DropOnDeath(Spare.A, Spare.Player, SpareVitals, FVector(100, 200, 0), 1234);
    const FFoundationItemEntity* Dropped = Spare.A.GetItemEntities().FindByPredicate(
        [SpareChest](const FFoundationItemEntity& E) { return E.Item == SpareChest; });
    TestTrue(TEXT("at 526: dropped worn to 527"), Dropped && Dropped->Damage == 527);
    return true;
}
