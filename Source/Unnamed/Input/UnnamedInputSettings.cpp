#include "UnnamedInputSettings.h"
#include "Engine/LocalPlayer.h"
#include "Kismet/GameplayStatics.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"

bool UUnnamedInputSettings::SaveChecked(FString& OutError)
{
    OutError.Empty();
    // Editor Play and diagnostic sessions must never overwrite normal preferences.
    if (GIsEditor || FParse::Param(FCommandLine::Get(), TEXT("MenuSettingsSessionOnly"))) return true;
    ULocalPlayer* Player = GetLocalPlayer();
    if (!Player || !UGameplayStatics::SaveGameToSlot(this, GetSaveFilename(Player), Player->GetLocalPlayerIndex()))
    {
        OutError = TEXT("Input settings changed for this session, but could not be saved. Please try again.");
        return false;
    }
    return true;
}
