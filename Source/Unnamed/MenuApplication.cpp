#include "MenuApplication.h"

#include "MenuVideoSettings.h"
#include "MenuPreferences.h"
#include "Input/MenuInputSettings.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "Kismet/KismetSystemLibrary.h"
#include "Kismet/GameplayStatics.h"
#include "Misc/PackageName.h"
#include "Serialization/JsonSerializer.h"

DEFINE_LOG_CATEGORY_STATIC(LogMenuApplication, Log, All);

void UMenuApplication::Initialize(APlayerController* InController, UMenuVideoSettings* InVideoSettings, UMenuInputSettings* InInputSettings, UMenuPreferences* InPreferences)
{
    Controller = InController;
    VideoSettings = InVideoSettings;
    InputSettings = InInputSettings;
    Preferences = InPreferences;
}

bool UMenuApplication::CanQuit() const
{
    const APlayerController* Player = Controller.Get();
    return !GIsEditor && Player && Player->IsLocalController() && Player->GetWorld() &&
        Player->GetWorld()->WorldType == EWorldType::Game && VideoSettings.IsValid();
}

bool UMenuApplication::CanOpenWorld() const
{
    const APlayerController* Player = Controller.Get();
    return Player && Player->IsLocalController() && Player->GetWorld() &&
        (Player->GetWorld()->WorldType == EWorldType::Game || Player->GetWorld()->WorldType == EWorldType::PIE) &&
        VideoSettings.IsValid() && !bQuitPending && !bQuitIssued;
}

bool UMenuApplication::FlushSettings()
{
    return VideoSettings.IsValid() && VideoSettings->FlushForExit(LastError) &&
        (!InputSettings.IsValid() || InputSettings->FlushForExit(LastError)) &&
        (!Preferences.IsValid() || Preferences->FlushForExit(LastError));
}

FString UMenuApplication::GetState()
{
    const auto State = MakeShared<FJsonObject>();
    State->SetBoolField(TEXT("ok"), LastError.IsEmpty());
    State->SetBoolField(TEXT("canQuit"), CanQuit());
    State->SetBoolField(TEXT("pendingQuit"), bQuitPending);
    State->SetBoolField(TEXT("quitIssued"), bQuitIssued);
    State->SetBoolField(TEXT("canOpenWorld"), CanOpenWorld());
    State->SetBoolField(TEXT("pendingWorld"), bWorldPending);
    State->SetBoolField(TEXT("travelIssued"), bTravelIssued);
    State->SetStringField(TEXT("error"), LastError);
    FString Json;
    FJsonSerializer::Serialize(State, TJsonWriterFactory<>::Create(&Json));
    return Json;
}

FString UMenuApplication::RequestQuit()
{
    LastError.Empty();
    if (bWorldPending || bTravelIssued)
    {
        LastError = TEXT("Please wait for Super Flat to open.");
        return GetState();
    }
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

FString UMenuApplication::RequestOpenWorld(const FString& WorldId)
{
    LastError.Empty();
    const FString RequestedMap=WorldId==TEXT("foundation")?TEXT("/Game/Maps/L_Foundation"):SuperFlatMap();
    if (WorldId != TEXT("super-flat") && WorldId != TEXT("foundation"))
        LastError = TEXT("Player Foundation and Super Flat are playable. The other entries are design references.");
    else if (!CanOpenWorld())
        LastError = TEXT("Opening a world requires a running game or Play in Editor session.");
    else if (!FPackageName::DoesPackageExist(RequestedMap))
        LastError = TEXT("The selected test level is missing from this build.");
    else if (!bWorldPending && !bTravelIssued)
    {
        bWorldPending = true;
        PendingMap=RequestedMap;
        WorldDeadline = FPlatformTime::Seconds() + 8.0;
        UE_LOG(LogMenuApplication, Display, TEXT("%s requested; waiting for completed settings changes"),*WorldId);
    }
    return GetState();
}

void UMenuApplication::Tick()
{
    if (bWorldPending)
    {
        LastError.Empty();
        if (!CanOpenWorld())
            LastError = TEXT("The game is no longer available for map travel.");
        else if (!VideoSettings->IsReadyForExit())
        {
            if (FPlatformTime::Seconds() < WorldDeadline) return;
            LastError = TEXT("Settings did not finish changing. Please try opening Super Flat again.");
        }
        else if (FlushSettings())
        {
            bWorldPending = false;
            bTravelIssued = true;
            UE_LOG(LogMenuApplication, Display, TEXT("Settings flushed; opening %s"), *PendingMap);
            UGameplayStatics::OpenLevel(Controller.Get(), FName(*PendingMap));
            return;
        }
        bWorldPending = false;
        UE_LOG(LogMenuApplication, Warning, TEXT("World travel cancelled: %s"), *LastError);
        return;
    }
    if (!bQuitPending) return;
    if (!CanQuit())
        LastError = TEXT("The standalone game is no longer available.");
    else if (!VideoSettings->IsReadyForExit())
    {
        if (FPlatformTime::Seconds() < QuitDeadline) return;
        LastError = TEXT("Settings did not finish changing. Please try quitting again.");
    }
    else if (FlushSettings())
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
