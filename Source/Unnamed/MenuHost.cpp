#include "MenuHost.h"
#include "Engine/Engine.h"
#include "Engine/GameViewportClient.h"
#include "Framework/Application/SlateApplication.h"
#include "GenericPlatform/GenericPlatformHttp.h"
#include "IWebBrowserWindow.h"
#include "Misc/Paths.h"
#include "SWebBrowser.h"

DEFINE_LOG_CATEGORY_STATIC(LogUnnamedMenu, Log, All);

AMenuGameMode::AMenuGameMode()
{
    PlayerControllerClass = AMenuPlayerController::StaticClass();
    DefaultPawnClass = nullptr;
    HUDClass = nullptr;
}

void AMenuPlayerController::BeginPlay()
{
    Super::BeginPlay();
    if (!IsLocalController() || !GEngine || !GEngine->GameViewport) return;
    FString Path = FPaths::ConvertRelativePathToFull(FPaths::ProjectContentDir() / TEXT("Menu/Web/index.html"));
    FPaths::NormalizeFilename(Path);
    const FString MenuURL = TEXT("file:///") + FGenericPlatformHttp::UrlEncode(Path)
        .Replace(TEXT("%2F"), TEXT("/"), ESearchCase::IgnoreCase)
        .Replace(TEXT("%3A"), TEXT(":"), ESearchCase::IgnoreCase);
    Browser = SNew(SWebBrowser)
        .InitialURL(MenuURL)
        .ShowControls(false)
        .ShowAddressBar(false)
        .ShowInitialThrobber(false)
        .SupportsThumbMouseButtonNavigation(false)
        .BrowserFrameRate(60)
        .BackgroundColor(FColor::Black)
        .OnBeforeNavigation_Lambda([MenuURL](const FString& URL, const FWebNavigationRequest&)
        {
            return URL != MenuURL && URL != TEXT("about:blank");
        })
        .OnBeforePopup_Lambda([](const FString&, const FString&) { return true; })
        .OnConsoleMessage_Lambda([](const FString& Message, const FString& Source, int32 Line, EWebBrowserConsoleLogSeverity Severity)
        {
            if (Severity == EWebBrowserConsoleLogSeverity::Error || Severity == EWebBrowserConsoleLogSeverity::Fatal)
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
    Browser->SetParentWindow(FSlateApplication::Get().FindWidgetWindow(GEngine->GameViewport->GetGameViewportWidget().ToSharedRef()));
    bShowMouseCursor = true;
    FInputModeUIOnly Input;
    Input.SetWidgetToFocus(Browser);
    Input.SetLockMouseToViewportBehavior(EMouseLockMode::DoNotLock);
    SetInputMode(Input);
    SetIgnoreMoveInput(true);
    SetIgnoreLookInput(true);
    UE_LOG(LogUnnamedMenu, Log, TEXT("Offline UI-only menu: %s; no pawn, settings bindings or services"), *MenuURL);
}

void AMenuPlayerController::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
    if (Browser.IsValid() && GEngine && GEngine->GameViewport)
        GEngine->GameViewport->RemoveViewportWidgetContent(Browser.ToSharedRef());
    Browser.Reset();
    Super::EndPlay(EndPlayReason);
}
