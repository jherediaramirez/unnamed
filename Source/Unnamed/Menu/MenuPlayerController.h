#pragma once

#include "CoreMinimal.h"
#include "GameFramework/PlayerController.h"
#include "MenuPlayerController.generated.h"

class SWebBrowser;
class UMenuVideoSettings;
class UMenuApplication;
class UMenuInputBridge;
class UMenuPreferences;
class UFoundationDiagnostics;
class UFoundationMenuBridge;

UCLASS()
class UNNAMED_API AMenuPlayerController : public APlayerController
{
    GENERATED_BODY()
public:
    AMenuPlayerController();
    void ShowMenu();
    void HideMenu();
    UMenuVideoSettings* EnsureVideoSettings();

    bool IsMenuOpen() const { return Browser.IsValid(); }
    UPROPERTY(VisibleAnywhere)
    TObjectPtr<UFoundationDiagnostics> Diagnostics;

protected:
    virtual void BeginPlay() override;
    virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
    virtual void Tick(float DeltaSeconds) override;

private:
    TSharedPtr<SWebBrowser> Browser;
    UPROPERTY(Transient)
    TObjectPtr<UMenuVideoSettings> VideoSettings;
    UPROPERTY(Transient)
    TObjectPtr<UMenuApplication> Application;
    UPROPERTY(Transient)
    TObjectPtr<UMenuInputBridge> InputSettings;
    UPROPERTY(Transient)
    TObjectPtr<UMenuPreferences> Preferences;
    UPROPERTY(Transient)
    TObjectPtr<UFoundationMenuBridge> FoundationBridge;
};
