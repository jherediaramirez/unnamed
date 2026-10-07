#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "FoundationPanels.generated.h"

class AFoundationController;
class SWidget;

// Blocking native panels over gameplay: Debug Options (F3+F6), the local chat/command text
// fixture and the inventory routing fixture. Opening one stops gameplay input and the
// simulation; closing it resumes play unless the pause menu is open.
UCLASS(ClassGroup = Foundation)
class UNNAMED_API UFoundationPanels : public UActorComponent
{
    GENERATED_BODY()
public:
    // Toggles: a second request while any panel is open closes it.
    void OpenDebugOptions();
    void OpenText(bool bCommand);
    void OpenInventory();
    void Close();

    bool IsOpen() const { return bOpen; }
    bool IsTextOpen() const { return bTextOpen; }
    bool IsInventoryOpen() const { return bInventoryOpen; }

    virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

private:
    AFoundationController* GetFoundationController() const;
    void Show(TSharedRef<SWidget> Content);

    TSharedPtr<SWidget> Root;
    bool bOpen = false;
    bool bTextOpen = false;
    bool bInventoryOpen = false;
};
