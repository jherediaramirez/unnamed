#include "Input/UnnamedInputSettings.h"
#include "UnnamedSession.h"
#include "Engine/LocalPlayer.h"
#include "Kismet/GameplayStatics.h"

bool UUnnamedInputSettings::SaveChecked(FString& OutError)
{
    OutError.Empty();
    if (!UnnamedSession::CanSaveSettings()) return true;
    ULocalPlayer* Player = GetLocalPlayer();
    if (!Player || !UGameplayStatics::SaveGameToSlot(this, GetSaveFilename(Player), Player->GetLocalPlayerIndex()))
    {
        OutError = TEXT("Input settings changed for this session, but could not be saved. Please try again.");
        return false;
    }
    return true;
}
