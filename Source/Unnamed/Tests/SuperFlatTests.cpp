#if WITH_DEV_AUTOMATION_TESTS
#include "../MenuApplication.h"
#include "../MenuHost.h"
#include "../SuperFlatWorld.h"
#include "GameFramework/SpectatorPawn.h"
#include "Misc/AutomationTest.h"
#include "Misc/PackageName.h"
#include "Serialization/JsonSerializer.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSuperFlatRoutingTest, "Unnamed.Worlds.SuperFlat.RoutingAndIsolation",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSuperFlatRoutingTest::RunTest(const FString& Parameters)
{
    UMenuApplication* Service = NewObject<UMenuApplication>();
    Service->Initialize(nullptr, nullptr);
    for (const FString Id : {TEXT("fictional-forest"), TEXT("fictional-build"), TEXT("/Game/Maps/L_SuperFlat"),
        TEXT("super-flat?listen"), TEXT("open /Game/Menu/L_Menu"), TEXT(""), TEXT("super-flat")})
    {
        TSharedPtr<FJsonObject> Reply;
        FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Service->RequestOpenWorld(Id)), Reply);
        TestTrue(TEXT("Structured travel reply"), Reply.IsValid());
        if (!Reply) return false;
        TestFalse(TEXT("Unknown IDs or missing controller cannot travel: ") + Id, Reply->GetBoolField(TEXT("ok")));
        TestFalse(TEXT("Rejected request never queues travel"), Reply->GetBoolField(TEXT("pendingWorld")));
        TestFalse(TEXT("Rejected request never issues travel"), Reply->GetBoolField(TEXT("travelIssued")));
        Service->Tick();
    }
    TestTrue(TEXT("Super Flat asset exists"), FPackageName::DoesPackageExist(UMenuApplication::SuperFlatMap()));
    TestTrue(TEXT("Menu asset retained"), FPackageName::DoesPackageExist(UMenuApplication::MenuMap()));
    const ASuperFlatGameMode* WorldMode = GetDefault<ASuperFlatGameMode>();
    TestTrue(TEXT("Temporary stock camera; no new FPS character"), WorldMode->DefaultPawnClass == ASpectatorPawn::StaticClass());
    TestTrue(TEXT("World uses its own controller, not the menu browser"), WorldMode->PlayerControllerClass == ASuperFlatPreviewController::StaticClass());
    TestTrue(TEXT("Existing menu keeps its controller"), GetDefault<AMenuGameMode>()->PlayerControllerClass == AMenuPlayerController::StaticClass());
    return true;
}
#endif
