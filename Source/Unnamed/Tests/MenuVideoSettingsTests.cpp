#if WITH_DEV_AUTOMATION_TESTS
#include "Menu/MenuVideoSettings.h"
#include "Misc/AutomationTest.h"
#include "Serialization/JsonSerializer.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMenuVideoValidationTest, "Unnamed.Menu.VideoSettings.Validation",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FMenuVideoValidationTest::RunTest(const FString& Parameters)
{
    UMenuVideoSettings* Service = NewObject<UMenuVideoSettings>();
    Service->Initialize(false);
    auto Read = [](const FString& Json)
    {
        TSharedPtr<FJsonObject> Value;
        FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Json), Value);
        return Value;
    };
    auto Apply = [&](const TCHAR* Name, const TCHAR* Json) { return Read(Service->ApplySetting(Name, Json)); };
    TestFalse(TEXT("No native window changes in editor tests"),
        Read(Service->GetState())->GetObjectField(TEXT("capabilities"))->GetBoolField(TEXT("fullscreen")));
    TestTrue(TEXT("GUI scale accepted"), Apply(TEXT("guiScale"), TEXT("{\"value\":3}"))->GetBoolField(TEXT("ok")));
    TestTrue(TEXT("Blur zero accepted"),
        Apply(TEXT("menuBackgroundBlurriness"), TEXT("{\"value\":0}"))->GetBoolField(TEXT("ok")));
    for (const TCHAR* Invalid : {TEXT("{\"value\":-1}"), TEXT("{\"value\":11}"), TEXT("{\"value\":2.5}"),
             TEXT("{\"value\":\"3\"}"), TEXT("{\"value\":true}"), TEXT("{\"value\":null}"), TEXT("[]"), TEXT("{"),
             TEXT("{\"value\":2,\"extra\":0}")})
        TestFalse(FString(TEXT("Reject invalid blur: ")) + Invalid,
            Apply(TEXT("menuBackgroundBlurriness"), Invalid)->GetBoolField(TEXT("ok")));
    TestFalse(TEXT("Reject unsupported world setting"),
        Apply(TEXT("renderDistance"), TEXT("{\"value\":12}"))->GetBoolField(TEXT("ok")));
    TestFalse(
        TEXT("Reject console setting"), Apply(TEXT("r.VSync"), TEXT("{\"value\":false}"))->GetBoolField(TEXT("ok")));
    TestFalse(
        TEXT("Reject frame step"), Apply(TEXT("framerateLimit"), TEXT("{\"value\":25}"))->GetBoolField(TEXT("ok")));
    TestFalse(
        TEXT("Reject wrong boolean type"), Apply(TEXT("fullscreen"), TEXT("{\"value\":1}"))->GetBoolField(TEXT("ok")));
    TestFalse(TEXT("Reject native mutation in editor"),
        Apply(TEXT("enableVsync"), TEXT("{\"value\":false}"))->GetBoolField(TEXT("ok")));
    const auto Settings = Read(Service->GetState())->GetObjectField(TEXT("settings"));
    TestEqual(TEXT("Rejected changes preserve GUI"), Settings->GetIntegerField(TEXT("guiScale")), 3);
    TestEqual(TEXT("Rejected changes preserve blur"), Settings->GetIntegerField(TEXT("menuBackgroundBlurriness")), 0);
    return true;
}
#endif
