#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "MenuBrowserHost.generated.h"

class APlayerController;
class SBackgroundBlur;
class SWebBrowser;
class UMenuApplication;
class UMenuBrowserSubsystem;
class UMenuInputBridge;
class UMenuPreferences;
class UMenuVideoSettings;

// Shows the offline CEF menu in its owning player controller's viewport and binds the
// native menu services to the page: menuvideo, menuinput, menupreferences and menuapplication,
// plus any bridges the owner adds. Any controller that needs the menu adds this component.
UCLASS(ClassGroup = Menu)
class UNNAMED_API UMenuBrowserHost : public UActorComponent
{
    GENERATED_BODY()
public:
    UMenuBrowserHost();

    // A world's pause menu (AFoundationController, foundation.Pause.Menu); set before Prewarm or Show. 0 (the menu
    // map's only mode): Show adds a fresh opaque browser and Hide destroys it. 1: one browser for the world session,
    // made hidden by Prewarm; Show reveals it and Hide collapses it. 2 (diagnostic): a fresh browser per Show, as 0,
    // but transparent until its pause screen has painted. Modes 1 and 2 open the page as PauseMenuUrl(Base, Mode).
    void SetPauseMode(int32 Mode);
    int32 GetPauseMode() const { return PauseMode; }
    // foundation.Pause.Background in modes 1 and 2: 0 the page's own panorama; 1 the world behind the page
    // (#pause&bg=world), blurred by a Slate background blur under the browser (Z 99) while the menu is shown.
    void SetPauseBackground(int32 Background);
    int32 GetPauseBackground() const { return PauseBackground; }
    // Mode 1: creates the browser once, on the pause screen, transparent and HitTestInvisible; it collapses when the
    // page reports its pause screen painted (HandlePageReady). Binds every service but changes no input.
    void Prewarm();

    // Adds the browser (or, in mode 1, reveals it), binds every service and gives it UI-only input with a cursor.
    void Show();
    // The menu map at menu.Title.Keep 1 and 2 (mode 0 only): takes the game's kept title browser and its services from
    // UMenuBrowserSubsystem instead of making them; Hide and EndPlay give it back.
    void ShowTitle(int32 KeepMode);
    // Removes the browser and its bindings (in mode 1, hides it and keeps both). Restoring game input is the owner's
    // job.
    void Hide();
    // True while the menu is shown.
    bool IsOpen() const { return PauseMode == 1 ? bShown : Browser.IsValid(); }
    // True while a browser exists, shown or not.
    bool HasBrowser() const { return Browser.IsValid(); }
    // The page's browser while it exists (the pause probe reads IsLoaded).
    TSharedPtr<SWebBrowser> GetBrowser() const { return Browser; }

    // The page in modes 1 and 2: the base URL at 0, base#pause at 1 and 2, plus &bg=world for Background 1.
    static FString PauseMenuUrl(const FString& Base, int32 Mode, int32 Background = 0);
    // The world-background blur's Slate strength: Menu Background Blur (0-10) x viewport height / 1080. A chosen
    // mapping: how it compares with vanilla's blur is unmeasured.
    static float PauseBlurStrength(int32 Setting, float ViewportHeight);

    // From the page (UFoundationMenuBridge): its pause screen has painted. Collapses a pre-warmed browser that is not
    // shown; when shown, also calls the page's reveal(). Returns IsOpen(), so a shown page reveals itself either way.
    bool HandlePageReady();
    // From the page: the first frame after it was shown. State is the page's own report ({"guiScale": n, "jump": key}).
    void HandlePageShown(const FString& State);
    bool IsPageReady() const { return bPageReady; }
    // FPlatformTime seconds of the last Prewarm or Show, of the page's ready report and of its first shown report
    // since that Show (0 = none yet).
    double GetStartSeconds() const { return StartSeconds; }
    double GetPageReadySeconds() const { return PageReadySeconds; }
    double GetPageShownSeconds() const { return PageShownSeconds; }
    const FString& GetPageShownState() const { return PageShownState; }

    // Bound under Name every time the page is shown, e.g. a world's pause-menu bridge.
    void AddBridge(const FString& Name, UObject* Bridge);

    // Video settings live as long as the host so window changes work while the menu is hidden.
    UMenuVideoSettings* GetVideoSettings();

    // file:/// URL of Content/Menu/Web/index.html.
    static FString GetMenuUrl();
    // The menu's browser for URL, not yet in the viewport or parented. Transparent: CEF paints nothing (alpha 0) before
    // the document.
    static TSharedRef<SWebBrowser> MakeBrowser(const FString& MenuURL, bool bTransparent);
    // Parents a browser to the game viewport's window (after it is added to the viewport).
    static void SetBrowserParentWindow(SWebBrowser& InBrowser);
    // UI-only input focused on the browser, cursor on, move and look ignored.
    static void FocusBrowser(APlayerController& Player, const TSharedPtr<SWebBrowser>& InBrowser);

    virtual void TickComponent(
        float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;
    virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

private:
    APlayerController* GetPlayerController() const;
    // Makes Browser for URL and adds it to the game viewport at Z 100.
    void CreateBrowser(const FString& MenuURL, bool bTransparent);
    // Binds menuvideo, menuinput, menupreferences, menuapplication and the extra bridges. bDisableGameplay turns
    // gameplay input off when the input bridge is made (Show; Prewarm leaves input alone).
    void BindServices(APlayerController& Player, bool bDisableGameplay);
    // Removes the browser and its bindings and releases the services (Hide in modes 0 and 2, EndPlay); a kept title
    // browser only leaves the viewport.
    void DestroyBrowser();
    // Adds (or removes) the world-background blur under the browser; Background 1 in modes 1 and 2 only.
    void ShowBlur(bool bShow);
    float GetBlurStrength() const;

    TSharedPtr<SWebBrowser> Browser;
    TSharedPtr<SBackgroundBlur> Blur;
    int32 PauseMode = 0;
    int32 PauseBackground = 0;
    bool bShown = false;
    bool bPageReady = false;
    // ShowTitle: Browser and the services belong to KeptTitles.
    bool bKeptTitle = false;
    TWeakObjectPtr<UMenuBrowserSubsystem> KeptTitles;
    double StartSeconds = 0, PageReadySeconds = 0, PageShownSeconds = 0;
    FString PageShownState;
    UPROPERTY(Transient)
    TObjectPtr<UMenuVideoSettings> VideoSettings;
    UPROPERTY(Transient)
    TObjectPtr<UMenuApplication> Application;
    UPROPERTY(Transient)
    TObjectPtr<UMenuInputBridge> InputBridge;
    UPROPERTY(Transient)
    TObjectPtr<UMenuPreferences> Preferences;
    UPROPERTY(Transient)
    TMap<FString, TObjectPtr<UObject>> ExtraBridges;
};
