#if WITH_DEV_AUTOMATION_TESTS
#include "Foundation/FoundationActions.h"
#include "Foundation/FoundationVitals.h"
#include "Hud/HudAssets.h"
#include "Hud/HudCreativeScreen.h"
#include "Hud/HudLoadingScreen.h"
#include "Hud/HudDrawList.h"
#include "Hud/HudModel.h"
#include "Hud/HudPresenter.h"
#include "Hud/HudPreview.h"
#include "UnnamedProjectSettings.h"
#include "Engine/Texture2D.h"
#include "Misc/AutomationTest.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FHudGeometryTest, "Unnamed.Hud.Geometry", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FHudGeometryTest::RunTest(const FString& Parameters)
{
    const FIntPoint Sizes[] = {{320, 240}, {427, 240}, {640, 360}, {641, 361}};
    const FIntPoint Hotbar[] = {{69, 218}, {122, 218}, {229, 338}, {229, 339}};
    const FIntPoint Crosshair[] = {{152, 112}, {206, 112}, {312, 172}, {313, 173}};
    for (int32 Index = 0; Index < 4; ++Index)
        for (bool Left : {false, true})
            for (int32 Slot = 0; Slot < 9; ++Slot)
            {
                const auto L = HudModel::GetLayout(Sizes[Index], Slot, Left);
                TestEqual(TEXT("Integer hotbar origin"), L.Hotbar.Min, Hotbar[Index]);
                TestEqual(TEXT("True center rounding"), L.Crosshair.Min, Crosshair[Index]);
                TestEqual(
                    TEXT("Nine selections pitch and overhang"), L.Selection.Min.X, Hotbar[Index].X - 1 + Slot * 20);
                TestEqual(TEXT("16px item inset"), L.GetItem(Slot), Hotbar[Index] + FIntPoint(3 + Slot * 20, 3));
                TestEqual(TEXT("Offhand frame side"), L.Offhand.Min.X, L.Center + (Left ? 91 : -120));
                TestEqual(TEXT("Offhand item is not mirrored"), L.OffhandItem.X, L.Center + (Left ? 101 : -117));
                TestEqual(TEXT("Attack opposite offhand"), L.AttackHotbar.X, L.Center + (Left ? -113 : 97));
                for (int32 MountRows = 0; MountRows <= 3; ++MountRows)
                    TestEqual(TEXT("Air one/two/three mount rows"), L.GetAirY(MountRows),
                        Sizes[Index].Y - 49 - FMath::Max(0, MountRows - 1) * 10);
            }
    TestEqual(TEXT("720 Auto"), HudModel::GetScale({1280, 720}, 0), 3);
    TestEqual(TEXT("1080 Auto"), HudModel::GetScale({1920, 1080}, 0), 4);
    TestEqual(TEXT("1440 Auto"), HudModel::GetScale({2560, 1440}, 0), 6);
    TestEqual(TEXT("4K Auto"), HudModel::GetScale({3840, 2160}, 0), 9);
    TestEqual(TEXT("320-wide HUD rule differs from 416-wide menu"), HudModel::GetScale({1280, 960}, 0), 4);
    TestEqual(TEXT("Requested maximum"), HudModel::GetScale({1920, 1080}, 3), 3);
    TestEqual(TEXT("Small viewport never zero"), HudModel::GetScale({200, 120}, 0), 1);
    TestEqual(TEXT("1440 requested 3"), HudModel::GetScale({2560, 1440}, 3), 3);
    TestEqual(TEXT("1440 requested 7 clamps to Auto's 6"), HudModel::GetScale({2560, 1440}, 7), 6);
    TestEqual(TEXT("1440 requested 0 is Auto"), HudModel::GetScale({2560, 1440}, 0), 6);
    // Every native screen follows Options > GUI Scale through the same rule (the creative inventory, its tooltip and
    // the loading screen were Auto-only until 2026-10-11).
    TestEqual(TEXT("Creative follows the requested scale"), SHudCreativeScreen::GetGuiScale({2560, 1440}, 3), 3);
    TestEqual(TEXT("Creative Auto at 1440"), SHudCreativeScreen::GetGuiScale({2560, 1440}, 0), 6);
    TestEqual(TEXT("Creative keeps the 320-wide rule"), SHudCreativeScreen::GetGuiScale({1280, 960}, 0), 4);
    TestEqual(TEXT("Creative Auto at a 320-wide step"), SHudCreativeScreen::GetGuiScale({1600, 1440}, 0), 5);
    // The widgets read the setting through their attribute every frame (the owners bind it to Options > GUI Scale).
    int32 Requested = 3;
    const TSharedRef<SHudCreativeScreen> CreativeScreen =
        SNew(SHudCreativeScreen).RequestedGuiScale_Lambda([&Requested] { return Requested; });
    const TSharedRef<SHudLoadingScreen> LoadingScreen =
        SNew(SHudLoadingScreen).RequestedGuiScale_Lambda([&Requested] { return Requested; });
    TestEqual(TEXT("Creative screen at the requested 3"), CreativeScreen->GetScaleFor({2560, 1440}), 3);
    TestEqual(TEXT("Loading screen at the requested 3"), LoadingScreen->GetScaleFor({2560, 1440}), 3);
    Requested = 0;
    TestEqual(TEXT("Creative screen follows a change to Auto"), CreativeScreen->GetScaleFor({2560, 1440}), 6);
    TestEqual(TEXT("Loading screen follows a change to Auto"), LoadingScreen->GetScaleFor({2560, 1440}), 6);
    const TSharedRef<SHudLoadingScreen> Unbound = SNew(SHudLoadingScreen);
    TestEqual(TEXT("Screens default to Auto"), Unbound->GetScaleFor({1920, 1080}), 4);
    TestEqual(TEXT("Creative Auto floors fractional pixels, as its old float test"),
        SHudCreativeScreen::GetGuiScale({1599.9f, 1440}, 0), 4);
    TestEqual(TEXT("Ceil logical dimensions"), HudModel::GetLogicalSize({1280, 720}, 3), FIntPoint(427, 240));
    TestEqual(TEXT("Odd framebuffer ceil"), HudModel::GetLogicalSize({1921, 1081}, 4), FIntPoint(481, 271));
    const auto Rows = HudModel::GetHealthGeometry(60, 60, 60, 0);
    TestEqual(TEXT("Three rows"), Rows.Rows, 3);
    TestEqual(TEXT("Three-row compression"), Rows.RowHeight, 9);
    TestEqual(TEXT("Armor follows player rows"), HudModel::GetLayout({640, 360}, 0, false).GetArmorY(Rows), 293);
    TestEqual(TEXT("Air does not follow player rows"), HudModel::GetLayout({640, 360}, 0, false).GetAirY(), 311);
    TestEqual(TEXT("Row pitch floor"), HudModel::GetHealthGeometry(200, 200, 200, 0).RowHeight, 3);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FHudHealthTest, "Unnamed.Hud.Health", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FHudHealthTest::RunTest(const FString& Parameters)
{
    for (int32 Points : {0, 1, 2, 3, 4, 19, 20})
    {
        int32 Full = 0, Half = 0;
        for (int32 Cell = 0; Cell < 10; ++Cell)
        {
            const int32 Fill = HudModel::GetPairFill(Points, Cell);
            Full += Fill == 2;
            Half += Fill == 1;
        }
        TestEqual(TEXT("Points survive cell selection"), Full * 2 + Half, Points);
        TestEqual(TEXT("At most one half"), Half, Points % 2);
    }
    FHudSnapshot S;
    S.Health = 3.01;
    FHudPresentation P;
    P.Update(S, 0, 0);
    TestEqual(TEXT("Fractional health is ceiled"), P.LastHealth, 4);
    const auto Odd = HudModel::GetHealthGeometry(19, 19, 19, 1);
    TestEqual(TEXT("Odd max plus absorption has source row count"), Odd.Rows, 1);
    TestEqual(TEXT("Separate container rounding deliberately yields 11"),
        Odd.NormalContainers + Odd.AbsorptionContainers, 11);
    TestEqual(TEXT("Delayed health expands max"), HudModel::GetHealthGeometry(20, 40, 3, 0).Rows, 2);
    TestEqual(TEXT("Absorption loss collapses rows"), HudModel::GetHealthGeometry(20, 20, 20, 0).Rows, 1);
    TestEqual(TEXT("Absorption adds row"), HudModel::GetHealthGeometry(20, 20, 20, .1).Rows, 2);
    S.bPoison = S.bWither = S.bFrozen = true;
    TestTrue(TEXT("Poison precedence"), HudModel::GetHeartType(S) == EHudHeart::Poisoned);
    S.bPoison = false;
    TestTrue(TEXT("Wither precedence"), HudModel::GetHeartType(S) == EHudHeart::Withered);
    S.bWither = false;
    TestTrue(TEXT("Frozen family"), HudModel::GetHeartType(S) == EHudHeart::Frozen);
    TestEqual(TEXT("Hardcore half blink sprite"), HudModel::GetHeartSprite(EHudHeart::Frozen, true, true, true),
        FName(TEXT("gui/sprites/hud/heart/frozen_hardcore_half_blinking")));
    TestEqual(TEXT("Hardcore container sprite"), HudModel::GetHeartSprite(EHudHeart::Container, true, false, true),
        FName(TEXT("gui/sprites/hud/heart/container_hardcore_blinking")));
    for (int64 Tick = 0; Tick < 100; ++Tick)
        for (int32 Cell = 0; Cell < 10; ++Cell)
            TestTrue(TEXT("Jitter bounded to 0..1 at a fixed HUD tick"),
                HudModel::GetJitter(Tick, Cell, 2) >= 0 && HudModel::GetJitter(Tick, Cell, 2) <= 1);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FHudAirTest, "Unnamed.Hud.Air", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FHudAirTest::RunTest(const FString& Parameters)
{
    struct FCase
    {
        int32 Air, Full, Burst, Empty, SurfaceFull, SurfaceEmpty;
    };
    const FCase Cases[] = {{300, 10, 0, 0, 0, 0}, {299, 10, 0, 0, 10, 0}, {272, 9, 1, 0, 9, 0}, {271, 9, 1, 0, 9, 0},
        {270, 9, 0, 0, 9, 1}, {269, 9, 0, 1, 9, 1}, {2, 0, 1, 9, 0, 9}, {1, 0, 1, 9, 0, 9}, {0, 0, 0, 10, 0, 10},
        {-1, 0, 0, 10, 0, 10}, {-19, 0, 0, 10, 0, 10}, {-20, 0, 0, 10, 0, 10}};
    for (const auto& C : Cases)
        for (bool Water : {false, true})
        {
            int32 Counts[4] = {};
            for (int32 Cell = 1; Cell <= 10; ++Cell)
                ++Counts[int32(HudModel::GetAirCell(C.Air, 300, Water, Cell))];
            TestEqual(
                FString::Printf(TEXT("A=%d water=%d full"), C.Air, Water), Counts[1], Water ? C.Full : C.SurfaceFull);
            TestEqual(TEXT("Burst only underwater"), Counts[2], Water ? C.Burst : 0);
            TestEqual(TEXT("Delayed empty/surface recovery"), Counts[3], Water ? C.Empty : C.SurfaceEmpty);
        }
    TestTrue(TEXT("Refill remains visible outside water"), HudModel::GetAirCell(150, 300, false, 10) == EHudAir::Empty);
    TestTrue(TEXT("Refill full hides"), HudModel::GetAirCell(300, 300, false, 1) == EHudAir::None);
    TestTrue(
        TEXT("Modified maximum supplied by adapter"), HudModel::GetAirCell(301, 600, true, 6) == EHudAir::Bursting);
    TestTrue(TEXT("Invalid maximum is unsupported"), HudModel::GetAirCell(0, 0, true, 1) == EHudAir::None);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHudPresentationTest, "Unnamed.Hud.Presentation",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FHudPresentationTest::RunTest(const FString& Parameters)
{
    FHudSnapshot S;
    FHudPresentation P;
    P.Update(S, 0, 0);
    S.Health = 3;
    S.bDamageCooldown = true;
    P.Update(S, 1, .05);
    TestEqual(TEXT("Damage 20 ticks"), P.BlinkUntil, int64(21));
    TestEqual(TEXT("Old health is retained"), P.DisplayedHealth, 20);
    TestFalse(TEXT("20-tick first band"), P.IsBlinking(1));
    TestTrue(TEXT("Three-tick blink band"), P.IsBlinking(4));
    S.Health = 9;
    P.Update(S, 4, .2);
    TestEqual(TEXT("Heal during blink is ten ticks"), P.BlinkUntil, int64(14));
    TestEqual(TEXT("Heal preserves delayed history"), P.DisplayedHealth, 20);
    P.Update(S, 24, 1.2);
    TestEqual(TEXT("Strictly more than one second"), P.DisplayedHealth, 20);
    P.Update(S, 25, 1.25);
    TestEqual(TEXT("History catches up"), P.DisplayedHealth, 9);
    S.ItemName = TEXT("Preview Token");
    S.NotificationTime = 2;
    P.Update(S, 30, 1.5);
    TestEqual(TEXT("Existing notification multiplier"), P.ItemNameUntil, int64(110));
    TestEqual(TEXT("Final ten tick fade"), P.GetNameAlpha(105), .5f);
    S.ItemName.Empty();
    P.Update(S, 31, 1.55);
    TestEqual(TEXT("Empty slot hides name"), P.GetNameAlpha(31), 0.f);
    for (int32 Bits = 0; Bits < 64; ++Bits)
    {
        bool W = Bits & 1, X = Bits & 2, R = Bits & 4, J = Bits & 8, C = Bits & 16, D = Bits & 32;
        const auto State = HudModel::GetContext(W, X, R, J, C, D);
        if (J && (!W || C || D))
            TestTrue(TEXT("Jump priority"), State == EHudContext::MountJump);
        else if (W && (!X || !R))
            TestTrue(TEXT("Locator fallback"), State == EHudContext::Locator);
        else
            TestTrue(TEXT("XP capability or empty"), State == (X ? EHudContext::Experience : EHudContext::Empty));
    }
    TestEqual(TEXT("XP uses 183 before clipping"), HudModel::GetExperienceWidth(.5), 91);
    TestEqual(TEXT("Full XP clipped to texture"), HudModel::GetExperienceWidth(1), 182);
    TestEqual(TEXT("Attack horizontal"), HudModel::GetAttackWidth(.5), 8);
    TestEqual(TEXT("Attack vertical"), HudModel::GetAttackHeight(.5), 9);
    S.AttackStrength = 1;
    S.bLivingTarget = S.bTargetAlive = S.bTargetInRange = true;
    TestTrue(TEXT("Ready target"), HudModel::IsReadyMarkerVisible(S));
    S.AttackDelay = 5;
    TestFalse(TEXT("Fast weapon has no ready marker"), HudModel::IsReadyMarkerVisible(S));
    S.Mode = EHudMode::Spectator;
    TestFalse(TEXT("Spectator target gate"), HudModel::IsCrosshairVisible(S));
    S.bSpectatorTarget = true;
    TestTrue(TEXT("Spectator eligible target"), HudModel::IsCrosshairVisible(S));
    S.bFirstPerson = false;
    TestFalse(TEXT("Third person hides crosshair"), HudModel::IsCrosshairVisible(S));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHudAssetsTest, "Unnamed.Hud.AssetsAndDraws",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FHudAssetsTest::RunTest(const FString& Parameters)
{
    const auto* Assets = LoadObject<UHudAssets>(nullptr, *UUnnamedProjectSettings::GetHudRegistryPath());
    if (!TestNotNull(TEXT("Cookable imported registry"), Assets)) return false;
    TestEqual(TEXT("All PNGs registered"), Assets->Sprites.Num(), 194);
    TestEqual(TEXT("Printable ASCII provider"), Assets->Glyphs.Num(), 95);
    TSet<FName> Ids;
    for (const auto& Entry : Assets->Sprites)
    {
        TestFalse(TEXT("No duplicate sprite ids"), Ids.Contains(Entry.Id));
        Ids.Add(Entry.Id);
        UTexture2D* Texture = Entry.Texture.LoadSynchronous();
        if (!TestNotNull(*Entry.Id.ToString(), Texture)) continue;
        // NullRHI has no platform texture. Rendered probes separately check cooked dimensions.
#if WITH_EDITORONLY_DATA
        TestEqual(TEXT("Imported width"), Texture->Source.GetSizeX(), int64(Entry.NativeSize.X));
        TestEqual(TEXT("Imported height"), Texture->Source.GetSizeY(), int64(Entry.NativeSize.Y));
        TestTrue(TEXT("Alpha preserved"), !Texture->CompressionNoAlpha);
        TestTrue(TEXT("No mipmaps"), Texture->MipGenSettings == TMGS_NoMipmaps);
#endif
        TestTrue(TEXT("Native filtering preset"),
            Texture->Filter == (Entry.ImportPreset == TEXT("blurred_overlay") ? TF_Bilinear : TF_Nearest));
        TestTrue(
            TEXT("Animation timing retained"), Entry.Frames.Num() == Entry.FrameTicks.Num() && Entry.Frames.Num() > 0);
        if (Entry.Id == TEXT("gui/sprites/hud/locator_bar_arrow_up"))
        {
            TestEqual(TEXT("Arrow frame is half sheet"), Entry.FrameSize, FIntPoint(7, 5));
            TestTrue(TEXT("Arrow 10/4 ticks"), Entry.FrameTicks == TArray<int32>({10, 4}));
        }
        if (Entry.Id == TEXT("gui/sprites/hud/locator_bar_background"))
            TestTrue(TEXT("Nine-slice retained"),
                Entry.MetadataJson.Contains(TEXT("nine_slice")) && Entry.MetadataJson.Contains(TEXT("\"left\":5")));
    }
    for (auto Type : {EHudHeart::Normal, EHudHeart::Poisoned, EHudHeart::Withered, EHudHeart::Frozen,
             EHudHeart::Absorbing, EHudHeart::Container})
        for (bool Hard : {false, true})
            for (bool Half : {false, true})
                for (bool Blink : {false, true})
                    TestTrue(TEXT("Every heart selection resolves"),
                        Ids.Contains(HudModel::GetHeartSprite(Type, Hard, Half, Blink)));
    TestEqual(TEXT("Narrow bitmap glyph metrics"), HudDrawList::MeasureText(TEXT("ii"), Assets->Glyphs), 4);
    TestEqual(TEXT("Space provider wins"), HudDrawList::MeasureText(TEXT("1 1"), Assets->Glyphs), 16);
    FHudPreview Preview;
    auto S = Preview.Values;
    S.Counts = {64, 16, 1, 2, 0, 0, 0, 0, 9};
    S.OffhandCount = 1;
    S.bWither = true;
    S.Absorption = 1;
    FHudPresentation P;
    P.Update(S, 0, 0);
    const auto L = HudModel::GetLayout({640, 360}, 0, false);
    auto Draws = HudDrawList::Build(S, P, L, 0, Assets->Glyphs);
    TestTrue(TEXT("Absorption uses wither artwork"),
        Draws.ContainsByPredicate(
            [](const FHudDraw& D) { return D.Sprite == TEXT("gui/sprites/hud/heart/withered_half"); }));
    for (const auto& D : Draws)
        TestTrue(TEXT("Every draw resolves"), D.Sprite.IsNone() || Ids.Contains(D.Sprite));
    const auto* Fill = Draws.FindByPredicate(
        [](const FHudDraw& D) { return D.Sprite == TEXT("gui/sprites/hud/experience_bar_progress"); });
    TestTrue(TEXT("Progress crops UV and destination together"),
        Fill && FMath::IsNearlyEqual(Fill->UV.Max.X, Fill->Rect.Width() / 182.f));
    S.Mode = EHudMode::Creative;
    Draws = HudDrawList::Build(S, P, L, 0, Assets->Glyphs);
    TestTrue(TEXT("Creative keeps hotbar"),
        Draws.ContainsByPredicate([](const FHudDraw& D) { return D.Sprite == TEXT("gui/sprites/hud/hotbar"); }));
    TestFalse(TEXT("Creative hides resources/XP"),
        Draws.ContainsByPredicate(
            [](const FHudDraw& D)
            {
                return D.Sprite.ToString().Contains(TEXT("heart/")) || D.Sprite.ToString().Contains(TEXT("experience"));
            }));
    S.Mode = EHudMode::Spectator;
    Draws = HudDrawList::Build(S, P, L, 0, Assets->Glyphs);
    TestFalse(TEXT("Spectator selector deferred, ordinary hotbar hidden"),
        Draws.ContainsByPredicate([](const FHudDraw& D) { return D.Sprite.ToString().Contains(TEXT("hotbar")); }));
    S.bVisible = false;
    TestEqual(TEXT("F1/focus gates entire game HUD"), HudDrawList::Build(S, P, L, 0, Assets->Glyphs).Num(), 0);
    S.bVisible = true;
    S.Mode = EHudMode::Survival;
    S.Health = S.MaxHealth = 19;
    S.Absorption = 1;
    P = FHudPresentation{};
    P.Update(S, 0, 0);
    Draws = HudDrawList::Build(S, P, L, 0, Assets->Glyphs);
    const int32 ArmorIndex =
        Draws.IndexOfByPredicate([](const FHudDraw& D) { return D.Sprite == TEXT("gui/sprites/hud/armor_full"); });
    const int32 HeartIndex =
        Draws.IndexOfByPredicate([](const FHudDraw& D) { return D.Sprite.ToString().Contains(TEXT("heart/")); });
    TestTrue(TEXT("Odd-max absorption spill paints over armor"),
        ArmorIndex >= 0 && ArmorIndex < HeartIndex && Draws[HeartIndex].Rect.Min.Y == L.Size.Y - 50);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHudPreviewTest, "Unnamed.Hud.PreviewSequences",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FHudPreviewTest::RunTest(const FString& Parameters)
{
    FHudPreview Preview;
    Preview.Start(EHudPreviewSequence::AirCycle);
    for (int32 Tick = 0; Tick < 300; ++Tick)
        Preview.Step();
    TestEqual(TEXT("Fixture zero air"), Preview.Values.Air, 0);
    TestEqual(TEXT("Fixture never damages a player"), Preview.Values.Health, 20.);
    for (int32 Tick = 0; Tick < 21; ++Tick)
        Preview.Step();
    TestFalse(TEXT("Fixture surfaced"), Preview.Values.bUnderwater);
    TestEqual(TEXT("Fixture recovery starts"), Preview.Values.Air, 4);
    for (int32 Tick = 0; Tick < 74; ++Tick)
        Preview.Step();
    TestEqual(TEXT("Fixture refill completes"), Preview.Values.Air, 300);
    TestTrue(TEXT("Sequence ends"), Preview.Sequence == EHudPreviewSequence::None);
    Preview.Start(EHudPreviewSequence::DamageHeal);
    for (int32 Tick = 0; Tick < 14; ++Tick)
        Preview.Step();
    TestEqual(TEXT("Healing occurs during damage window"), Preview.Values.Health, 9.);
    Preview.Start(EHudPreviewSequence::ExperienceGain);
    for (int32 Tick = 0; Tick < 60; ++Tick)
        Preview.Step();
    TestEqual(TEXT("Preview XP level changes"), Preview.Values.Level, 8);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHudVitalsAdapterTest, "Unnamed.Hud.VitalsAdapter",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FHudVitalsAdapterTest::RunTest(const FString& Parameters)
{
    FFoundationVitals V;
    V.Mode = EFoundationPlayerMode::Adventure;
    V.Health = 13.5f;
    V.MaxHealth = 24;
    V.Absorption = 3;
    V.Armor = 7;
    V.Food = 9;
    V.Saturation = 0;
    V.Air = 120;
    V.bEyesInWater = true;
    V.Level = 4;
    V.Progress = .25f;
    V.DamageCooldown = 3;
    V.AttackSpeed = 1.6f;
    V.AttackTicker = 25;
    FFoundationTarget Target;
    Target.Kind = EFoundationTargetKind::Entity;
    Target.bInRange = Target.bInteractive = true;
    FHudSnapshot S;
    S.bHardcore = S.bPoison = true;
    UHudPresenter::ApplyVitals(S, V, Target);
    TestTrue(TEXT("Vitals are authoritative"), S.Resources == EHudDataSource::Authoritative);
    TestTrue(TEXT("Player mode maps by name, not order"), S.Mode == EHudMode::Adventure);
    TestTrue(TEXT("Health values"), S.Health == 13.5 && S.MaxHealth == 24 && S.Absorption == 3);
    TestTrue(TEXT("Armor and food"), S.Armor == 7 && S.Food == 9 && S.Saturation == 0);
    TestTrue(TEXT("Air from eyes in water"), S.Air == 120 && S.MaxAir == 300 && S.bUnderwater);
    TestTrue(TEXT("Experience"), S.Level == 4 && S.Experience == .25);
    TestTrue(TEXT("Damage cooldown drives the health blink"), S.bDamageCooldown);
    TestTrue(TEXT("Recovered sword-like attack on an entity in reach"),
        S.AttackStrength == 1 && S.AttackDelay > 12 && HudModel::IsReadyMarkerVisible(S));
    TestTrue(TEXT("Interactive target in reach for spectators"), S.bSpectatorTarget);
    TestFalse(TEXT("No effect or Hardcore owner yet"), S.bHardcore || S.bPoison);
    Target.bInRange = false;
    V.Mode = EFoundationPlayerMode::Creative;
    UHudPresenter::ApplyVitals(S, V, Target);
    TestFalse(TEXT("Entity out of reach is no crosshair target"), S.bLivingTarget || S.bSpectatorTarget);
    TestTrue(TEXT("Creative"), S.Mode == EHudMode::Creative && !S.CanHurt());

    // The label names whatever is still fixture data.
    const auto* Assets = LoadObject<UHudAssets>(nullptr, *UUnnamedProjectSettings::GetHudRegistryPath());
    if (!TestNotNull(TEXT("Cookable imported registry"), Assets)) return false;
    const auto L = HudModel::GetLayout({640, 360}, 0, false);
    auto LabelGlyphs = [&](EHudDataSource Resources, EHudDataSource Items)
    {
        FHudSnapshot Label;
        Label.Resources = Resources;
        Label.Items = Items;
        FHudPresentation P;
        P.Update(Label, 0, 0);
        return HudDrawList::Build(Label, P, L, 0, Assets->Glyphs)
            .FilterByPredicate([&L](const FHudDraw& D)
                { return D.Sprite == TEXT("font/ascii") && D.Rect.Min.Y >= L.Size.Y - 12 && D.Rect.Min.X < 100; })
            .Num();
    };
    TestEqual(TEXT("PREVIEW with shadows"), LabelGlyphs(EHudDataSource::Preview, EHudDataSource::Preview), 14);
    TestEqual(TEXT("PREVIEW ITEMS beside real vitals"),
        LabelGlyphs(EHudDataSource::Authoritative, EHudDataSource::Preview), 24);
    TestEqual(TEXT("No label without fixture data"),
        LabelGlyphs(EHudDataSource::Authoritative, EHudDataSource::Authoritative), 0);
    return true;
}
#endif
