#include "Menu/MenuBrowserSubsystem.h"
#include "UnnamedLog.h"
#include "Input/UnnamedInputSubsystem.h"
#include "Menu/MenuApplication.h"
#include "Menu/MenuBrowserHost.h"
#include "Menu/MenuGameMode.h"
#include "Menu/MenuInputBridge.h"
#include "Menu/MenuLoadingOverlay.h"
#include "Menu/MenuPreferences.h"
#include "Menu/MenuTitleBridge.h"
#include "Menu/MenuVideoSettings.h"
#include "Dom/JsonValue.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/GameViewportClient.h"
#include "Engine/LocalPlayer.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "HAL/IConsoleManager.h"
#include "IWebBrowserWindow.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "SWebBrowser.h"
#include "SWebBrowserView.h"
#include "Widgets/SWindow.h"
#include "UObject/UObjectGlobals.h"

namespace
{
    TAutoConsoleVariable<int32> CVarTitleKeep(TEXT("menu.Title.Keep"), 1,
        TEXT("The title screen's browser: 0 a fresh opaque browser each time the menu map starts (until 2026-10-12: "
             "about 0.6 s of black at game start and after Return to Main Menu); 1 one browser for the whole game, "
             "switched back to the title while each world starts, so Return to Main Menu shows the title at once, and "
             "a Mojang Studios loading overlay over it at game start until the title has painted, then a 2 s fade-out "
             "into the title; 2 the same, but the overlay cuts to the title with no fade. Read when the menu map "
             "starts."),
        ECVF_Default);
    // A hidden reset that never reports gives up after this long: the page leaves the viewport, and the next menu map
    // shows the overlay until a fresh title.
    constexpr double MenuTitlePrepaintTimeout = 5.0;
    // The overlay never stays longer than this on a menu map: a page that never reports is shown as it is, with input.
    constexpr double MenuTitleOverlayTimeout = 10.0;
    const TCHAR* MenuTitleCall = TEXT("typeof MenuTitle!=='undefined'&&MenuTitle.");

    // The CEF window inside an SWebBrowser (its SWebBrowserView child).
    TSharedPtr<IWebBrowserWindow> FindMenuBrowserWindow(SWidget& Widget)
    {
        if (Widget.GetType() == FName(TEXT("SWebBrowserView")))
            return static_cast<SWebBrowserView&>(Widget).GetBrowserWindow();
        FChildren* Children = Widget.GetChildren();
        for (int32 Index = 0; Children && Index < Children->Num(); ++Index)
            if (TSharedPtr<IWebBrowserWindow> Window = FindMenuBrowserWindow(*Children->GetChildAt(Index)))
                return Window;
        return nullptr;
    }
}

int32 UMenuBrowserSubsystem::GetKeepMode()
{
    return FMath::Clamp(CVarTitleKeep.GetValueOnGameThread(), 0, 2);
}

EMenuTitleReveal UMenuBrowserSubsystem::GetTitleReveal(bool bTitlePainted, bool bSameLook)
{
    return bTitlePainted && bSameLook ? EMenuTitleReveal::Now : EMenuTitleReveal::AfterReady;
}

void UMenuBrowserSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
    Super::Initialize(Collection);
    Collection.InitializeDependency<UMenuPreferences>();
    PostLoadMapHandle = FCoreUObjectDelegates::PostLoadMapWithWorld.AddUObject(this, &ThisClass::HandlePostLoadMap);
    TickHandle = FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateUObject(this, &ThisClass::Tick));
}

void UMenuBrowserSubsystem::Deinitialize()
{
    FCoreUObjectDelegates::PostLoadMapWithWorld.Remove(PostLoadMapHandle);
    FTSTicker::GetCoreTicker().RemoveTicker(TickHandle);
    // The game instance shuts down before the WebBrowser module, so CEF is still there to close the page.
    Release();
    Super::Deinitialize();
}

UMenuBrowserSubsystem::FLook UMenuBrowserSubsystem::GetLook() const
{
    FLook Look;
    FVector2D Size(0, 0);
    if (GEngine && GEngine->GameViewport) GEngine->GameViewport->GetViewportSize(Size);
    Look.Viewport = FIntPoint(FMath::RoundToInt32(Size.X), FMath::RoundToInt32(Size.Y));
    Look.PreferencesRevision = Preferences ? Preferences->GetRevision() : -1;
    Look.VideoSaves = UMenuVideoSettings::GetSaveCount();
    return Look;
}

void UMenuBrowserSubsystem::CreateBrowser(APlayerController& InPlayer, UUnnamedInputSubsystem* PlayerInput)
{
    // The same page and browser settings as a fresh menu map's (UMenuBrowserHost::Show, mode 0).
    Browser = UMenuBrowserHost::MakeBrowser(UMenuBrowserHost::GetMenuUrl(), false);
    BrowserWindow = FindMenuBrowserWindow(*Browser);
    if (!BrowserWindow.IsValid())
        UE_LOG(
            LogUnnamedMenu, Warning, TEXT("Title browser: no CEF window found; Return to Main Menu shows the overlay"));
    MadeSeconds = FPlatformTime::Seconds();
    Token = 0;
    bAllowNativeVideo = !GIsEditor && InPlayer.GetWorld()->WorldType == EWorldType::Game &&
        !FParse::Param(FCommandLine::Get(), TEXT("RenderOffscreen"));
    VideoSettings = NewObject<UMenuVideoSettings>(this);
    VideoSettings->Initialize(bAllowNativeVideo);
    InputBridge = NewObject<UMenuInputBridge>(this);
    InputBridge->Initialize(PlayerInput);
    Preferences = GetGameInstance()->GetSubsystem<UMenuPreferences>();
    Application = NewObject<UMenuApplication>(this);
    TitleBridge = NewObject<UMenuTitleBridge>(this);
    TitleBridge->Initialize(this);
    Browser->BindUObject(TEXT("menuvideo"), VideoSettings, true);
    Browser->BindUObject(TEXT("menuinput"), InputBridge, true);
    Browser->BindUObject(TEXT("menupreferences"), Preferences, true);
    Browser->BindUObject(TEXT("menuapplication"), Application, true);
    Browser->BindUObject(TEXT("menutitle"), TitleBridge, true);
    RequestedLook = GetLook();
}

TSharedPtr<SWebBrowser> UMenuBrowserSubsystem::Attach(APlayerController& InPlayer, int32 InMode)
{
    Mode = InMode;
    Player = &InPlayer;
    AttachSeconds = FPlatformTime::Seconds();
    UUnnamedInputSubsystem* PlayerInput = InPlayer.GetLocalPlayer()->GetSubsystem<UUnnamedInputSubsystem>();
    const bool bMade = !Browser.IsValid();
    // The look the cached paint is compared with, before this map's own settings objects read anything again.
    const FLook Look = GetLook();
    if (bMade)
        CreateBrowser(InPlayer, PlayerInput);
    else
    {
        // What a fresh menu map's services would read: the saved video settings (applied to the window as they were)
        // and the player's input.
        VideoSettings->Reinitialize(bAllowNativeVideo);
        InputBridge->Initialize(PlayerInput);
    }
    Application->Initialize(&InPlayer, VideoSettings, InputBridge, Preferences);
    if (PlayerInput) PlayerInput->SetGameplayInputEnabled(false);
    AddToViewport();
    if (bMade) UMenuBrowserHost::SetBrowserParentWindow(*Browser);
    Phase = EPhase::Attached;
    bRevealed = false;
    const EMenuTitleReveal Decision = GetTitleReveal(bTitlePainted, Look == PaintedLook);
    UE_LOG(LogUnnamedMenu, Display, TEXT("Title browser %s, %s"), bMade ? TEXT("made") : TEXT("kept"),
        Decision == EMenuTitleReveal::Now ? TEXT("shown at once (its title painted before this map)")
                                          : TEXT("under the loading overlay until its title has painted"));
    if (Decision == EMenuTitleReveal::Now)
    {
        Reveal();
        return Browser;
    }
    Browser->SetVisibility(EVisibility::HitTestInvisible);
    ShowOverlay();
    // A loaded page goes back to the title now; a new one reports its first paint by itself.
    if (!bMade && Browser->IsLoaded()) RequestReset();
    // Vanilla's overlay takes no input: nothing is focused until the title is revealed.
    InPlayer.bShowMouseCursor = true;
    InPlayer.SetInputMode(FInputModeUIOnly());
    return Browser;
}

void UMenuBrowserSubsystem::Detach()
{
    if (!Browser.IsValid()) return;
    HideOverlay();
    RemoveFromViewport();
    // The page still shows the screen it left on ("Preparing for world creation...", "Reading world data..."): not a
    // title to show again until the next reset reports.
    bTitlePainted = false;
    bRevealed = false;
    Phase = EPhase::Detached;
    Player.Reset();
}

void UMenuBrowserSubsystem::Release()
{
    HideOverlay();
    if (!Browser.IsValid()) return;
    RemoveFromViewport();
    if (VideoSettings) Browser->UnbindUObject(TEXT("menuvideo"), VideoSettings, true);
    if (InputBridge) Browser->UnbindUObject(TEXT("menuinput"), InputBridge, true);
    if (Preferences) Browser->UnbindUObject(TEXT("menupreferences"), Preferences, true);
    if (Application) Browser->UnbindUObject(TEXT("menuapplication"), Application, true);
    if (TitleBridge) Browser->UnbindUObject(TEXT("menutitle"), TitleBridge, true);
    Browser.Reset();
    BrowserWindow.Reset();
    VideoSettings = nullptr;
    Application = nullptr;
    InputBridge = nullptr;
    Preferences = nullptr;
    TitleBridge = nullptr;
    Phase = EPhase::None;
    bTitlePainted = bRevealed = false;
    Player.Reset();
    UE_LOG(LogUnnamedMenu, Display, TEXT("Title browser released"));
}

void UMenuBrowserSubsystem::AddToViewport()
{
    if (bInViewport || !GEngine || !GEngine->GameViewport) return;
    GEngine->GameViewport->AddViewportWidgetContent(Browser.ToSharedRef(), 100);
    bInViewport = true;
}

void UMenuBrowserSubsystem::RemoveFromViewport()
{
    if (!bInViewport) return;
    bInViewport = false;
    if (GEngine && GEngine->GameViewport) GEngine->GameViewport->RemoveViewportWidgetContent(Browser.ToSharedRef());
}

void UMenuBrowserSubsystem::KeepPainting()
{
    // CEF counts a browser as visible while its viewport reports a size every frame (FCEFWebBrowserWindow::
    // SetViewportSize and CheckTickActivity); it then paints into its texture though nothing draws it. The size is the
    // one the menu map's viewport will give it.
    if (!BrowserWindow.IsValid() || !GEngine || !GEngine->GameViewport) return;
    FVector2D Pixels(0, 0);
    GEngine->GameViewport->GetViewportSize(Pixels);
    const TSharedPtr<SWindow> Parent = BrowserWindow->GetParentWindow();
    const float Dpi = Parent.IsValid() && Parent->GetNativeWindow().IsValid()
        ? FMath::Max(Parent->GetNativeWindow()->GetDPIScaleFactor(), .01f)
        : 1.f;
    BrowserWindow->SetViewportSize(
        Pixels.X > 0 && Pixels.Y > 0 ? (Pixels / Dpi).IntPoint() : BrowserWindow->GetViewportSize());
}

void UMenuBrowserSubsystem::RequestReset()
{
    ++Token;
    bTitlePainted = false;
    RequestedLook = GetLook();
    Browser->ExecuteJavascript(FString::Printf(TEXT("%sreset(%d)"), MenuTitleCall, Token));
}

void UMenuBrowserSubsystem::HandleTitleReady(int32 InToken)
{
    // A document's own first paint counts only before any reset; a reset counts only if it is the latest, and only
    // while the page is painting (not after a repaint gave up: its texture may predate the report).
    if (!Browser.IsValid() || (InToken < 0 ? Token != 0 : InToken != Token) || Phase == EPhase::Detached ||
        Phase == EPhase::None)
    {
        UE_LOG(LogUnnamedMenu, Display, TEXT("Title ready ignored (token %d, expected %d)"), InToken, Token);
        return;
    }
    bTitlePainted = true;
    PaintedLook = RequestedLook;
    const double Now = FPlatformTime::Seconds();
    const double Since = Phase == EPhase::Prepainting ? PrepaintSeconds : Phase == EPhase::Attached ? AttachSeconds : 0;
    UE_LOG(LogUnnamedMenu, Display, TEXT("Title ready (token %d): %.1f ms after %s"), InToken, (Now - Since) * 1000,
        Phase == EPhase::Prepainting ? TEXT("the world started (hidden)") : TEXT("the menu map started"));
    if (Phase == EPhase::Prepainting)
        // No more viewport reports: CEF hides the page after a frame, and its last paint stays this title.
        Phase = EPhase::Detached;
    else if (Phase == EPhase::Attached && !bRevealed)
    {
        if (Overlay.IsValid() && Mode == 1)
        {
            // The title shows through the overlay's fade; input waits for its end (Tick).
            Overlay->SetTargetProgress(1);
            Overlay->StartFadeOut(Now);
        }
        else
            Reveal();
    }
}

void UMenuBrowserSubsystem::Reveal()
{
    HideOverlay();
    bRevealed = true;
    Browser->SetVisibility(EVisibility::Visible);
    if (APlayerController* Controller = Player.Get()) UMenuBrowserHost::FocusBrowser(*Controller, Browser);
    // The application's state now has this map's controller (world travel enabled again).
    Browser->ExecuteJavascript(FString(MenuTitleCall) + TEXT("shown()"));
    UE_LOG(LogUnnamedMenu, Display, TEXT("Title shown: %.1f ms after the menu map started"),
        (FPlatformTime::Seconds() - AttachSeconds) * 1000);
}

void UMenuBrowserSubsystem::ShowOverlay()
{
    if (Overlay.IsValid() || !GEngine || !GEngine->GameViewport) return;
    // Accessibility > Monochrome Logo: the black background.
    bool bMonochrome = false;
    if (Preferences)
        if (const TSharedPtr<const FJsonValue> Value = Preferences->FindPreference(TEXT("darkMojangStudiosBackground"));
            Value.IsValid() && Value->Type == EJson::Boolean)
            bMonochrome = Value->AsBool();
    Overlay = SNew(SMenuLoadingOverlay)
                  .Monochrome(bMonochrome)
                  .RequestedGuiScale_Lambda([WeakVideo = TWeakObjectPtr<UMenuVideoSettings>(VideoSettings)]
                      { return WeakVideo.IsValid() ? WeakVideo->GetGuiScale() : 0; });
    GEngine->GameViewport->AddViewportWidgetContent(Overlay.ToSharedRef(), 101);
}

void UMenuBrowserSubsystem::HideOverlay()
{
    if (!Overlay.IsValid()) return;
    if (GEngine && GEngine->GameViewport) GEngine->GameViewport->RemoveViewportWidgetContent(Overlay.ToSharedRef());
    Overlay.Reset();
}

void UMenuBrowserSubsystem::HandlePostLoadMap(UWorld* World)
{
    // Any world but the menu map, which attaches the browser itself.
    if (!Browser.IsValid() || !World || World->GetGameInstance() != GetGameInstance() ||
        Cast<AMenuGameMode>(World->GetAuthGameMode()))
        return;
    StartPrepaint();
}

void UMenuBrowserSubsystem::StartPrepaint()
{
    if (!Browser.IsValid() || !GEngine || !GEngine->GameViewport) return;
    // The saved values only: a world's own video settings own the window and the frame rate.
    VideoSettings->ReloadSaved();
    // Out of the viewport, never drawn: CEF paints it because it is told it is visible (KeepPainting, every frame).
    RemoveFromViewport();
    Phase = EPhase::Prepainting;
    PrepaintSeconds = FPlatformTime::Seconds();
    KeepPainting();
    // A page still loading reports its first paint by itself.
    if (Browser->IsLoaded()) RequestReset();
    UE_LOG(LogUnnamedMenu, Display, TEXT("Title browser repainting the title, hidden (token %d)"), Token);
}

bool UMenuBrowserSubsystem::Tick(float DeltaTime)
{
    if (!Browser.IsValid()) return true;
    const double Now = FPlatformTime::Seconds();
    if (Overlay.IsValid())
    {
        Overlay->SetTargetProgress(bTitlePainted ? 1.f : Browser->IsLoaded() ? .5f : 0.f);
        if (Overlay->IsFadedOut(Now))
            Reveal();
        else if (!bTitlePainted && Phase == EPhase::Attached && Now - AttachSeconds > MenuTitleOverlayTimeout)
        {
            UE_LOG(LogUnnamedMenu, Warning,
                TEXT("Title browser: no title painted %.0f s after the menu map started; "
                     "shown as it is"),
                MenuTitleOverlayTimeout);
            Reveal();
        }
    }
    if (Phase == EPhase::Prepainting && Now - PrepaintSeconds > MenuTitlePrepaintTimeout)
    {
        UE_LOG(LogUnnamedMenu, Warning, TEXT("Title browser: no title painted %.0f s after the world started"),
            MenuTitlePrepaintTimeout);
        Phase = EPhase::Detached;
    }
    else if (Phase == EPhase::Prepainting)
        KeepPainting();
    // A title painted for another look (Options in the world's pause menu, a resized window): paint it again now,
    // still hidden, so Return to Main Menu shows the new one at once.
    else if (Phase == EPhase::Detached && bTitlePainted && !(GetLook() == PaintedLook))
        StartPrepaint();
    return true;
}
