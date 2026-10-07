#pragma once

#include "CoreMinimal.h"
#include "UObject/Object.h"
#include "MenuInputSettings.generated.h"

class UUnnamedInputSubsystem;

// Narrow browser bridge: movement bindings and two accessibility policies only.
UCLASS()

class UNNAMED_API UMenuInputSettings : public UObject
{
    GENERATED_BODY()
public:
    void Initialize(UUnnamedInputSubsystem* InInput);
    bool FlushForExit(FString& OutError);
    UFUNCTION()
    FString GetState();
    UFUNCTION()
    FString ApplyBinding(const FString& Id, const FString& KeyName);
    UFUNCTION()
    FString ApplyCapturedKey(const FString& Id, const FString& LayoutKeyName);
    UFUNCTION()
    FString ResetBinding(const FString& Id);
    UFUNCTION()
    FString ResetBindings(bool bResetPolicies);
    UFUNCTION()
    FString ApplyPolicy(const FString& Id, bool bToggle);

private:
    FString Snapshot(bool bSuccess = true);
    TWeakObjectPtr<UUnnamedInputSubsystem> Input;
    FString LastError;
};
