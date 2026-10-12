#include "Foundation/FoundationScreenMessages.h"
#include "HAL/IConsoleManager.h"
#include "Misc/AutomationTest.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFoundationScreenMessagesTest, "Unnamed.Foundation.ScreenMessages",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FFoundationScreenMessagesTest::RunTest(const FString& Parameters)
{
    // Vanilla shows no engine text: hidden in normal play, kept in probe and capture runs (Jesus, 2026-10-11).
    TestEqual(TEXT("default mode"), FoundationScreenMessages::DefaultMode, -1);
    const IConsoleVariable* Variable = IConsoleManager::Get().FindConsoleVariable(TEXT("foundation.ScreenMessages"));
    if (TestNotNull(TEXT("foundation.ScreenMessages exists"), Variable) &&
        (Variable->GetFlags() & ECVF_SetByMask) == ECVF_SetByConstructor)
        TestEqual(TEXT("the switch starts at the default"), Variable->GetInt(), -1);
    TestFalse(TEXT("normal play hides them"), FoundationScreenMessages::ShouldShow(-1, false));
    TestTrue(TEXT("probe runs keep them"), FoundationScreenMessages::ShouldShow(-1, true));
    TestFalse(TEXT("0 hides them in a probe too"), FoundationScreenMessages::ShouldShow(0, true));
    TestFalse(TEXT("0 hides them in play"), FoundationScreenMessages::ShouldShow(0, false));
    TestTrue(TEXT("1 shows them in play, as before"), FoundationScreenMessages::ShouldShow(1, false));
    TestTrue(TEXT("1 shows them in a probe"), FoundationScreenMessages::ShouldShow(1, true));
    TestTrue(TEXT("a Playground probe"),
        FoundationScreenMessages::IsProbeRun(
            TEXT("Unnamed.uproject -game -FoundationProbe=\"C:/out/runtime-60.json\"")));
    TestTrue(TEXT("a voxel capture"), FoundationScreenMessages::IsProbeRun(TEXT("-VoxelShot=\"C:/out\" -nosound")));
    TestTrue(TEXT("a flight"), FoundationScreenMessages::IsProbeRun(TEXT("-VoxelFlight=C:/out")));
    TestTrue(TEXT("a loading shot"), FoundationScreenMessages::IsProbeRun(TEXT("-VoxelLoadingShot=C:/out")));
    TestFalse(TEXT("a normal launch"),
        FoundationScreenMessages::IsProbeRun(TEXT("Unnamed.uproject /Game/Maps/L_Generated?VoxelWorld=abc -game")));
    return true;
}
