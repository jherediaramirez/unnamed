#if WITH_DEV_AUTOMATION_TESTS
#include "Foundation/FoundationController.h"
#include "HAL/IConsoleManager.h"
#include "Misc/AutomationTest.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFoundationLoadCoverTest, "Unnamed.Foundation.LoadCover",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

// foundation.Load.Cover: the switch's default and the loading screen's frame count once the world is ready.
bool FFoundationLoadCoverTest::RunTest(const FString& Parameters)
{
    IConsoleVariable* Cover = IConsoleManager::Get().FindConsoleVariable(TEXT("foundation.Load.Cover"));
    TestNotNull(TEXT("foundation.Load.Cover exists"), Cover);
    if (Cover) TestEqual(TEXT("foundation.Load.Cover defaults to 1"), Cover->GetInt(), 1);
    TestEqual(TEXT("0: no cover frames"), AFoundationController::GetLoadCoverFrames(0), 0);
    TestEqual(TEXT("1: three rendered frames"), AFoundationController::GetLoadCoverFrames(1), 3);
    TestFalse(TEXT("never before the world is ready"), AFoundationController::ShouldHideLoadingScreen(0, 0));
    TestTrue(TEXT("0: hidden on the first ready frame (until 2026-10-12)"),
        AFoundationController::ShouldHideLoadingScreen(1, 0));
    TestFalse(TEXT("1: still up on the third ready frame"), AFoundationController::ShouldHideLoadingScreen(3, 3));
    TestTrue(TEXT("1: hidden on the fourth"), AFoundationController::ShouldHideLoadingScreen(4, 3));
    return true;
}
#endif
