#pragma once

#include "CoreMinimal.h"

// Unreal's on-screen messages: the renderer's yellow warnings (such as "[VSM] Non-Nanite Marking Job Queue overflow")
// and AddOnScreenDebugMessage text. Vanilla shows no engine text, so normal play hides them; probe and capture runs
// keep them, and the log keeps every message either way. foundation.ScreenMessages: -1 that choice (the default), 0
// always hidden, 1 always shown (the look until 2026-10-11). Editor sessions (not -game) are left alone.
namespace FoundationScreenMessages
{
    constexpr int32 DefaultMode = -1;
    // Whether the messages show for a mode in a run that is, or is not, a probe or capture.
    UNNAMED_API bool ShouldShow(int32 Mode, bool bProbeRun);
    // A run with -FoundationProbe=, -VoxelShot=, -VoxelFlight= or -VoxelLoadingShot= on its command line.
    UNNAMED_API bool IsProbeRun(const TCHAR* CommandLine);
}
