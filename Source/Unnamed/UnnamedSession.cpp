#include "UnnamedSession.h"

#include "Misc/CommandLine.h"
#include "Misc/Parse.h"

bool UnnamedSession::CanSaveSettings()
{
    return !GIsEditor && !FParse::Param(FCommandLine::Get(), TEXT("MenuSettingsSessionOnly"));
}
