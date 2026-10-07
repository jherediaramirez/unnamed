#if WITH_DEV_AUTOMATION_TESTS
#include "Menu/MenuApplication.h"
#include "Misc/AutomationTest.h"
#include "Serialization/JsonSerializer.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMenuApplicationSafetyTest, "Unnamed.Menu.Application.QuitSafety",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FMenuApplicationSafetyTest::RunTest(const FString& Parameters)
{
    UMenuApplication* Service = NewObject<UMenuApplication>();
    Service->Initialize(nullptr, nullptr);
    TSharedPtr<FJsonObject> Reply;
    FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Service->RequestQuit()), Reply);
    TestTrue(TEXT("Structured reply"), Reply.IsValid());
    if (!Reply) return false;
    TestFalse(TEXT("Reject missing standalone owner"), Reply->GetBoolField(TEXT("ok")));
    TestFalse(TEXT("No editor quit capability"), Reply->GetBoolField(TEXT("canQuit")));
    TestFalse(TEXT("No queued exit on rejection"), Reply->GetBoolField(TEXT("pendingQuit")));
    TestFalse(TEXT("No issued exit on rejection"), Reply->GetBoolField(TEXT("quitIssued")));
    TestFalse(TEXT("Rejection explains standalone requirement"), Reply->GetStringField(TEXT("error")).IsEmpty());
    Service->Tick();
    return true;
}
#endif
