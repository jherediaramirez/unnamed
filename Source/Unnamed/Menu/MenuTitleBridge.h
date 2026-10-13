#pragma once

#include "CoreMinimal.h"
#include "UObject/Object.h"
#include "MenuTitleBridge.generated.h"

class UMenuBrowserSubsystem;

// Browser binding "menutitle" (JS MenuTitle), bound only on the kept title browser (menu.Title.Keep 1 and 2): the page
// reports that its title screen has painted, after loading or after native asked it to go back to the title.
UCLASS()
class UNNAMED_API UMenuTitleBridge : public UObject
{
    GENERATED_BODY()
public:
    void Initialize(UMenuBrowserSubsystem* InTitles) { Titles = InTitles; }

    // The title and the panorama have painted, then two more frames. Token: the reset it answers (MenuTitle.reset),
    // -1 for a freshly loaded document.
    UFUNCTION()
    void TitleReady(int32 Token);

private:
    TWeakObjectPtr<UMenuBrowserSubsystem> Titles;
};
