#include "Menu/MenuTitleBridge.h"
#include "Menu/MenuBrowserSubsystem.h"

void UMenuTitleBridge::TitleReady(int32 Token)
{
    if (Titles.IsValid()) Titles->HandleTitleReady(Token);
}
