#include "Menu/MenuApplication.h"
#include "UnnamedLog.h"
#include "UnnamedProjectSettings.h"

#include "Menu/MenuVideoSettings.h"
#include "Menu/MenuPreferences.h"
#include "Menu/MenuInputBridge.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "Kismet/KismetSystemLibrary.h"
#include "Kismet/GameplayStatics.h"
#include "Misc/PackageName.h"
#include "Serialization/JsonSerializer.h"
#include "Voxel/VoxelTerrainFactory.h"
#include "Voxel/VoxelWorldgenReader.h"

namespace
{
    // The list's second line, as the reference shows a save: its folder and when it was last played, local time.
    FString DescribeSavedWorldPlayed(const FUnnamedSavedWorld& World)
    {
        const FDateTime Local = World.LastPlayed + (FDateTime::Now() - FDateTime::UtcNow());
        return FString::Printf(TEXT("%s (%d/%d/%02d, %d:%02d %s)"), *World.Id, Local.GetMonth(), Local.GetDay(),
            Local.GetYear() % 100, Local.GetHour12(), Local.GetMinute(), Local.IsMorning() ? TEXT("AM") : TEXT("PM"));
    }

    // The third line: the mode, then cheats and any world type other than Default.
    FString DescribeSavedWorldRules(const FUnnamedSavedWorld& World)
    {
        FString Text = World.IsHardcore() ? TEXT("Hardcore Mode!") : World.GameMode + TEXT(" Mode");
        if (World.bCommands) Text += TEXT(", Cheats");
        if (World.Type != TEXT("Default")) Text += TEXT(", ") + World.Type;
        return Text;
    }

    // A saved world whose terrain cannot be built never opens: the voxel world has no fallback for it.
    bool CanBuildSavedWorldTerrain(const FUnnamedSavedWorld& World, FString& OutError)
    {
        FString Reason;
        if (VoxelTerrainFactory::Check(World, VoxelWorldgen::GetReferenceRoot(), Reason)) return true;
        OutError = FString::Printf(TEXT("This world cannot be played: %s."), *Reason);
        return false;
    }

    bool CanPlaySavedWorld(const FString& Id, FString& OutError)
    {
        FUnnamedSavedWorld World;
        return UnnamedSavedWorlds::Load(Id, World, OutError) && CanBuildSavedWorldTerrain(World, OutError);
    }
}

void UMenuApplication::Initialize(APlayerController* InController, UMenuVideoSettings* InVideoSettings,
    UMenuInputBridge* InInputBridge, UMenuPreferences* InPreferences)
{
    Controller = InController;
    VideoSettings = InVideoSettings;
    InputBridge = InInputBridge;
    Preferences = InPreferences;
    // The kept title page's application (menu.Title.Keep) is initialized again on every menu map: nothing pending.
    bQuitPending = bQuitIssued = bWorldPending = bTravelIssued = false;
    LastError.Empty();
    RefreshSavedWorlds();
}

void UMenuApplication::RefreshSavedWorlds()
{
    SavedWorlds = UnnamedSavedWorlds::List();
}

void UMenuApplication::QueueTravel(const FString& Map, const FString& Options, const FString& Label)
{
    if (bWorldPending || bTravelIssued) return;
    bWorldPending = true;
    PendingMap = Map;
    PendingOptions = Options;
    WorldDeadline = FPlatformTime::Seconds() + 8.0;
    UE_LOG(LogUnnamedMenu, Display, TEXT("%s requested; waiting for completed settings changes"), *Label);
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
        (!InputBridge.IsValid() || InputBridge->FlushForExit(LastError)) &&
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
    // The page renders and enables exactly these worlds; it keeps no list of its own.
    TArray<TSharedPtr<FJsonValue>> Worlds;
    for (const FUnnamedWorldEntry& Entry : UUnnamedProjectSettings::Get().Worlds)
    {
        const auto World = MakeShared<FJsonObject>();
        World->SetStringField(TEXT("id"), Entry.Id.ToString());
        World->SetStringField(TEXT("name"), Entry.Name);
        World->SetStringField(TEXT("summary"), Entry.Summary);
        World->SetStringField(TEXT("status"), Entry.Status);
        Worlds.Add(MakeShared<FJsonValueObject>(World));
    }
    State->SetArrayField(TEXT("worlds"), Worlds);
    // Create New World's saves, most recently played first, each with its settings for Re-Create.
    TArray<TSharedPtr<FJsonValue>> Saved;
    for (const FUnnamedSavedWorld& Entry : SavedWorlds)
    {
        const auto World = MakeShared<FJsonObject>();
        World->SetStringField(TEXT("id"), Entry.Id);
        World->SetStringField(TEXT("name"), Entry.Name);
        World->SetStringField(TEXT("summary"), DescribeSavedWorldPlayed(Entry));
        World->SetStringField(TEXT("status"), DescribeSavedWorldRules(Entry));
        World->SetObjectField(TEXT("draft"), Entry.ToDraft());
        Saved.Add(MakeShared<FJsonValueObject>(World));
    }
    State->SetArrayField(TEXT("savedWorlds"), Saved);
    FString Json;
    FJsonSerializer::Serialize(State, TJsonWriterFactory<>::Create(&Json));
    return Json;
}

FString UMenuApplication::RequestQuit()
{
    LastError.Empty();
    if (bWorldPending || bTravelIssued)
    {
        LastError = TEXT("Please wait for the selected world to open.");
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
        UE_LOG(LogUnnamedMenu, Display, TEXT("Quit requested; waiting for completed settings changes"));
    }
    return GetState();
}

FString UMenuApplication::RequestOpenWorld(const FString& WorldId)
{
    LastError.Empty();
    // A catalog world opens its own map; a saved world opens the generated map with its id.
    FString RequestedMap = UUnnamedProjectSettings::GetWorldMapPackage(WorldId), Options;
    const bool bSaved = RequestedMap.IsEmpty() && UnnamedSavedWorlds::IsValidId(WorldId);
    if (bSaved)
    {
        RequestedMap = UUnnamedProjectSettings::GetGeneratedMapPackage();
        Options = TEXT("VoxelWorld=") + WorldId;
    }
    if (RequestedMap.IsEmpty())
        LastError = TEXT("Choose a world from the list.");
    else if (!CanOpenWorld())
        LastError = TEXT("Opening a world requires a running game or Play in Editor session.");
    else if (!FPackageName::DoesPackageExist(RequestedMap))
        LastError = TEXT("The selected world is missing from this build.");
    else if (bWorldPending || bTravelIssued)
        return GetState();
    else if (!bSaved || (CanPlaySavedWorld(WorldId, LastError) && UnnamedSavedWorlds::Touch(WorldId, LastError)))
        QueueTravel(RequestedMap, Options, WorldId);
    if (bSaved) RefreshSavedWorlds();
    return GetState();
}

FString UMenuApplication::CreateWorld(const FString& DraftJson)
{
    LastError.Empty();
    const FString Map = UUnnamedProjectSettings::GetGeneratedMapPackage();
    FUnnamedSavedWorld World;
    if (bWorldPending || bTravelIssued) return GetState();
    if (!CanOpenWorld())
        LastError = TEXT("Creating a world requires a running game or Play in Editor session.");
    else if (!FPackageName::DoesPackageExist(Map))
        LastError = TEXT("The generated world's map is missing from this build.");
    else if (UnnamedSavedWorlds::ParseDraft(DraftJson, World, LastError) &&
        CanBuildSavedWorldTerrain(World, LastError) && UnnamedSavedWorlds::Create(DraftJson, World, LastError))
    {
        RefreshSavedWorlds();
        QueueTravel(Map, TEXT("VoxelWorld=") + World.Id, World.Id);
    }
    return GetState();
}

FString UMenuApplication::DeleteWorld(const FString& WorldId)
{
    LastError.Empty();
    if (bWorldPending || bTravelIssued)
        LastError = TEXT("Please wait for the selected world to open.");
    else if (UnnamedSavedWorlds::Delete(WorldId, LastError))
        RefreshSavedWorlds();
    return GetState();
}

FString UMenuApplication::RenameWorld(const FString& WorldId, const FString& Name)
{
    LastError.Empty();
    if (Name.Len() > 1024)
        LastError = TEXT("A world name can be at most 128 characters.");
    else if (UnnamedSavedWorlds::Rename(WorldId, Name, LastError))
        RefreshSavedWorlds();
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
            LastError = TEXT("Settings did not finish changing. Please try opening the world again.");
        }
        else if (FlushSettings())
        {
            bWorldPending = false;
            bTravelIssued = true;
            UE_LOG(LogUnnamedMenu, Display, TEXT("Settings flushed; opening %s%s%s"), *PendingMap,
                PendingOptions.IsEmpty() ? TEXT("") : TEXT("?"), *PendingOptions);
            UGameplayStatics::OpenLevel(Controller.Get(), FName(*PendingMap), true, PendingOptions);
            return;
        }
        bWorldPending = false;
        UE_LOG(LogUnnamedMenu, Warning, TEXT("World travel cancelled: %s"), *LastError);
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
        UE_LOG(LogUnnamedMenu, Display, TEXT("Settings flushed; requesting normal game shutdown"));
        UKismetSystemLibrary::QuitGame(Controller.Get(), Controller.Get(), EQuitPreference::Quit, false);
        return;
    }
    bQuitPending = false;
    UE_LOG(LogUnnamedMenu, Warning, TEXT("Quit cancelled: %s"), *LastError);
}
