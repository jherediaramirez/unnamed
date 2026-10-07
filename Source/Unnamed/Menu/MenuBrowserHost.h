#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "MenuBrowserHost.generated.h"

class APlayerController;
class SWebBrowser;
class UMenuApplication;
class UMenuInputBridge;
class UMenuPreferences;
class UMenuVideoSettings;

// Shows the offline CEF menu in its owning player controller's viewport and binds the
// native menu services to the page: menuvideo, menuinput, menupreferences and menuapp,
// plus any bridges the owner adds. Any controller that needs the menu adds this component.
UCLASS(ClassGroup = Menu)
class UNNAMED_API UMenuBrowserHost : public UActorComponent
{
    GENERATED_BODY()
public:
    UMenuBrowserHost();

    // Adds the browser, binds every service and gives it UI-only input with a cursor.
    void Show();
    // Removes the browser and its bindings. Restoring game input is the owner's job.
    void Hide();
    bool IsOpen() const { return Browser.IsValid(); }

    // Bound under Name every time the page is shown, e.g. a world's pause-menu bridge.
    void AddBridge(const FString& Name, UObject* Bridge);

    // Video settings live as long as the host so window changes work while the menu is hidden.
    UMenuVideoSettings* GetVideoSettings();

    virtual void TickComponent(
        float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;
    virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

private:
    APlayerController* GetPlayerController() const;

    TSharedPtr<SWebBrowser> Browser;
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
