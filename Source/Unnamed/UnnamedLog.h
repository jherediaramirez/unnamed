#pragma once

#include "CoreMinimal.h"

// One log category per area; the project prefix keeps them distinct from engine categories.
// Raise one with e.g. -LogCmds="LogUnnamedFoundation Verbose".
DECLARE_LOG_CATEGORY_EXTERN(LogUnnamedMenu, Log, All); // Menu host, application, video and preferences.
DECLARE_LOG_CATEGORY_EXTERN(LogUnnamedWorlds, Log, All); // World travel targets such as Super Flat.
DECLARE_LOG_CATEGORY_EXTERN(LogUnnamedFoundation, Log, All); // Player Foundation and its runtime probe.
