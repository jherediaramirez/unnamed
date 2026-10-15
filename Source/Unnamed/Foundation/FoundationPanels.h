#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "FoundationPanels.generated.h"

class AFoundationController;
class AFoundationLab;
class AFoundationPawn;
class AFoundationPlayground;
class SHudContainerScreen;
enum class EFoundationMenuKind : uint8;
class SHudCreativeScreen;
class SWidget;

// Blocking native panels over gameplay: Debug Options (F3+F6), the local chat/command text
// fixture and the inventory: in a creative lab the creative inventory screen (SHudCreativeScreen), elsewhere
// the survival inventory screen (SHudContainerScreen; foundation.Inventory.Screen 0: the inventory routing fixture).
// Opening one stops gameplay input and the simulation; closing it resumes play unless the pause menu is open.
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
    // The open chat's typed line (step 15: the HUD draws it in vanilla's input bar); empty when closed.
    FString GetTypedText() const;
    // The caret's character index in the typed line (0 to its length; the length when the box has not reported one).
    int32 GetTypedCaret() const;
    bool IsInventoryOpen() const { return bInventoryOpen; }
    // The creative screen's search tab takes typed text: the inventory key types instead of closing.
    bool IsTypingInInventory() const;
    TSharedPtr<SHudCreativeScreen> GetCreativeScreen() const { return Creative; }
    TSharedPtr<SHudContainerScreen> GetSurvivalScreen() const { return Survival; }
    // The survival inventory screen (foundation.Inventory.Screen 1, outside creative). Public for probes, which open
    // it in the creative Playground too.
    void OpenSurvivalInventory(AFoundationLab& Lab);
    // A container's screen over the menu the authority opened (survival step 7: a crafting table's, ContainerId from
    // the block use's result); the inventory's is container 0. Closing sends ContainerClose, which returns its grid.
    // False for a menu with no screen yet (the furnace's, until step 8a2): nothing opens. The player's simulation keeps
    // running under it with the input zeroed (Codex review 12, finding 2).
    bool OpenContainerScreen(AFoundationLab& Lab, EFoundationMenuKind Kind, int32 ContainerId);
    // The open survival screen's container id (0 the inventory's), or INDEX_NONE without one.
    int32 GetOpenContainer() const { return Survival.IsValid() ? SurvivalContainer : INDEX_NONE; }
    // foundation.Panels.Preload 1: builds the creative screen before its first open, so its textures load now and the
    // first open neither builds it nor draws a frame without them. Returns false while the player's lab is not ready
    // (try again next frame); true once built, or when the lab has no creative inventory.
    bool PreloadCreative();
    bool HasPreloadedCreative() const { return PreloadedCreative.IsValid(); }

    virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

private:
    AFoundationController* GetFoundationController() const;
    // Bare: no centred dark frame (the chat's invisible input; the HUD draws the bar).
    void Show(TSharedRef<SWidget> Content, bool bBare = false);
    void OpenCreative(AFoundationLab& Lab);
    // The creative screen for the controller's pawn in Lab, not yet shown.
    TSharedRef<SHudCreativeScreen> MakeCreative(AFoundationLab& Lab);

    TSharedPtr<SWidget> Root;
    TSharedPtr<SHudCreativeScreen> Creative;
    TSharedPtr<SHudContainerScreen> Survival;
    // The player the survival screen sends its clicks for; Close sends its ContainerClose.
    TWeakObjectPtr<AFoundationPawn> SurvivalPawn;
    int32 SurvivalContainer = INDEX_NONE;
    // PreloadCreative's screen and the pawn it was built for; the first open takes it.
    TSharedPtr<SHudCreativeScreen> PreloadedCreative;
    TWeakObjectPtr<AFoundationPawn> PreloadedPawn;
    // The lab's rules it was built from (a world started again builds a new authority); compared, never dereferenced.
    const void* PreloadedAuthority = nullptr;
    bool bOpen = false;
    bool bTextOpen = false;
    TSharedPtr<SEditableTextBox> TextBox;
    // The box's last reported caret offset (OnCursorMovedWithSelection); INDEX_NONE until it reports.
    int32 TypedCaret = INDEX_NONE;
    bool bInventoryOpen = false;
};
