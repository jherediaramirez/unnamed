#if WITH_DEV_AUTOMATION_TESTS
#include "Menu/MenuBrowserHost.h"
#include "Menu/MenuBrowserSubsystem.h"
#include "Menu/MenuLoadingOverlay.h"
#include "Engine/GameInstance.h"
#include "HAL/IConsoleManager.h"
#include "Misc/AutomationTest.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMenuTitleBrowserTest, "Unnamed.Menu.TitleBrowser",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

// menu.Title.Keep: the switch's default, when the menu map may show the kept browser at once, the loading overlay's
// layout and fade (our own numbers, unverified against vanilla), and a host or subsystem with no player or page doing
// nothing.
bool FMenuTitleBrowserTest::RunTest(const FString& Parameters)
{
    IConsoleVariable* Keep = IConsoleManager::Get().FindConsoleVariable(TEXT("menu.Title.Keep"));
    TestNotNull(TEXT("menu.Title.Keep exists"), Keep);
    if (Keep)
    {
        TestEqual(TEXT("menu.Title.Keep defaults to 1 (kept, with the fade)"), Keep->GetInt(), 1);
        TestEqual(TEXT("GetKeepMode reads it"), UMenuBrowserSubsystem::GetKeepMode(), 1);
    }

    TestTrue(TEXT("a title painted for this look shows at once"),
        UMenuBrowserSubsystem::GetTitleReveal(true, true) == EMenuTitleReveal::Now);
    TestTrue(TEXT("a title painted for another look waits for a fresh one"),
        UMenuBrowserSubsystem::GetTitleReveal(true, false) == EMenuTitleReveal::AfterReady);
    TestTrue(TEXT("no title painted (game start, the page left for a world) waits"),
        UMenuBrowserSubsystem::GetTitleReveal(false, true) == EMenuTitleReveal::AfterReady);
    TestTrue(TEXT("neither"), UMenuBrowserSubsystem::GetTitleReveal(false, false) == EMenuTitleReveal::AfterReady);

    // 2560 x 1440 and 1280 x 720 at Auto are both 427 x 240 GUI pixels (scales 6 and 3): a 240 x 60 logo in the middle
    // and the bar at row 199.
    for (const FIntPoint Framebuffer : {FIntPoint(2560, 1440), FIntPoint(1280, 720)})
    {
        const FMenuLoadingOverlayLayout Layout = FMenuLoadingOverlayLayout::Compute(Framebuffer, 0);
        const FString At = FString::Printf(TEXT(" at %dx%d"), Framebuffer.X, Framebuffer.Y);
        TestEqual(TEXT("Auto scale") + At, Layout.GuiScale, Framebuffer.X == 2560 ? 6 : 3);
        TestEqual(TEXT("GUI size") + At, Layout.GuiSize, FIntPoint(427, 240));
        TestEqual(TEXT("logo left half") + At, Layout.LogoLeft, FIntRect(93, 90, 213, 150));
        TestEqual(TEXT("logo right half") + At, Layout.LogoRight, FIntRect(213, 90, 333, 150));
        TestEqual(TEXT("bar") + At, Layout.Bar, FIntRect(93, 194, 333, 204));
    }
    const FMenuLoadingOverlayLayout Scale2 = FMenuLoadingOverlayLayout::Compute(FIntPoint(1280, 720), 2);
    TestEqual(TEXT("GUI Scale 2 at 1280x720"), Scale2.GuiSize, FIntPoint(640, 360));
    TestEqual(TEXT("the logo follows the GUI height"), Scale2.LogoLeft, FIntRect(140, 135, 320, 225));
    TestEqual(TEXT("the bar at 0.8325 of the height"), Scale2.Bar, FIntRect(140, 294, 500, 304));
    const FMenuLoadingOverlayLayout Wide = FMenuLoadingOverlayLayout::Compute(FIntPoint(3840, 1080), 0);
    TestEqual(TEXT("a wide window: the height decides the logo"), Wide.LogoLeft.Height(), Wide.GuiSize.Y / 4);

    const TSharedRef<SMenuLoadingOverlay> Overlay = SNew(SMenuLoadingOverlay);
    TestEqual(TEXT("no fade before the title is ready"), Overlay->GetFade(100), -1.);
    Overlay->StartFadeOut(100);
    Overlay->StartFadeOut(101);
    TestEqual(TEXT("the fade counts from its first start"), Overlay->GetFade(100.5), .5);
    TestFalse(TEXT("still fading after 1.9 s"), Overlay->IsFadedOut(101.9));
    TestTrue(TEXT("done after 2 s"), Overlay->IsFadedOut(102));

    UMenuBrowserHost* Host = NewObject<UMenuBrowserHost>();
    Host->ShowTitle(1);
    TestFalse(TEXT("no player controller: no title browser"), Host->HasBrowser());
    // A game instance subsystem lives within a game instance.
    UMenuBrowserSubsystem* Titles = NewObject<UMenuBrowserSubsystem>(NewObject<UGameInstance>(GetTransientPackage()));
    Titles->HandleTitleReady(-1);
    Titles->Detach();
    TestFalse(TEXT("no page: nothing painted"), Titles->IsTitlePainted());
    TestFalse(TEXT("no page: no overlay"), Titles->IsOverlayShown());
    return true;
}
#endif
