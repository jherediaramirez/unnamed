#include "MenuApplication.h"

#include "MenuVideoSettings.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "Kismet/KismetSystemLibrary.h"
#include "Serialization/JsonSerializer.h"

DEFINE_LOG_CATEGORY_STATIC(LogMenuApplication, Log, All);

void UMenuApplication::Initialize(APlayerController* InController, UMenuVideoSettings* InVideoSettings)
{
    Controller = InController;
    VideoSettings = InVideoSettings;
}

bool UMenuApplication::CanQuit() const
{
    const APlayerController* Player = Controller.Get();
    return !GIsEditor && Player && Player->IsLocalController() && Player->GetWorld() &&
        Player->GetWorld()->WorldType == EWorldType::Game && VideoSettings.IsValid();
}

FString UMenuApplication::GetState()
{
    const auto State = MakeShared<FJsonObject>();
    State->SetBoolField(TEXT("ok"), LastError.IsEmpty());
    State->SetBoolField(TEXT("canQuit"), CanQuit());
    State->SetBoolField(TEXT("pendingQuit"), bQuitPending);
    State->SetBoolField(TEXT("quitIssued"), bQuitIssued);
    State->SetStringField(TEXT("error"), LastError);
    FString Json;
    FJsonSerializer::Serialize(State, TJsonWriterFactory<>::Create(&Json));
    return Json;
}

FString UMenuApplication::RequestQuit()
{
    LastError.Empty();
    if (!CanQuit())
    {
        LastError = TEXT("Quit Game is available in the standalone game, not inside Unreal Editor.");
        return GetState();
    }
    if (!bQuitPending && !bQuitIssued)
    {
        bQuitPending = true;
        QuitDeadline = FPlatformTime::Seconds() + 8.0;
        UE_LOG(LogMenuApplication, Display, TEXT("Quit requested; waiting for completed settings changes"));
    }
    return GetState();
}

void UMenuApplication::Tick()
{
    if (!bQuitPending) return;
    if (!CanQuit())
        LastError = TEXT("The standalone game is no longer available.");
    else if (!VideoSettings->IsReadyForExit())
    {
        if (FPlatformTime::Seconds() < QuitDeadline) return;
        LastError = TEXT("Settings did not finish changing. Please try quitting again.");
    }
    else if (VideoSettings->FlushForExit(LastError))
    {
        bQuitPending = false;
        bQuitIssued = true;
        UE_LOG(LogMenuApplication, Display, TEXT("Settings flushed; requesting normal game shutdown"));
        UKismetSystemLibrary::QuitGame(Controller.Get(), Controller.Get(), EQuitPreference::Quit, false);
        return;
    }
    bQuitPending = false;
    UE_LOG(LogMenuApplication, Warning, TEXT("Quit cancelled: %s"), *LastError);
}
