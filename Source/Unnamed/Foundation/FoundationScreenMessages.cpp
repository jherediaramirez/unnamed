#include "Foundation/FoundationScreenMessages.h"
#include "Engine/Engine.h"
#include "HAL/IConsoleManager.h"
#include "Misc/CommandLine.h"
#include "Misc/CoreDelegates.h"
#include "Misc/DelayedAutoRegister.h"
#include "Misc/Parse.h"

namespace
{
    TAutoConsoleVariable<int32> CVarScreenMessages(TEXT("foundation.ScreenMessages"),
        FoundationScreenMessages::DefaultMode,
        TEXT("Unreal's on-screen messages (the renderer's yellow warnings, debug text): -1 hidden in normal play and "
             "shown in probe and capture runs, 0 hidden, 1 shown (the look until 2026-10-11). The log keeps them."),
        ECVF_Default);

    void ApplyScreenMessages()
    {
        // The editor's own sessions keep Unreal's behaviour; -game runs and packages follow the switch.
        if (GIsEditor || !GEngine) return;
        const bool bShow = FoundationScreenMessages::ShouldShow(
            CVarScreenMessages.GetValueOnGameThread(), FoundationScreenMessages::IsProbeRun(FCommandLine::Get()));
        GAreScreenMessagesEnabled = bShow;
        GEngine->bEnableOnScreenDebugMessages = bShow;
    }

    FDelayedAutoRegisterHelper RegisterScreenMessages(EDelayedRegisterRunPhase::EndOfEngineInit,
        []
        {
            ApplyScreenMessages();
            CVarScreenMessages->SetOnChangedCallback(
                FConsoleVariableDelegate::CreateLambda([](IConsoleVariable*) { ApplyScreenMessages(); }));
        });
}

bool FoundationScreenMessages::ShouldShow(int32 Mode, bool bProbeRun)
{
    return Mode == 1 || (Mode < 0 && bProbeRun);
}

bool FoundationScreenMessages::IsProbeRun(const TCHAR* CommandLine)
{
    FString Value;
    for (const TCHAR* Key :
        {TEXT("FoundationProbe="), TEXT("VoxelShot="), TEXT("VoxelFlight="), TEXT("VoxelLoadingShot=")})
        if (FParse::Value(CommandLine, Key, Value)) return true;
    return false;
}
