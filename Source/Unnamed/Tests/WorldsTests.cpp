#if WITH_DEV_AUTOMATION_TESTS
#include "Menu/MenuApplication.h"
#include "UnnamedProjectSettings.h"
#include "Menu/MenuGameMode.h"
#include "Menu/MenuPlayerController.h"
#include "SuperFlat/SuperFlatGameMode.h"
#include "SuperFlat/SuperFlatPreviewController.h"
#include "GameFramework/SpectatorPawn.h"
#include "InputAction.h"
#include "InputMappingContext.h"
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
    TestTrue(TEXT("Super Flat asset exists"),
        FPackageName::DoesPackageExist(UUnnamedProjectSettings::GetWorldMapPackage(TEXT("super-flat"))));
    TestTrue(TEXT("Menu asset retained"), FPackageName::DoesPackageExist(UUnnamedProjectSettings::GetMenuMapPackage()));
    const ASuperFlatGameMode* WorldMode = GetDefault<ASuperFlatGameMode>();
    TestTrue(TEXT("Temporary stock camera; no new FPS character"),
        WorldMode->DefaultPawnClass == ASpectatorPawn::StaticClass());
    TestTrue(TEXT("World uses its own controller, not the menu browser"),
        WorldMode->PlayerControllerClass == ASuperFlatPreviewController::StaticClass());
    TestTrue(TEXT("Existing menu keeps its controller"),
        GetDefault<AMenuGameMode>()->PlayerControllerClass == AMenuPlayerController::StaticClass());
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FWorldCatalogTest, "Unnamed.Worlds.Catalog",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FWorldCatalogTest::RunTest(const FString& Parameters)
{
    const UUnnamedProjectSettings& Project = UUnnamedProjectSettings::Get();
    TestNotNull(TEXT("Exact catalog ID resolves"), Project.FindWorld(TEXT("super-flat")));
    TestNull(TEXT("IDs are case-sensitive"), Project.FindWorld(TEXT("Super-Flat")));
    TestNull(TEXT("Paths are never IDs"), Project.FindWorld(TEXT("/Game/Maps/L_SuperFlat")));
    for (const FUnnamedWorldEntry& World : Project.Worlds)
        TestTrue(TEXT("Catalog map exists: ") + World.Id.ToString(),
            FPackageName::DoesPackageExist(World.Map.GetLongPackageName()));
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
