#if WITH_DEV_AUTOMATION_TESTS
#include "Foundation/FoundationPanels.h"
#include "HAL/IConsoleManager.h"
#include "Misc/AutomationTest.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFoundationPanelsPreloadTest, "Unnamed.Foundation.PanelsPreload",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

// foundation.Panels.Preload: the switch's default, and panels in a headless run (no game viewport, so nothing ever
// opens) preloading nothing.
bool FFoundationPanelsPreloadTest::RunTest(const FString& Parameters)
{
    IConsoleVariable* Preload = IConsoleManager::Get().FindConsoleVariable(TEXT("foundation.Panels.Preload"));
    TestNotNull(TEXT("foundation.Panels.Preload exists"), Preload);
    if (Preload) TestEqual(TEXT("foundation.Panels.Preload defaults to 1"), Preload->GetInt(), 1);
    UFoundationPanels* Panels = NewObject<UFoundationPanels>();
    TestTrue(TEXT("no game viewport: nothing to wait for"), Panels->PreloadCreative());
    TestFalse(TEXT("nothing preloaded"), Panels->HasPreloadedCreative());
    return true;
}
#endif
