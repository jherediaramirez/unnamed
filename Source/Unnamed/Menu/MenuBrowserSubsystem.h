#pragma once

#include "CoreMinimal.h"
#include "Containers/Ticker.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "MenuBrowserSubsystem.generated.h"

class APlayerController;
class IWebBrowserWindow;
class SMenuLoadingOverlay;
class SWebBrowser;
class UMenuApplication;
class UMenuInputBridge;
class UMenuPreferences;
class UMenuTitleBridge;
class UMenuVideoSettings;
class UUnnamedInputSubsystem;

// What the menu map shows when it takes the kept title browser.
enum class EMenuTitleReveal : uint8
{
    // The browser's last paint is a title for this window size and these settings: shown in the map's first frame.
    Now,
    // Vanilla's loading overlay until the page reports a freshly painted title (game start, or the look changed).
    AfterReady
};

// menu.Title.Keep 1 and 2: one browser for the title screen for the whole game. The first menu map makes it, under
// the Mojang Studios loading overlay until the page has painted the title. Leaving the menu map takes it out of
// the viewport (map travel itself keeps viewport widgets), where CEF stops painting it. When a world has loaded, the
// page goes back to the title out of view: the subsystem keeps CEF painting it into its texture, as a Slate tick would,
// until the page reports that title painted, so Return to Main Menu shows a fresh title in the menu map's first frame.
// A change that would make that paint stale (the window size, the saved video settings or preferences) paints it
// again, still out of view.
// It owns the page's services: menuapplication, menuinput, menuvideo and menutitle (menupreferences is the shared
// UMenuPreferences).
UCLASS()
class UNNAMED_API UMenuBrowserSubsystem : public UGameInstanceSubsystem
{
    GENERATED_BODY()
public:
    // menu.Title.Keep (0-2), read by the menu map's BeginPlay.
    static int32 GetKeepMode();
    static EMenuTitleReveal GetTitleReveal(bool bTitlePainted, bool bSameLook);

    virtual void Initialize(FSubsystemCollectionBase& Collection) override;
    virtual void Deinitialize() override;

    // The menu map's BeginPlay (UMenuBrowserHost::ShowTitle): makes the browser once, points the application at Player,
    // turns gameplay input off and adds the browser at Z 100, shown at once or under the overlay until the title is
    // ready. Mode 1 fades the overlay out as vanilla does; mode 2 cuts to the title.
    TSharedPtr<SWebBrowser> Attach(APlayerController& Player, int32 Mode);
    // The menu map's EndPlay: out of the viewport; the browser and its bindings stay.
    void Detach();
    // Destroys the browser and its bindings (menu.Title.Keep 0 on a later menu map; game shutdown).
    void Release();
    // From the page (menutitle.TitleReady): Token is the reset it answers, -1 a freshly loaded document.
    void HandleTitleReady(int32 Token);

    TSharedPtr<SWebBrowser> GetBrowser() const { return Browser; }
    bool IsTitlePainted() const { return bTitlePainted; }
    bool IsOverlayShown() const { return Overlay.IsValid(); }
    // Shown and taking input on the menu map.
    bool IsRevealed() const { return bRevealed; }
    UMenuVideoSettings* GetVideoSettings() const { return VideoSettings; }
    UMenuApplication* GetApplication() const { return Application; }
    UMenuInputBridge* GetInputBridge() const { return InputBridge; }
    UMenuPreferences* GetPreferences() const { return Preferences; }

private:
    // What a painted title depends on beyond the page itself.
    struct FLook
    {
        FIntPoint Viewport = FIntPoint::ZeroValue;
        int32 PreferencesRevision = -1;
        uint32 VideoSaves = 0;
        bool operator==(const FLook&) const = default;
    };
    enum class EPhase : uint8
    {
        None,
        Attached,
        Prepainting,
        Detached
    };
    FLook GetLook() const;
    void CreateBrowser(APlayerController& Player, UUnnamedInputSubsystem* PlayerInput);
    void AddToViewport();
    void RemoveFromViewport();
    // While the page repaints out of view: what the browser's viewport does on every Slate tick.
    void KeepPainting();
    void RequestReset();
    void ShowOverlay();
    void HideOverlay();
    void Reveal();
    void HandlePostLoadMap(UWorld* World);
    void StartPrepaint();
    bool Tick(float DeltaTime);

    TSharedPtr<SWebBrowser> Browser;
    // Browser's CEF window (inside its SWebBrowserView).
    TSharedPtr<IWebBrowserWindow> BrowserWindow;
    TSharedPtr<SMenuLoadingOverlay> Overlay;
    TWeakObjectPtr<APlayerController> Player;
    EPhase Phase = EPhase::None;
    int32 Mode = 1;
    // The last reset sent to the page (0: none yet, so only the document's own first paint counts).
    int32 Token = 0;
    bool bInViewport = false;
    bool bTitlePainted = false;
    bool bRevealed = false;
    bool bAllowNativeVideo = false;
    FLook PaintedLook, RequestedLook;
    double MadeSeconds = 0, AttachSeconds = 0, PrepaintSeconds = 0;
    FDelegateHandle PostLoadMapHandle;
    FTSTicker::FDelegateHandle TickHandle;
    UPROPERTY(Transient)
    TObjectPtr<UMenuVideoSettings> VideoSettings;
    UPROPERTY(Transient)
    TObjectPtr<UMenuApplication> Application;
    UPROPERTY(Transient)
    TObjectPtr<UMenuInputBridge> InputBridge;
    UPROPERTY(Transient)
    TObjectPtr<UMenuPreferences> Preferences;
    UPROPERTY(Transient)
    TObjectPtr<UMenuTitleBridge> TitleBridge;
};
