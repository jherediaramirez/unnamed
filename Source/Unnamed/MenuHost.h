#pragma once

#include "CoreMinimal.h"
#include "GameFramework/GameModeBase.h"
#include "GameFramework/PlayerController.h"
#include "MenuHost.generated.h"

class SWebBrowser;
class UMenuVideoSettings;
class UMenuApplication;

UCLASS()
class UNNAMED_API AMenuPlayerController : public APlayerController
{
    GENERATED_BODY()
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
};

UCLASS()
class UNNAMED_API AMenuGameMode : public AGameModeBase
{
    GENERATED_BODY()
public:
    AMenuGameMode();
};
