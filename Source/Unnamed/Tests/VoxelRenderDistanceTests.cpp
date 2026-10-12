#if WITH_DEV_AUTOMATION_TESTS
#include "Dom/JsonValue.h"
#include "HAL/IConsoleManager.h"
#include "Misc/AutomationTest.h"
#include "Voxel/VoxelRenderDistance.h"
#include "Voxel/VoxelWorldSettings.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVoxelRenderDistanceTest, "Unnamed.Voxel.RenderDistance",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FVoxelRenderDistanceTest::RunTest(const FString& Parameters)
{
    using namespace VoxelRenderDistance;
    auto Expect = [this](int32 Chunks, int32 View, int32 Far)
    {
        const FRadii Radii = FromChunks(Chunks);
        TestEqual(FString::Printf(TEXT("Render distance %d: view radius"), Chunks), Radii.ViewRadius, View);
        TestEqual(FString::Printf(TEXT("Render distance %d: far radius"), Chunks), Radii.FarRadius, Far);
    };
    // The far ring is twice the view up to 24; from 24 on it is no wider than the view, so none draws.
    Expect(2, 2, 4);
    Expect(6, 6, 12);
    Expect(8, 8, 16);
    Expect(11, 11, 22);
    Expect(12, 12, 24);
    Expect(13, 13, 24);
    Expect(16, 16, 24);
    Expect(23, 23, 24);
    Expect(24, 24, 24);
    Expect(32, 32, 24);
    // The slider's range, 2-32, bounds a value from an edited file.
    Expect(0, 2, 4);
    Expect(1, 2, 4);
    Expect(33, 32, 24);
    Expect(1000, 32, 24);
    for (int32 Chunks = MinChunks; Chunks <= MaxChunks; ++Chunks)
    {
        const FRadii Radii = FromChunks(Chunks);
        if (Radii.ViewRadius > 32 || Radii.FarRadius > 24)
            AddError(FString::Printf(TEXT("Render distance %d leaves the radii the ray tracing set proved"), Chunks));
    }

    // The default is the world before the slider was wired: the project settings' radii.
    const UVoxelWorldSettings& Settings = UVoxelWorldSettings::Get();
    const FRadii Project{Settings.ViewRadius, Settings.FarRadius};
    TestTrue(
        TEXT("The default render distance gives the project settings' radii"), FromChunks(DefaultChunks) == Project);

    // The preference: a number drives the rings, anything else reads as the default; the switch's 0 ignores it.
    const FJsonValueNumber Four(4);
    const FJsonValueNumber Rounded(7.6);
    const FJsonValueString Text(TEXT("4"));
    const FRadii Other{5, 9};
    TestTrue(TEXT("A saved 4 gives 4 and 8"), GetWanted(true, &Four, Other) == FRadii{4, 8});
    TestTrue(TEXT("A saved 7.6 rounds to 8"), GetWanted(true, &Rounded, Other) == FRadii{8, 16});
    TestTrue(TEXT("No saved value reads as 12"), GetWanted(true, nullptr, Other) == FRadii{12, 24});
    TestTrue(TEXT("A saved string reads as 12"), GetWanted(true, &Text, Other) == FRadii{12, 24});
    TestTrue(
        TEXT("voxel.RenderDistance.FromOptions 0 keeps the project settings"), GetWanted(false, &Four, Other) == Other);

    // Game-override priority: above the project settings, below the command line and the console.
    IConsoleVariable* Variable = IConsoleManager::Get().RegisterConsoleVariable(
        TEXT("voxel.Test.RenderDistanceRadius"), 12, TEXT("Unnamed.Voxel.RenderDistance's scratch variable."));
    if (!TestNotNull(TEXT("Scratch variable"), Variable)) return false;
    Variable->Set(12, ECVF_SetByProjectSetting);
    TestFalse(TEXT("The same value changes nothing"), SetRadius(*Variable, 12));
    TestEqual(TEXT("... and keeps the project setting's priority"), int32(Variable->GetFlags() & ECVF_SetByMask),
        int32(ECVF_SetByProjectSetting));
    TestTrue(TEXT("A new value over a project setting is set"), SetRadius(*Variable, 8));
    TestEqual(TEXT("... to that value"), Variable->GetInt(), 8);
    TestEqual(TEXT("... at game-override priority"), int32(Variable->GetFlags() & ECVF_SetByMask),
        int32(ECVF_SetByGameOverride));
    TestTrue(TEXT("Its own priority can move it again"), SetRadius(*Variable, 16));
    TestEqual(TEXT("... to that value"), Variable->GetInt(), 16);
    Variable->Set(20, ECVF_SetByCommandline);
    TestFalse(TEXT("The command line wins"), SetRadius(*Variable, 8));
    TestEqual(TEXT("... and keeps its value"), Variable->GetInt(), 20);
    Variable->Set(24, ECVF_SetByConsole);
    TestFalse(TEXT("The console wins"), SetRadius(*Variable, 8));
    TestEqual(TEXT("... and keeps its value"), Variable->GetInt(), 24);
    IConsoleManager::Get().UnregisterConsoleObject(Variable, false);
    return true;
}

#endif
