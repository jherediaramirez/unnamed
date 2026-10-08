#pragma once

#include "CoreMinimal.h"

// The voxel world's one log category, shared by this core module and the Unreal side (Source/Unnamed/Voxel).
// Raise it with -LogCmds="LogUnnamedVoxel Verbose".
UNNAMEDVOXEL_API DECLARE_LOG_CATEGORY_EXTERN(LogUnnamedVoxel, Log, All);
