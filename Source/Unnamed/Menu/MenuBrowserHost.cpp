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

void UMenuBrowserHost::Show()
{
    APlayerController* Player = GetPlayerController();
    if (Browser.IsValid() || !Player || !Player->IsLocalController() || !GEngine || !GEngine->GameViewport) return;
    FString Path = FPaths::ConvertRelativePathToFull(FPaths::ProjectContentDir() / TEXT("Menu/Web/index.html"));
    FPaths::NormalizeFilename(Path);
    const FString MenuURL = TEXT("file:///") +
        FGenericPlatformHttp::UrlEncode(Path)
            .Replace(TEXT("%2F"), TEXT("/"), ESearchCase::IgnoreCase)
            .Replace(TEXT("%3A"), TEXT(":"), ESearchCase::IgnoreCase);
    Browser =
        SNew(SWebBrowser)
            .InitialURL(MenuURL)
            .ShowControls(false)
            .ShowAddressBar(false)
            .ShowInitialThrobber(false)
            .SupportsThumbMouseButtonNavigation(false)
            .BrowserFrameRate(60)
            .BackgroundColor(FColor::Black)
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

    Browser->BindUObject(TEXT("menuvideo"), GetVideoSettings(), true);
    auto* PlayerInput = Player->GetLocalPlayer()->GetSubsystem<UUnnamedInputSubsystem>();
    InputBridge = NewObject<UMenuInputBridge>(this);
    InputBridge->Initialize(PlayerInput);
    PlayerInput->SetGameplayInputEnabled(false);
    Browser->BindUObject(TEXT("menuinput"), InputBridge, true);
    Preferences = Player->GetGameInstance()->GetSubsystem<UMenuPreferences>();
    Browser->BindUObject(TEXT("menupreferences"), Preferences, true);
    Application = NewObject<UMenuApplication>(this);
    Application->Initialize(Player, VideoSettings, InputBridge, Preferences);
    Browser->BindUObject(TEXT("menuapp"), Application, true);
    for (const auto& [Name, Bridge] : ExtraBridges)
        Browser->BindUObject(Name, Bridge, true);

    Player->bShowMouseCursor = true;
    FInputModeUIOnly InputMode;
    InputMode.SetWidgetToFocus(Browser);
    InputMode.SetLockMouseToViewportBehavior(EMouseLockMode::DoNotLock);
    Player->SetInputMode(InputMode);
    Player->SetIgnoreMoveInput(true);
    Player->SetIgnoreLookInput(true);
    UE_LOG(LogUnnamedMenu, Log, TEXT("Offline menu: %s"), *MenuURL);
}

void UMenuBrowserHost::Hide()
{
    if (!Browser.IsValid()) return;
    if (GEngine && GEngine->GameViewport) GEngine->GameViewport->RemoveViewportWidgetContent(Browser.ToSharedRef());
    if (VideoSettings) Browser->UnbindUObject(TEXT("menuvideo"), VideoSettings, true);
    if (Application) Browser->UnbindUObject(TEXT("menuapp"), Application, true);
    if (InputBridge) Browser->UnbindUObject(TEXT("menuinput"), InputBridge, true);
    if (Preferences) Browser->UnbindUObject(TEXT("menupreferences"), Preferences, true);
    for (const auto& [Name, Bridge] : ExtraBridges)
        Browser->UnbindUObject(Name, Bridge, true);
    Browser.Reset();
    Application = nullptr;
    InputBridge = nullptr;
    Preferences = nullptr;
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
    Hide();
    Super::EndPlay(EndPlayReason);
}
