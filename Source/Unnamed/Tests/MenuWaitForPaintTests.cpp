#if WITH_DEV_AUTOMATION_TESTS
#include "Menu/MenuApplication.h"
#include "HAL/IConsoleManager.h"
#include "Misc/AutomationTest.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"

namespace
{
    bool ReadMenuWaitForPaint(UMenuApplication& Application)
    {
        TSharedPtr<FJsonObject> State;
        bool bWait = false;
        return FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Application.GetState()), State) &&
            State->TryGetBoolField(TEXT("waitForPaint"), bWait) && bWait;
    }
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMenuWaitForPaintTest, "Unnamed.Menu.WorldWaitForPaint",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

// menu.World.WaitForPaint: the switch's default and the page seeing it in GetState (read when the application is
// initialized, i.e. when the menu map starts).
bool FMenuWaitForPaintTest::RunTest(const FString& Parameters)
{
    IConsoleVariable* WaitForPaint = IConsoleManager::Get().FindConsoleVariable(TEXT("menu.World.WaitForPaint"));
    TestNotNull(TEXT("menu.World.WaitForPaint exists"), WaitForPaint);
    if (!WaitForPaint) return false;
    TestEqual(TEXT("menu.World.WaitForPaint defaults to 1"), WaitForPaint->GetInt(), 1);
    UMenuApplication* Application = NewObject<UMenuApplication>();
    Application->Initialize(nullptr, nullptr);
    TestTrue(TEXT("GetState reports it"), ReadMenuWaitForPaint(*Application));
    WaitForPaint->Set(0, ECVF_SetByCode);
    TestTrue(TEXT("still 1 until the menu map starts again"), ReadMenuWaitForPaint(*Application));
    Application->Initialize(nullptr, nullptr);
    TestFalse(TEXT("0 once read again"), ReadMenuWaitForPaint(*Application));
    WaitForPaint->Set(1, ECVF_SetByCode);
    return true;
}
#endif
