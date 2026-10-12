#include "Menu/MenuBrowserHost.h"
#include "UnnamedLog.h"
#include "Menu/MenuApplication.h"
#include "Menu/MenuInputBridge.h"
#include "Menu/MenuPreferences.h"
#include "Menu/MenuVideoSettings.h"
#include "Input/UnnamedInputSubsystem.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/GameViewportClient.h"
#include "Engine/LocalPlayer.h"
#include "Engine/World.h"
#include "Framework/Application/SlateApplication.h"
#include "GameFramework/PlayerController.h"
#include "GenericPlatform/GenericPlatformHttp.h"
#include "IWebBrowserWindow.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "Misc/Paths.h"
#include "SWebBrowser.h"

UMenuBrowserHost::UMenuBrowserHost()
{
    PrimaryComponentTick.bCanEverTick = true;
    PrimaryComponentTick.bTickEvenWhenPaused = true;
}

APlayerController* UMenuBrowserHost::GetPlayerController() const
{
    return Cast<APlayerController>(GetOwner());
}

void UMenuBrowserHost::AddBridge(const FString& Name, UObject* Bridge)
{
    ExtraBridges.Add(Name, Bridge);
    if (Browser.IsValid()) Browser->BindUObject(Name, Bridge, true);
}

UMenuVideoSettings* UMenuBrowserHost::GetVideoSettings()
{
    if (!VideoSettings)
    {
        VideoSettings = NewObject<UMenuVideoSettings>(this);
        // Window, frame-rate and VSync changes need a visible standalone game window.
        VideoSettings->Initialize(!GIsEditor && GetWorld()->WorldType == EWorldType::Game &&
            !FParse::Param(FCommandLine::Get(), TEXT("RenderOffscreen")));
    }
    return VideoSettings;
}

void UMenuBrowserHost::SetPauseMode(int32 Mode)
{
    PauseMode = FMath::Clamp(Mode, 0, 2);
}

FString UMenuBrowserHost::PauseMenuUrl(const FString& Base, int32 Mode)
{
    return Mode == 1 || Mode == 2 ? Base + TEXT("#pause") : Base;
}

FString UMenuBrowserHost::GetMenuUrl()
{
    FString Path = FPaths::ConvertRelativePathToFull(FPaths::ProjectContentDir() / TEXT("Menu/Web/index.html"));
    FPaths::NormalizeFilename(Path);
    return TEXT("file:///") +
        FGenericPlatformHttp::UrlEncode(Path)
            .Replace(TEXT("%2F"), TEXT("/"), ESearchCase::IgnoreCase)
            .Replace(TEXT("%3A"), TEXT(":"), ESearchCase::IgnoreCase);
}

void UMenuBrowserHost::CreateBrowser(const FString& MenuURL, bool bTransparent)
{
    Browser =
        SNew(SWebBrowser)
            .InitialURL(MenuURL)
            .ShowControls(false)
            .ShowAddressBar(false)
            .ShowInitialThrobber(false)
            .SupportsThumbMouseButtonNavigation(false)
            .BrowserFrameRate(60)
            .BackgroundColor(FColor::Black)
            // Modes 1 and 2: CEF paints nothing (alpha 0) before the document, and the page stays transparent until
            // its pause screen has painted, so the game shows instead of black.
            .SupportsTransparency(bTransparent)
            // Only the menu document itself may load; no navigation or popups.
            .OnBeforeNavigation_Lambda([MenuURL](const FString& URL, const FWebNavigationRequest&)
                { return URL != MenuURL && URL != TEXT("about:blank"); })
            .OnBeforePopup_Lambda([](const FString&, const FString&) { return true; })
            .OnConsoleMessage_Lambda(
                [](const FString& Message, const FString& Source, int32 Line, EWebBrowserConsoleLogSeverity Severity)
                {
                    if (Severity == EWebBrowserConsoleLogSeverity::Error ||
                        Severity == EWebBrowserConsoleLogSeverity::Fatal)
                    {
                        UE_LOG(LogUnnamedMenu, Error, TEXT("%s:%d %s"), *Source, Line, *Message);
                    }
                    else if (Severity == EWebBrowserConsoleLogSeverity::Warning)
                    {
                        UE_LOG(LogUnnamedMenu, Warning, TEXT("%s:%d %s"), *Source, Line, *Message);
                    }
                    else
                    {
                        UE_LOG(LogUnnamedMenu, Log, TEXT("%s:%d %s"), *Source, Line, *Message);
                    }
                })
            .OnLoadError_Lambda([] { UE_LOG(LogUnnamedMenu, Error, TEXT("Offline menu document failed to load")); });
    GEngine->GameViewport->AddViewportWidgetContent(Browser.ToSharedRef(), 100);
    Browser->SetParentWindow(
        FSlateApplication::Get().FindWidgetWindow(GEngine->GameViewport->GetGameViewportWidget().ToSharedRef()));
}

void UMenuBrowserHost::BindServices(APlayerController& Player, bool bDisableGameplay)
{
    Browser->BindUObject(TEXT("menuvideo"), GetVideoSettings(), true);
    auto* PlayerInput = Player.GetLocalPlayer()->GetSubsystem<UUnnamedInputSubsystem>();
    InputBridge = NewObject<UMenuInputBridge>(this);
    InputBridge->Initialize(PlayerInput);
    if (bDisableGameplay) PlayerInput->SetGameplayInputEnabled(false);
    Browser->BindUObject(TEXT("menuinput"), InputBridge, true);
    Preferences = Player.GetGameInstance()->GetSubsystem<UMenuPreferences>();
    Browser->BindUObject(TEXT("menupreferences"), Preferences, true);
    Application = NewObject<UMenuApplication>(this);
    Application->Initialize(&Player, VideoSettings, InputBridge, Preferences);
    Browser->BindUObject(TEXT("menuapplication"), Application, true);
    for (const auto& [Name, Bridge] : ExtraBridges)
        Browser->BindUObject(Name, Bridge, true);
}

void UMenuBrowserHost::FocusBrowser(APlayerController& Player)
{
    Player.bShowMouseCursor = true;
    FInputModeUIOnly InputMode;
    InputMode.SetWidgetToFocus(Browser);
    InputMode.SetLockMouseToViewportBehavior(EMouseLockMode::DoNotLock);
    Player.SetInputMode(InputMode);
    Player.SetIgnoreMoveInput(true);
    Player.SetIgnoreLookInput(true);
}

void UMenuBrowserHost::Prewarm()
{
    APlayerController* Player = GetPlayerController();
    if (PauseMode != 1 || Browser.IsValid() || !Player || !Player->IsLocalController() || !GEngine ||
        !GEngine->GameViewport || !GEngine->GameViewport->GetGameViewportWidget().IsValid())
        return;
    const FString MenuURL = PauseMenuUrl(GetMenuUrl(), PauseMode);
    StartSeconds = FPlatformTime::Seconds();
    bPageReady = false;
    PageReadySeconds = PageShownSeconds = 0;
    CreateBrowser(MenuURL, true);
    // In the layout, so it sizes, loads and paints; the page itself stays invisible until it is shown.
    Browser->SetVisibility(EVisibility::HitTestInvisible);
    BindServices(*Player, false);
    UE_LOG(LogUnnamedMenu, Log, TEXT("Offline menu pre-warmed: %s"), *MenuURL);
}

void UMenuBrowserHost::Show()
{
    APlayerController* Player = GetPlayerController();
    if (PauseMode == 1)
    {
        if (bShown || !Player || !Player->IsLocalController() || !GEngine || !GEngine->GameViewport) return;
        // A world whose pre-warm could not run (no viewport yet) makes its browser now.
        Prewarm();
        if (!Browser.IsValid()) return;
        bShown = true;
        StartSeconds = FPlatformTime::Seconds();
        PageShownSeconds = 0;
        Browser->SetVisibility(EVisibility::Visible);
        Player->GetLocalPlayer()->GetSubsystem<UUnnamedInputSubsystem>()->SetGameplayInputEnabled(false);
        FocusBrowser(*Player);
        // The page shows itself at once, or as soon as it has painted if it is still loading (HandlePageReady).
        Browser->ExecuteJavascript(TEXT("typeof FoundationMenu!=='undefined'&&FoundationMenu.reveal()"));
        UE_LOG(LogUnnamedMenu, Log, TEXT("Offline menu revealed"));
        return;
    }
    if (Browser.IsValid() || !Player || !Player->IsLocalController() || !GEngine || !GEngine->GameViewport) return;
    const FString MenuURL = PauseMenuUrl(GetMenuUrl(), PauseMode);
    StartSeconds = FPlatformTime::Seconds();
    bPageReady = false;
    PageReadySeconds = PageShownSeconds = 0;
    CreateBrowser(MenuURL, PauseMode == 2);
    BindServices(*Player, true);
    FocusBrowser(*Player);
    UE_LOG(LogUnnamedMenu, Log, TEXT("Offline menu: %s"), *MenuURL);
}

void UMenuBrowserHost::Hide()
{
    if (PauseMode != 1)
    {
        DestroyBrowser();
        return;
    }
    if (!Browser.IsValid()) return;
    bShown = false;
    // A page still painting stays in the layout (invisible) to finish; a painted one collapses and stops painting.
    Browser->SetVisibility(bPageReady ? EVisibility::Collapsed : EVisibility::HitTestInvisible);
}

void UMenuBrowserHost::DestroyBrowser()
{
    bShown = false;
    bPageReady = false;
    if (!Browser.IsValid()) return;
    if (GEngine && GEngine->GameViewport) GEngine->GameViewport->RemoveViewportWidgetContent(Browser.ToSharedRef());
    if (VideoSettings) Browser->UnbindUObject(TEXT("menuvideo"), VideoSettings, true);
    if (Application) Browser->UnbindUObject(TEXT("menuapplication"), Application, true);
    if (InputBridge) Browser->UnbindUObject(TEXT("menuinput"), InputBridge, true);
    if (Preferences) Browser->UnbindUObject(TEXT("menupreferences"), Preferences, true);
    for (const auto& [Name, Bridge] : ExtraBridges)
        Browser->UnbindUObject(Name, Bridge, true);
    Browser.Reset();
    Application = nullptr;
    InputBridge = nullptr;
    Preferences = nullptr;
}

bool UMenuBrowserHost::HandlePageReady()
{
    if (!Browser.IsValid()) return false;
    bPageReady = true;
    PageReadySeconds = FPlatformTime::Seconds();
    if (PauseMode == 1 && !bShown) Browser->SetVisibility(EVisibility::Collapsed);
    // Shown already (mode 2, or ESC while the page was loading): tell the page now as well, so a lost MenuReady reply
    // (a 5 s bridge timeout) or a reveal() sent before the script existed cannot leave it hidden under UI-only input.
    else if (IsOpen())
        Browser->ExecuteJavascript(TEXT("typeof FoundationMenu!=='undefined'&&FoundationMenu.reveal()"));
    return IsOpen();
}

void UMenuBrowserHost::HandlePageShown(const FString& State)
{
    if (PageShownSeconds > 0) return;
    PageShownSeconds = FPlatformTime::Seconds();
    PageShownState = State;
}

void UMenuBrowserHost::TickComponent(
    float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
    Super::TickComponent(DeltaTime, TickType, ThisTickFunction);
    if (VideoSettings) VideoSettings->Tick();
    if (Application) Application->Tick();
}

void UMenuBrowserHost::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
    DestroyBrowser();
    Super::EndPlay(EndPlayReason);
}
