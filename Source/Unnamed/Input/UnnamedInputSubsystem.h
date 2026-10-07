#pragma once

#include "CoreMinimal.h"
#include "Subsystems/LocalPlayerSubsystem.h"
#include "Input/UnnamedInputIntent.h"
#include "UnnamedInputSubsystem.generated.h"

class UInputMappingContext;
class UInputAction;
class UEnhancedInputComponent;
class UUnnamedInputSettings;
struct FInputActionValue;
struct FInputActionInstance;
DECLARE_MULTICAST_DELEGATE_TwoParams(FOnUnnamedInputAction, FName, bool);
DECLARE_MULTICAST_DELEGATE(FOnUnnamedInputReset);

UCLASS()
class UNNAMED_API UUnnamedInputSubsystem : public ULocalPlayerSubsystem
{
    GENERATED_BODY()
public:
    virtual void Deinitialize() override;
    bool EnsureReady(FString& OutError);
    UUnnamedInputSettings* GetSettings() const;

    UInputMappingContext* GetOnFootContext() const { return OnFootContext; }

    UInputMappingContext* GetActionContext() const { return FoundationContext; }

    TArray<FName> GetSupportedIds() const;
    FKey GetCurrentKey(FName Id) const;
    FKey GetDefaultKey(FName Id) const;
    FString DescribeAppliedContexts() const;
    // US-layout physical key for a layout key, so bindings follow key position.
    static FKey ToPhysicalKey(FKey LayoutKey);

    bool IsGameplayEnabled() const { return bGameplayEnabled; }

    FOnUnnamedInputAction OnAction;
    FOnUnnamedInputReset OnIntentReset;
    // Called by the future local gameplay controller after its input component exists.
    bool BindGameplayInput(UEnhancedInputComponent* Component);
    UFUNCTION(BlueprintCallable, Category = "Input")
    void SetGameplayInputEnabled(bool bEnabled);
    UFUNCTION(BlueprintCallable, Category = "Input")
    void ClearIntent();
    UFUNCTION(BlueprintPure, Category = "Input")

    FUnnamedInputIntent GetIntent() const { return Model.Intent; }
    // Consume once per character simulation update; preserves held inputs.
    UFUNCTION(BlueprintCallable, Category = "Input")
    FUnnamedInputIntent ConsumeIntent();
    void RefreshPolicies();

private:
    void OnMove(const FInputActionValue& Value);
    void OnLook(const FInputActionValue& Value);
    void SprintStart();
    void SprintEnd();
    void CrouchStart();
    void CrouchEnd();
    void JumpStart();
    void JumpEnd();
    void OnApplicationActivationChanged(bool bActive);
    void ActionStart(const FInputActionInstance& Instance);
    void ActionEnd(const FInputActionInstance& Instance);
    UPROPERTY(Transient)
    TObjectPtr<UInputMappingContext> OnFootContext;
    UPROPERTY(Transient)
    TObjectPtr<UInputMappingContext> FoundationContext;
    UPROPERTY(Transient)
    TObjectPtr<UInputMappingContext> RuntimeActionContext;
    TWeakObjectPtr<UEnhancedInputComponent> BoundComponent;
    TArray<uint32> BindingHandles;
    FDelegateHandle ActivationHandle;
    FUnnamedInputIntentModel Model;
    bool bGameplayEnabled = false;
};
