#include "Input/UnnamedInputSettings.h"
#include "UnnamedFiles.h"
#include "UnnamedSession.h"
#include "Engine/LocalPlayer.h"
#include "EnhancedInputDeveloperSettings.h"
#include "Kismet/GameplayStatics.h"
#include "Misc/Paths.h"

bool UUnnamedInputSettings::SaveChecked(FString& OutError)
{
    OutError.Empty();
    if (!UnnamedSession::CanSaveSettings()) return true;
    ULocalPlayer* Player = GetLocalPlayer();
    if (!Player || !WriteSlot(GetSaveFilename(Player)))
    {
        OutError = TEXT("Input settings changed for this session, but could not be saved. Please try again.");
        return false;
    }
    return true;
}

bool UUnnamedInputSettings::WriteSlot(const FString& SlotName)
{
    TArray<uint8> Bytes;
    return UGameplayStatics::SaveGameToMemory(this, Bytes) &&
        UnnamedFiles::SaveBytesAtomically(GetSlotPath(SlotName), Bytes) == UnnamedFiles::ESaveResult::Saved;
}

FString UUnnamedInputSettings::GetSlotPath(const FString& SlotName)
{
    return FPaths::ProjectSavedDir() / TEXT("SaveGames") / SlotName + TEXT(".sav");
}

FString UUnnamedInputSettings::GetDefaultSlotName()
{
    return GetDefault<UEnhancedInputDeveloperSettings>()->InputSettingsSaveSlotName;
}

bool UUnnamedInputSettings::RestoreSlot(const FString& SlotName)
{
    return UnnamedFiles::RestoreFromBackup(GetSlotPath(SlotName));
}
