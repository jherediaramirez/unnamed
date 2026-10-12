#include "Input/UnnamedInputRestore.h"
#include "Input/UnnamedInputSettings.h"
#include "UnnamedLog.h"
#include "UnnamedSession.h"
#include "Engine/GameInstance.h"

void UUnnamedInputRestore::Initialize(FSubsystemCollectionBase& Collection)
{
    Super::Initialize(Collection);
    if (!UnnamedSession::CanSaveSettings()) return;
    const FString Slot = UUnnamedInputSettings::GetDefaultSlotName();
    if (UUnnamedInputSettings::RestoreSlot(Slot))
        UE_LOG(LogUnnamedMenu, Warning,
            TEXT("Input settings: %s was missing or empty after an interrupted save; restored it from %s.bak (local "
                 "players: %d)"),
            *UUnnamedInputSettings::GetSlotPath(Slot), *UUnnamedInputSettings::GetSlotPath(Slot),
            GetGameInstance()->GetNumLocalPlayers());
}
