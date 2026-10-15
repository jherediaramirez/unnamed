#include "Misc/AutomationTest.h"
#include "Foundation/FoundationActions.h"
#include "Foundation/FoundationVitals.h"
#include "Hud/HudDrawList.h"
#include "Hud/HudPresenter.h"

#if WITH_DEV_AUTOMATION_TESTS
// Survival step 11b: the hurt tint, the fire overlay and the chat caret. Values are literals from the survival findings
// ([survival.r6.hud_rules], [survival.r4.caret]), never the constants under test.
namespace HudFeedbackLocal
{
    // Fixed-advance glyphs (6 px), as the death screen's draw-list test.
    TArray<FHudGlyph> MakeGlyphs()
    {
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
        return Glyphs;
    }

    TArray<FHudDraw> Build(const FHudSnapshot& S)
    {
        return HudDrawList::Build(S, FHudPresentation{}, HudModel::GetLayout({640, 360}, 0, false), 0, MakeGlyphs());
    }

    int32 CountSprite(const TArray<FHudDraw>& Draws, const TCHAR* Sprite)
    {
        int32 Count = 0;
        for (const FHudDraw& Draw : Draws)
            Count += Draw.Sprite == Sprite;
        return Count;
    }

    FHudSnapshot Survival()
    {
        FHudSnapshot S;
        S.Resources = EHudDataSource::Authoritative;
        return S;
    }
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHudFeedbackTintTest, "Unnamed.Hud.Feedback.Tint",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FHudFeedbackTintTest::RunTest(const FString& Parameters)
{
    using namespace HudFeedbackLocal;
    // [survival.r6.hud_rules] overlay texel 0xB2FF0000 (ARGB) while hurtTime > 0 or deathTime > 0; hurtTime is
    // DamageCooldown - 10 (a fresh hit 20 gives 10).
    const FLinearColor Red(FColor(0xFF, 0x00, 0x00, 0xB2));
    auto IsTint = [&](const FHudDraw& Draw)
    { return Draw.Sprite.IsNone() && Draw.Rect == FIntRect(0, 0, 640, 360) && Draw.Color.Equals(Red); };
    auto TintFirst = [&](const FHudSnapshot& S)
    {
        const TArray<FHudDraw> Draws = Build(S);
        return !Draws.IsEmpty() && IsTint(Draws[0]);
    };
    auto Tinted = [&](const FHudSnapshot& S) { return Build(S).ContainsByPredicate(IsTint); };

    FHudSnapshot S = Survival();
    TestFalse(TEXT("no tint by default"), Tinted(S));
    S.bDamageTint = true;
    TestTrue(TEXT("tint is the first draw, full screen"), TintFirst(S));
    S.bHideHud = true;
    TestTrue(TEXT("F1 keeps the tint"), TintFirst(S));

    // The presenter's rule: tint at hurtTime > 0 and when dead, none at hurtTime 0, none with the switch off.
    IConsoleVariable* ScreenTint = IConsoleManager::Get().FindConsoleVariable(TEXT("foundation.Hurt.ScreenTint"));
    if (!TestNotNull(TEXT("foundation.Hurt.ScreenTint"), ScreenTint)) return false;
    const int32 OldTint = ScreenTint->GetInt();
    FFoundationVitals V;
    FHudSnapshot Applied;
    // Off by default (vanilla): no screen tint even on a fresh hit.
    ScreenTint->Set(0, ECVF_SetByCode);
    V.DamageCooldown = 20;
    {
        FHudSnapshot Off;
        UHudPresenter::ApplyVitals(Off, V, FFoundationTarget{});
        TestFalse(TEXT("no screen tint with the switch off"), Off.bDamageTint);
    }
    ScreenTint->Set(1, ECVF_SetByCode);
    auto Apply = [&]
    {
        Applied = FHudSnapshot{};
        UHudPresenter::ApplyVitals(Applied, V, FFoundationTarget{});
        return Applied.bDamageTint;
    };
    V.DamageCooldown = 20;
    TestTrue(TEXT("fresh hit (hurtTime 10)"), Apply());
    V.DamageCooldown = 11;
    TestTrue(TEXT("hurtTime 1"), Apply());
    V.DamageCooldown = 10;
    TestFalse(TEXT("hurtTime 0"), Apply());
    V.DamageCooldown = 0;
    V.DeathTicks = 0;
    TestTrue(TEXT("dead"), Apply());
    V.DeathTicks = -1;
    TestFalse(TEXT("alive, not hurt"), Apply());
    ScreenTint->Set(OldTint, ECVF_SetByCode);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHudFeedbackFireTest, "Unnamed.Hud.Feedback.Fire",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FHudFeedbackFireTest::RunTest(const FString& Parameters)
{
    using namespace HudFeedbackLocal;
    FHudSnapshot S = Survival();
    TestEqual(TEXT("no fire sprite when not burning"), CountSprite(Build(S), TEXT("block/fire_1")), 0);

    S.bOnFire = true;
    S.FireFrame = 3;
    const TArray<FHudDraw> Draws = Build(S);
    TestEqual(TEXT("two fire sprites"), CountSprite(Draws, TEXT("block/fire_1")), 2);
    TArray<const FHudDraw*> Fire;
    for (const FHudDraw& Draw : Draws)
        if (Draw.Sprite == TEXT("block/fire_1")) Fire.Add(&Draw);
    if (Fire.Num() == 2)
    {
        // fire_1 is 32 frames of 16 x 16 in a 16 x 512 strip: frame 3 is v 3/32 to 4/32; the right copy is mirrored.
        TestEqual(TEXT("left copy frame top"), Fire[0]->UV.Min.Y, 3.f / 32);
        TestEqual(TEXT("left copy frame bottom"), Fire[0]->UV.Max.Y, 4.f / 32);
        TestTrue(TEXT("left copy not mirrored"), Fire[0]->UV.Min.X < Fire[0]->UV.Max.X);
        TestTrue(TEXT("right copy mirrored"), Fire[1]->UV.Min.X > Fire[1]->UV.Max.X);
        TestEqual(TEXT("bottom aligned"), Fire[0]->Rect.Max.Y, 360);
        TestTrue(TEXT("left copy left of right copy"), Fire[0]->Rect.Min.X < Fire[1]->Rect.Min.X);
        TestTrue(TEXT("alpha 0.9"), FMath::IsNearlyEqual(Fire[0]->Color.A, .9f));
    }
    // The overlay sits after the tint and before the HUD.
    S.bDamageTint = true;
    const TArray<FHudDraw> Both = Build(S);
    TestTrue(TEXT("tint first, fire second"),
        Both.Num() > 1 && Both[0].Sprite.IsNone() && Both[1].Sprite == TEXT("block/fire_1"));

    // The presenter: burning in Survival draws it; Creative and Spectator do not.
    FFoundationVitals V;
    V.FireTicks = 100;
    FHudSnapshot Applied;
    UHudPresenter::ApplyVitals(Applied, V, FFoundationTarget{});
    TestTrue(TEXT("survival burning"), Applied.bOnFire);
    for (const EFoundationPlayerMode Mode : {EFoundationPlayerMode::Creative, EFoundationPlayerMode::Spectator})
    {
        V.Mode = Mode;
        Applied = FHudSnapshot{};
        UHudPresenter::ApplyVitals(Applied, V, FFoundationTarget{});
        TestFalse(TEXT("creative or spectator: no fire overlay"), Applied.bOnFire);
    }
    V.Mode = EFoundationPlayerMode::Survival;
    V.FireTicks = 0;
    Applied = FHudSnapshot{};
    UHudPresenter::ApplyVitals(Applied, V, FFoundationTarget{});
    TestFalse(TEXT("not burning"), Applied.bOnFire);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHudFeedbackCaretTest, "Unnamed.Hud.Feedback.Caret",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FHudFeedbackCaretTest::RunTest(const FString& Parameters)
{
    using namespace HudFeedbackLocal;
    // [survival.r4.caret] "_" after the text at the end; a 1 px bar at the insert position otherwise; hidden in the off
    // phase. Glyphs are 6 px wide, the line starts at x 4, the bar's top is at y 360 - 13 and 10 px tall.
    auto Underscores = [](const TArray<FHudDraw>& Draws)
    {
        int32 Count = 0;
        for (const FHudDraw& Draw : Draws)
            if (Draw.Sprite == TEXT("font/ascii") &&
                Draw.UV.Min == FVector2f(FIntPoint('_' % 16 * 8, '_' / 16 * 8)) / 128.f && Draw.Color.R > .5f &&
                Draw.Rect.Min.Y == 360 - 12 && Draw.Rect.Min.X > 4 + 6 * 2)
                ++Count;
        return Count;
    };
    auto Bars = [](const TArray<FHudDraw>& Draws)
    {
        TArray<FIntRect> Found;
        for (const FHudDraw& Draw : Draws)
            if (Draw.Sprite.IsNone() && Draw.Rect.Width() == 1 && Draw.Rect.Height() == 10) Found.Add(Draw.Rect);
        return Found;
    };

    FHudSnapshot S = Survival();
    S.bChatOpen = true;
    S.ChatInput = TEXT("abc");
    S.bCaretVisible = true;

    S.CaretIndex = 3;
    TArray<FHudDraw> Draws = Build(S);
    TestEqual(TEXT("caret at the end is a '_' after the text"), Underscores(Draws), 1);
    TestEqual(TEXT("no bar at the end"), Bars(Draws).Num(), 0);

    S.CaretIndex = 1;
    Draws = Build(S);
    const TArray<FIntRect> Found = Bars(Draws);
    TestEqual(TEXT("mid-text caret is one bar"), Found.Num(), 1);
    if (Found.Num() == 1) TestEqual(TEXT("bar after one glyph"), Found[0].Min, FIntPoint(4 + 6, 360 - 13));
    TestEqual(TEXT("no '_' mid-text"), Underscores(Draws), 0);

    S.bCaretVisible = false;
    Draws = Build(S);
    TestEqual(TEXT("off phase hides the bar"), Bars(Draws).Num(), 0);
    S.CaretIndex = 3;
    TestEqual(TEXT("off phase hides the '_'"), Underscores(Build(S)), 0);

    // A long line scrolls from the left and keeps its caret in view.
    S.ChatInput = FString::ChrN(200, TEXT('a'));
    S.CaretIndex = 200;
    S.bCaretVisible = true;
    TestEqual(TEXT("long line: '_' still drawn at the end"), Underscores(Build(S)) > 0, true);
    S.CaretIndex = 0;
    const TArray<FIntRect> AtStart = Bars(Build(S));
    TestTrue(TEXT("caret at the start stays at the left edge"), AtStart.Num() == 1 && AtStart[0].Min.X == 4);
    return true;
}
#endif
