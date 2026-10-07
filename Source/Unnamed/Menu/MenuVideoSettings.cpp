#include "Menu/MenuVideoSettings.h"

#include "Engine/Engine.h"
#include "DynamicRHI.h"
#include "Engine/GameViewportClient.h"
#include "GameFramework/GameUserSettings.h"
#include "GenericPlatform/GenericApplication.h"
#include "HAL/FileManager.h"
#include "HAL/IConsoleManager.h"
#include "Misc/CommandLine.h"
#include "Misc/ConfigCacheIni.h"
#include "Misc/Parse.h"
#include "Misc/Paths.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Widgets/SWindow.h"

DEFINE_LOG_CATEGORY_STATIC(LogMenuVideoSettings, Log, All);

namespace
{
    constexpr const TCHAR* Section = TEXT("MenuVideoSettings");

    bool ValidFrameLimit(int32 Value)
    {
        return Value >= 10 && Value <= 260 && Value % 10 == 0;
    }

    FString ModeName(EWindowMode::Type Mode)
    {
        return Mode == EWindowMode::Windowed          ? TEXT("Windowed")
            : Mode == EWindowMode::WindowedFullscreen ? TEXT("Borderless")
                                                      : TEXT("Exclusive");
    }
}

TSharedPtr<SWindow> UMenuVideoSettings::GetWindow() const
{
    return GEngine && GEngine->GameViewport ? GEngine->GameViewport->GetWindow() : nullptr;
}

void UMenuVideoSettings::Initialize(bool bAllowNativeChanges)
{
    bWaitingForWindow = bAllowNativeChanges && !GetWindow().IsValid();
    bNativeAllowed = bAllowNativeChanges && GetWindow().IsValid();
    bPersistenceEnabled = bNativeAllowed && !FParse::Param(FCommandLine::Get(), TEXT("MenuSettingsSessionOnly"));
    SettingsFile = FPaths::ProjectSavedDir() / TEXT("Config/MenuVideoSettings.ini");
    FString Override;
    if (FParse::Value(FCommandLine::Get(), TEXT("MenuVideoSettingsFile="), Override))
        SettingsFile = FPaths::ConvertRelativePathToFull(Override);
    if (const TSharedPtr<SWindow> Window = GetWindow())
    {
        const FVector2D Size = Window->GetClientSizeInScreen();
        WindowedSize = FIntPoint(FMath::RoundToInt(Size.X), FMath::RoundToInt(Size.Y));
        WindowedPosition = Window->GetPositionInScreen();
        bSavedFullscreen = Window->GetWindowMode() != EWindowMode::Windowed;
    }
    Load();
    SampleStart = FPlatformTime::Seconds();
    if (bNativeAllowed)
    {
        SetFrameLimit(FrameLimit);
        SetVSync(bVSync);
        const FIntPoint RestoredWindowedSize = WindowedSize;
        SetFullscreen(bSavedFullscreen);
        if (bPersistenceEnabled && bSavedFullscreen)
            WindowedSize = RestoredWindowedSize;
        else if (bLoadedSettings && !bSavedFullscreen)
            GetWindow()->ReshapeWindow(WindowedPosition, FVector2D(RestoredWindowedSize));
    }
    UE_LOG(LogMenuVideoSettings, Display, TEXT("Five-setting adapter initialized; native=%d persistence=%d"),
        bNativeAllowed, bPersistenceEnabled);
}

bool UMenuVideoSettings::SetFrameLimit(int32 Value)
{
    const float Target = Value == 260 ? 0.0f : static_cast<float>(Value);
    GEngine->SetMaxFPS(Target);
    if (!FMath::IsNearlyEqual(GEngine->GetMaxFPS(), Target))
    {
        LastError = TEXT("The engine frame limit is overridden by another configuration.");
        return false;
    }
    FrameLimit = Value;
    if (UGameUserSettings* Settings = GEngine->GetGameUserSettings()) Settings->SetFrameRateLimit(Target);
    return true;
}

bool UMenuVideoSettings::SetVSync(bool bEnabled)
{
    IConsoleVariable* CVar = IConsoleManager::Get().FindConsoleVariable(TEXT("r.VSync"));
    if (!CVar)
    {
        LastError = TEXT("Vertical synchronization is unavailable.");
        return false;
    }
    CVar->Set(bEnabled ? 1 : 0, ECVF_SetByGameSetting);
    if ((CVar->GetInt() != 0) != bEnabled)
    {
        LastError = TEXT("Vertical synchronization is overridden by another configuration.");
        return false;
    }
    bVSync = bEnabled;
    if (UGameUserSettings* Settings = GEngine->GetGameUserSettings()) Settings->SetVSyncEnabled(bEnabled);
    return true;
}

bool UMenuVideoSettings::SetFullscreen(bool bEnabled)
{
    const TSharedPtr<SWindow> Window = GetWindow();
    UGameUserSettings* UserSettings = GEngine->GetGameUserSettings();
    if (!Window || !UserSettings || bTransitionPending)
    {
        LastError = TEXT("The game window is unavailable or is already changing mode.");
        return false;
    }
    bPreviousFullscreen = Window->GetWindowMode() != EWindowMode::Windowed;
    if (bPreviousFullscreen == bEnabled && Window->GetWindowMode() != EWindowMode::Fullscreen)
    {
        bSavedFullscreen = bEnabled;
        return true;
    }
    if (!bPreviousFullscreen)
    {
        const FVector2D Size = Window->GetClientSizeInScreen();
        WindowedSize = FIntPoint(FMath::RoundToInt(Size.X), FMath::RoundToInt(Size.Y));
        WindowedPosition = Window->GetPositionInScreen();
    }
    FDisplayMetrics Displays;
    FDisplayMetrics::RebuildDisplayMetrics(Displays);
    const FVector2D Center = Window->GetPositionInScreen() + FVector2D(Window->GetClientSizeInScreen()) * 0.5;
    FIntPoint TargetSize = WindowedSize;
    for (int32 Index = 0; Index < Displays.MonitorInfo.Num(); ++Index)
    {
        const FMonitorInfo& Monitor = Displays.MonitorInfo[Index];
        const FPlatformRect& Rect = Monitor.DisplayRect;
        if (Center.X >= Rect.Left && Center.X < Rect.Right && Center.Y >= Rect.Top && Center.Y < Rect.Bottom)
        {
            UserSettings->SetDisplayProperties(Monitor.ID, Index);
            if (bEnabled) TargetSize = FIntPoint(Rect.Right - Rect.Left, Rect.Bottom - Rect.Top);
            break;
        }
    }
    UserSettings->SetFullscreenMode(bEnabled ? EWindowMode::WindowedFullscreen : EWindowMode::Windowed);
    UserSettings->SetScreenResolution(TargetSize);
    // Apply only the window request. ApplySettings would also apply unrelated quality/audio and save them.
    UserSettings->ApplyResolutionSettings(false);
    bTargetFullscreen = bEnabled;
    bTransitionPending = true;
    TransitionDeadline = FPlatformTime::Seconds() + 5.0;
    return true;
}

void UMenuVideoSettings::Tick()
{
    // BeginPlay can precede attachment of the standalone Slate window.
    if (bWaitingForWindow && GetWindow().IsValid()) Initialize(true);
    const double Now = FPlatformTime::Seconds();
    ++SampleFrames;
    if (Now - SampleStart >= 1.0)
    {
        MeasuredFPS = SampleFrames / (Now - SampleStart);
        SampleFrames = 0;
        SampleStart = Now;
    }
    if (!bTransitionPending) return;
    const TSharedPtr<SWindow> Window = GetWindow();
    const EWindowMode::Type Target = bTargetFullscreen ? EWindowMode::WindowedFullscreen : EWindowMode::Windowed;
    const FVector2D Size = Window ? FVector2D(Window->GetClientSizeInScreen()) : FVector2D::ZeroVector;
    if (Window && Window->GetWindowMode() == Target && Size.X > 0 && Size.Y > 0)
    {
        if (!bTargetFullscreen) Window->ReshapeWindow(WindowedPosition, FVector2D(WindowedSize));
        bSavedFullscreen = bTargetFullscreen;
        bTransitionPending = false;
        GEngine->GetGameUserSettings()->ConfirmVideoMode();
        Save();
    }
    else if (Now >= TransitionDeadline)
    {
        bTransitionPending = false;
        LastError = TEXT("The window mode change failed; the previous mode was restored.");
        UGameUserSettings* UserSettings = GEngine->GetGameUserSettings();
        UserSettings->SetFullscreenMode(bPreviousFullscreen ? EWindowMode::WindowedFullscreen : EWindowMode::Windowed);
        UserSettings->SetScreenResolution(WindowedSize);
        UserSettings->ApplyResolutionSettings(false);
        UE_LOG(LogMenuVideoSettings, Error, TEXT("%s"), *LastError);
    }
}

FString UMenuVideoSettings::GetState()
{
    return Snapshot();
}

FString UMenuVideoSettings::ApplySetting(const FString& Name, const FString& ValueJson)
{
    LastError.Empty();
    const bool bBoolean = Name == TEXT("fullscreen") || Name == TEXT("enableVsync");
    const bool bNumeric =
        Name == TEXT("framerateLimit") || Name == TEXT("guiScale") || Name == TEXT("menuBackgroundBlurriness");
    TSharedPtr<FJsonObject> Input;
    if ((!bBoolean && !bNumeric) || ValueJson.Len() > 128 ||
        !FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(ValueJson), Input) || !Input ||
        Input->Values.Num() != 1 || !Input->HasField(TEXT("value")))
    {
        LastError = TEXT("Unsupported setting or malformed request.");
        return Snapshot(false);
    }
    bool Flag = false;
    double Number = 0;
    const TSharedPtr<FJsonValue> Field = Input->Values[TEXT("value")];
    if (Field->Type != (bBoolean ? EJson::Boolean : EJson::Number) ||
        (bBoolean && !Input->TryGetBoolField(TEXT("value"), Flag)) ||
        (bNumeric &&
            (!Input->TryGetNumberField(TEXT("value"), Number) || !FMath::IsFinite(Number) || Number < 0 ||
                Number > 260 || FMath::FloorToDouble(Number) != Number)))
    {
        LastError = TEXT("Invalid setting value.");
        return Snapshot(false);
    }
    const int32 Value = static_cast<int32>(Number);
    if ((Name == TEXT("framerateLimit") && !ValidFrameLimit(Value)) || (Name == TEXT("guiScale") && Value > 32) ||
        (Name == TEXT("menuBackgroundBlurriness") && Value > 10))
    {
        LastError = TEXT("Setting value is outside its supported range.");
        return Snapshot(false);
    }
    const bool bEngineSetting = bBoolean || Name == TEXT("framerateLimit");
    if (bEngineSetting && !bNativeAllowed)
    {
        LastError = TEXT("Window and engine settings require the standalone game, not an editor Play viewport.");
        return Snapshot(false);
    }
    bool bSuccess = true;
    if (Name == TEXT("fullscreen"))
        bSuccess = SetFullscreen(Flag);
    else if (Name == TEXT("enableVsync"))
        bSuccess = SetVSync(Flag);
    else if (Name == TEXT("framerateLimit"))
        bSuccess = SetFrameLimit(Value);
    else if (Name == TEXT("guiScale"))
        GuiScale = Value;
    else
        BackgroundBlur = Value;
    if (bSuccess && !bTransitionPending) Save();
    return Snapshot(bSuccess);
}

FString UMenuVideoSettings::HardReset()
{
    LastError.Empty();
    if (!bNativeAllowed || bTransitionPending)
    {
        LastError = TEXT("Reset requires the standalone game and a settled window.");
        return Snapshot(false);
    }
    const int32 PreviousLimit = FrameLimit;
    const bool bPreviousVSync = bVSync;
    if (!SetFrameLimit(120) || !SetVSync(true) || !SetFullscreen(false))
    {
        const FString Error = LastError;
        SetFrameLimit(PreviousLimit);
        SetVSync(bPreviousVSync);
        LastError = Error;
        return Snapshot(false);
    }
    GuiScale = 0;
    BackgroundBlur = 5;
    if (!bTransitionPending) Save();
    return Snapshot();
}

FString UMenuVideoSettings::Snapshot(bool bSuccess) const
{
    const TSharedPtr<SWindow> Window = GetWindow();
    const IConsoleVariable* VSync = IConsoleManager::Get().FindConsoleVariable(TEXT("r.VSync"));
    const FVector2D Size = Window ? FVector2D(Window->GetClientSizeInScreen()) : FVector2D::ZeroVector;
    const EWindowMode::Type Mode = Window ? Window->GetWindowMode() : EWindowMode::Windowed;
    const auto Settings = MakeShared<FJsonObject>();
    Settings->SetBoolField(TEXT("fullscreen"), Mode != EWindowMode::Windowed);
    Settings->SetNumberField(TEXT("framerateLimit"), FrameLimit);
    Settings->SetBoolField(TEXT("enableVsync"), VSync ? VSync->GetInt() != 0 : bVSync);
    Settings->SetNumberField(TEXT("guiScale"), GuiScale);
    Settings->SetNumberField(TEXT("menuBackgroundBlurriness"), BackgroundBlur);
    const auto Capabilities = MakeShared<FJsonObject>();
    for (const TCHAR* Key : {TEXT("fullscreen"), TEXT("framerateLimit"), TEXT("enableVsync")})
        Capabilities->SetBoolField(Key, bNativeAllowed);
    Capabilities->SetBoolField(TEXT("guiScale"), !bWaitingForWindow);
    Capabilities->SetBoolField(TEXT("menuBackgroundBlurriness"), !bWaitingForWindow);
    const auto Observed = MakeShared<FJsonObject>();
    FString DefaultRHI;
    GConfig->GetString(TEXT("/Script/WindowsTargetPlatform.WindowsTargetSettings"), TEXT("DefaultGraphicsRHI"),
        DefaultRHI, GEngineIni);
    Observed->SetStringField(TEXT("defaultGraphicsAPI"),
        DefaultRHI == TEXT("DefaultGraphicsRHI_DX12")       ? TEXT("DirectX 12")
            : DefaultRHI == TEXT("DefaultGraphicsRHI_DX11") ? TEXT("DirectX 11")
                                                            : DefaultRHI);
    const FString ActiveRHI = GDynamicRHI ? GDynamicRHI->GetName() : TEXT("Unavailable");
    Observed->SetStringField(TEXT("graphicsAPI"),
        ActiveRHI == TEXT("D3D12")       ? TEXT("DirectX 12")
            : ActiveRHI == TEXT("D3D11") ? TEXT("DirectX 11")
                                         : ActiveRHI);
    Observed->SetStringField(TEXT("windowMode"), ModeName(Mode));
    Observed->SetNumberField(TEXT("clientWidth"), Size.X);
    Observed->SetNumberField(TEXT("clientHeight"), Size.Y);
    Observed->SetNumberField(TEXT("maxFPS"), GEngine ? GEngine->GetMaxFPS() : 0);
    Observed->SetNumberField(TEXT("vsync"), VSync ? VSync->GetInt() : -1);
    Observed->SetNumberField(TEXT("measuredFPS"), MeasuredFPS);
    const auto Result = MakeShared<FJsonObject>();
    Result->SetBoolField(TEXT("ok"), bSuccess);
    Result->SetBoolField(TEXT("pendingFullscreen"), bTransitionPending);
    Result->SetBoolField(TEXT("persistent"), bPersistenceEnabled);
    Result->SetStringField(TEXT("error"), LastError);
    Result->SetObjectField(TEXT("settings"), Settings);
    Result->SetObjectField(TEXT("capabilities"), Capabilities);
    Result->SetObjectField(TEXT("observed"), Observed);
    FString Json;
    FJsonSerializer::Serialize(Result, TJsonWriterFactory<>::Create(&Json));
    return Json;
}

void UMenuVideoSettings::Load()
{
    if (!bPersistenceEnabled) return;
    FConfigFile File;
    File.Read(SettingsFile);
    int32 Version = 0;
    if (!File.GetInt(Section, TEXT("Version"), Version) || Version != 1) return;
    bLoadedSettings = true;
    int32 Number = 0;
    if (File.GetInt(Section, TEXT("FrameLimit"), Number) && ValidFrameLimit(Number)) FrameLimit = Number;
    if (File.GetInt(Section, TEXT("GuiScale"), Number) && Number >= 0 && Number <= 32) GuiScale = Number;
    if (File.GetInt(Section, TEXT("BackgroundBlur"), Number) && Number >= 0 && Number <= 10) BackgroundBlur = Number;
    File.GetBool(Section, TEXT("Fullscreen"), bSavedFullscreen);
    File.GetBool(Section, TEXT("VSync"), bVSync);
    int32 Width = 0, Height = 0;
    if (File.GetInt(Section, TEXT("WindowedWidth"), Width) && File.GetInt(Section, TEXT("WindowedHeight"), Height) &&
        Width >= 640 && Width <= 16384 && Height >= 480 && Height <= 16384)
        WindowedSize = FIntPoint(Width, Height);
}

bool UMenuVideoSettings::FlushForExit(FString& OutError)
{
    OutError.Empty();
    if (!Save())
    {
        OutError = LastError;
        return false;
    }
    return true;
}

bool UMenuVideoSettings::Save()
{
    if (!bPersistenceEnabled) return true;
    FConfigFile File;
    File.SetString(Section, TEXT("Version"), TEXT("1"));
    File.SetBool(Section, TEXT("Fullscreen"), bSavedFullscreen);
    File.SetBool(Section, TEXT("VSync"), bVSync);
    File.SetString(Section, TEXT("FrameLimit"), *LexToString(FrameLimit));
    File.SetString(Section, TEXT("GuiScale"), *LexToString(GuiScale));
    File.SetString(Section, TEXT("BackgroundBlur"), *LexToString(BackgroundBlur));
    File.SetString(Section, TEXT("WindowedWidth"), *LexToString(WindowedSize.X));
    File.SetString(Section, TEXT("WindowedHeight"), *LexToString(WindowedSize.Y));
    IFileManager::Get().MakeDirectory(*FPaths::GetPath(SettingsFile), true);
    if (!File.Write(SettingsFile))
    {
        LastError = TEXT("Settings changed for this session, but could not be saved.");
        UE_LOG(LogMenuVideoSettings, Warning, TEXT("%s"), *LastError);
        return false;
    }
    return true;
}
