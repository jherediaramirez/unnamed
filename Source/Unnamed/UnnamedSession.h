#pragma once

#include "CoreMinimal.h"

namespace UnnamedSession
{
    // False in the editor (including Play) and for launches with -MenuSettingsSessionOnly:
    // changes then stay in memory and never overwrite the player's saved settings.
    UNNAMED_API bool CanSaveSettings();
}
