#pragma once
#include "CoreMinimal.h"
#include "GameFramework/PlayerController.h"
#include "Components/ActorComponent.h"
#include "Foundation/FoundationPawn.h"
#include "FoundationController.generated.h"
class SWidget;
class STextBlock;
class IInputProcessor;
class UUnnamedInputSubsystem;
class UMenuBrowserHost;
class AFoundationController;

UCLASS()
class UNNAMED_API UFoundationDiagnostics : public UActorComponent
{
    GENERATED_BODY()
public:
    UFoundationDiagnostics();
    virtual void BeginPlay() override;
    virtual void EndPlay(const EEndPlayReason::Type Reason) override;
    virtual void TickComponent(
        float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;
    bool RouteKey(FKey Key, bool Down, bool Repeat = false);
    bool RouteWheel(float Delta);
    void Action(FName Id, bool Down);
    void CancelActions();
    void Event(const FString& Text);
    void OpenDebugOptions();
    void OpenText(bool Command);
    void OpenInventory();
    void ClosePanel();
    void Resume();
    void ReturnToMenu();
    void FocusChanged(bool Active);
    void SetCapture(bool Active);
    FString Snapshot() const;
    FString State() const;
    void CopySnapshot();
    AFoundationController* Controller() const;
    UUnnamedInputSubsystem* Input() const;
    bool IsGameplay() const;
    bool bOverlay = false, bHideHud = false, bShowCollision = false, bCapture = false, bText = false, bPanel = false,
         bInventory = false;
    FFoundationActions Actions;
    TArray<FString> Events;
    FString LastPhysical = TEXT("none"), LastLogical = TEXT("none");

private:
    TSharedPtr<IInputProcessor> Processor;
    TSharedPtr<SWidget> OverlayRoot, PanelRoot;
    TSharedPtr<STextBlock> DebugText, HudText, Crosshair;
    FDelegateHandle FocusHandle;
    TSet<FKey> Pressed, Blocked, ChordKeys;
    TSet<FName> Held;
    TArray<FKey> WheelRelease;
    int64 WheelStep = 0, NextUseTick = 0;
    bool bDebugUsed = false, bDebugDown = false;
    double SampleAt = 0, LastTime = 0, FrameMs = 0, WheelRemainder = 0;
    int64 LastActionTick = 0;
    FString LastMotionState, LastActionResult;
    void ShowPanel(TSharedRef<SWidget> Content);
};

UCLASS()
class UNNAMED_API UFoundationMenuBridge : public UObject
{
    GENERATED_BODY()
public:
    void Initialize(UFoundationDiagnostics* Value) { Diagnostics = Value; }
    UFUNCTION()
    FString GetState();
    UFUNCTION()
    void SetCapture(bool Active);
    UFUNCTION()
    void Resume();
    UFUNCTION()
    void ReturnToMenu();

private:
    TWeakObjectPtr<UFoundationDiagnostics> Diagnostics;
};

UCLASS()
class UNNAMED_API AFoundationController : public APlayerController
{
    GENERATED_BODY()
public:
    AFoundationController();
    UMenuBrowserHost* GetMenuHost() const { return MenuHost; }
    UPROPERTY(VisibleAnywhere)
    TObjectPtr<UFoundationDiagnostics> Diagnostics;
    virtual void BeginPlay() override;
    virtual void SetupInputComponent() override;
    virtual void OnPossess(APawn* NewPawn) override;
    virtual void OnUnPossess() override;
    void PauseFoundation();
    void ResumeFoundation();

private:
    UPROPERTY(VisibleAnywhere)
    TObjectPtr<UMenuBrowserHost> MenuHost;
    UPROPERTY(Transient)
    TObjectPtr<UFoundationMenuBridge> MenuBridge;
};
