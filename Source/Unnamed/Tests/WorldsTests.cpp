#if WITH_DEV_AUTOMATION_TESTS
#include "Menu/MenuApplication.h"
#include "UnnamedProjectSettings.h"
#include "Menu/MenuGameMode.h"
#include "Menu/MenuPlayerController.h"
#include "Foundation/FoundationController.h"
#include "Foundation/FoundationGameMode.h"
#include "Foundation/FoundationPawn.h"
#include "InputAction.h"
#include "InputMappingContext.h"
#include "Materials/MaterialInterface.h"
#include "Misc/AutomationTest.h"
#include "Misc/PackageName.h"
#include "Serialization/JsonSerializer.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FWorldRoutingTest, "Unnamed.Worlds.RoutingAndIsolation",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FWorldRoutingTest::RunTest(const FString& Parameters)
{
    UMenuApplication* Service = NewObject<UMenuApplication>();
    Service->Initialize(nullptr, nullptr);
    // Design references, retired worlds, paths, commands and the real ID without a menu controller.
    for (const FString Id : {TEXT("fictional-forest"), TEXT("fictional-build"), TEXT("super-flat"), TEXT("foundation"),
             TEXT("interaction-lab"), TEXT("/Game/Maps/L_Playground"), TEXT("playground?listen"),
             TEXT("open /Game/Menu/L_Menu"), TEXT(""), TEXT("playground")})
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
    TestTrue(TEXT("Playground asset exists"),
        FPackageName::DoesPackageExist(UUnnamedProjectSettings::GetWorldMapPackage(TEXT("playground"))));
    TestTrue(TEXT("Menu asset retained"), FPackageName::DoesPackageExist(UUnnamedProjectSettings::GetMenuMapPackage()));
    const AFoundationGameMode* WorldMode = GetDefault<AFoundationGameMode>();
    TestTrue(TEXT("World plays the first-person pawn"), WorldMode->DefaultPawnClass == AFoundationPawn::StaticClass());
    TestTrue(TEXT("World uses its own controller, not the menu's"),
        WorldMode->PlayerControllerClass == AFoundationController::StaticClass());
    TestTrue(TEXT("Existing menu keeps its controller"),
        GetDefault<AMenuGameMode>()->PlayerControllerClass == AMenuPlayerController::StaticClass());
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FWorldCatalogTest, "Unnamed.Worlds.Catalog",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FWorldCatalogTest::RunTest(const FString& Parameters)
{
    const UUnnamedProjectSettings& Project = UUnnamedProjectSettings::Get();
    TestEqual(TEXT("One world for every feature"), Project.Worlds.Num(), 1);
    TestNotNull(TEXT("Exact catalog ID resolves"), Project.FindWorld(TEXT("playground")));
    TestNull(TEXT("IDs are case-sensitive"), Project.FindWorld(TEXT("Playground")));
    TestNull(TEXT("Paths are never IDs"), Project.FindWorld(TEXT("/Game/Maps/L_Playground")));
    for (const TCHAR* Retired : {TEXT("foundation"), TEXT("super-flat"), TEXT("interaction-lab")})
        TestNull(TEXT("Retired world stays retired: ") + FString(Retired), Project.FindWorld(Retired));
    for (const FUnnamedWorldEntry& World : Project.Worlds)
        TestTrue(TEXT("Catalog map exists: ") + World.Id.ToString(),
            FPackageName::DoesPackageExist(World.Map.GetLongPackageName()));
    TestNotNull(TEXT("Dev grid material loads"), Project.DevGridMaterial.LoadSynchronous());
    TestTrue(TEXT("Every input asset loads"),
        Project.OnFootContext.LoadSynchronous() && Project.FoundationActionsContext.LoadSynchronous() &&
            Project.MoveAction.LoadSynchronous() && Project.LookAction.LoadSynchronous() &&
            Project.SprintAction.LoadSynchronous() && Project.CrouchAction.LoadSynchronous() &&
            Project.JumpAction.LoadSynchronous());

    UMenuApplication* Service = NewObject<UMenuApplication>();
    TSharedPtr<FJsonObject> State;
    FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Service->GetState()), State);
    const TArray<TSharedPtr<FJsonValue>>* Worlds = nullptr;
    TestTrue(TEXT("State lists the catalog for the page"), State && State->TryGetArrayField(TEXT("worlds"), Worlds));
    if (!Worlds) return false;
    TestEqual(TEXT("Page receives every catalog world"), Worlds->Num(), Project.Worlds.Num());
    for (int32 Index = 0; Index < Worlds->Num(); ++Index)
        TestEqual(TEXT("Catalog order and IDs"), (*Worlds)[Index]->AsObject()->GetStringField(TEXT("id")),
            Project.Worlds[Index].Id.ToString());
    return true;
}
#endif
