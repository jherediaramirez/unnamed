#if WITH_DEV_AUTOMATION_TESTS
#include "Menu/MenuBrowserHost.h"
#include "HAL/IConsoleManager.h"
#include "Misc/AutomationTest.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMenuPauseModeTest, "Unnamed.Menu.PauseMenu.Modes",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

// foundation.Pause.Menu and .Background: the switches' defaults, the page URL per mode and background, the blur
// mapping, and a host with no player never making a browser.
bool FMenuPauseModeTest::RunTest(const FString& Parameters)
{
    IConsoleVariable* Mode = IConsoleManager::Get().FindConsoleVariable(TEXT("foundation.Pause.Menu"));
    TestNotNull(TEXT("foundation.Pause.Menu exists"), Mode);
    if (Mode) TestEqual(TEXT("foundation.Pause.Menu defaults to 1 (kept alive)"), Mode->GetInt(), 1);
    IConsoleVariable* Background = IConsoleManager::Get().FindConsoleVariable(TEXT("foundation.Pause.Background"));
    TestNotNull(TEXT("foundation.Pause.Background exists"), Background);
    if (Background) TestEqual(TEXT("foundation.Pause.Background defaults to 1 (the world)"), Background->GetInt(), 1);

    const FString Base = TEXT("file:///C:/Game/Content/Menu/Web/index.html");
    TestEqual(TEXT("mode 0 opens today's URL"), UMenuBrowserHost::PauseMenuUrl(Base, 0), Base);
    TestEqual(TEXT("mode 1 opens the pause screen"), UMenuBrowserHost::PauseMenuUrl(Base, 1), Base + TEXT("#pause"));
    TestEqual(TEXT("mode 2 opens the pause screen"), UMenuBrowserHost::PauseMenuUrl(Base, 2), Base + TEXT("#pause"));
    TestEqual(TEXT("background 1 asks for the world"), UMenuBrowserHost::PauseMenuUrl(Base, 1, 1),
        Base + TEXT("#pause&bg=world"));
    TestEqual(
        TEXT("background 0 keeps the panorama"), UMenuBrowserHost::PauseMenuUrl(Base, 2, 0), Base + TEXT("#pause"));
    TestEqual(TEXT("mode 0 ignores the background"), UMenuBrowserHost::PauseMenuUrl(Base, 0, 1), Base);
    TestEqual(TEXT("blur 5 at 1080 lines"), UMenuBrowserHost::PauseBlurStrength(5, 1080), 5.f);
    TestEqual(
        TEXT("blur scales with the viewport height"), UMenuBrowserHost::PauseBlurStrength(5, 1440), 5.f * 1440 / 1080);
    TestEqual(TEXT("blur 0 is none"), UMenuBrowserHost::PauseBlurStrength(0, 1440), 0.f);
    TestEqual(TEXT("blur clamps to the setting's 10"), UMenuBrowserHost::PauseBlurStrength(12, 1080), 10.f);

    UMenuBrowserHost* Host = NewObject<UMenuBrowserHost>();
    TestEqual(TEXT("a host starts in mode 0 (the menu map)"), Host->GetPauseMode(), 0);
    Host->SetPauseMode(7);
    TestEqual(TEXT("modes clamp to 0..2"), Host->GetPauseMode(), 2);
    Host->SetPauseMode(1);
    Host->Prewarm();
    Host->Show();
    TestFalse(TEXT("no player controller: no browser"), Host->HasBrowser());
    TestFalse(TEXT("no player controller: not shown"), Host->IsOpen());
    TestFalse(TEXT("no page: not ready"), Host->HandlePageReady());
    Host->Hide();
    return true;
}
#endif
