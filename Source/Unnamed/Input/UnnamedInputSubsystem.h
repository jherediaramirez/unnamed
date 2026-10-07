#pragma once

#include "CoreMinimal.h"
#include "Subsystems/LocalPlayerSubsystem.h"
#include "UnnamedInputIntent.h"
#include "UnnamedInputSubsystem.generated.h"

class UInputMappingContext;
class UInputAction;
class UEnhancedInputComponent;
class UUnnamedInputSettings;
struct FInputActionValue;
struct FInputActionInstance;
DECLARE_MULTICAST_DELEGATE_TwoParams(FFoundationActionSignal, FName, bool);
DECLARE_MULTICAST_DELEGATE(FFoundationInputReset);

UCLASS()

class UNNAMED_API UUnnamedInputSubsystem : public ULocalPlayerSubsystem
{
    GENERATED_BODY()
public:
    virtual void Deinitialize() override;
    bool EnsureReady(FString& OutError);
    UUnnamedInputSettings* Settings() const;

    UInputMappingContext* Context() const { return OnFootContext; }

    UInputMappingContext* ActionContext() const { return FoundationContext; }

    TArray<FName> SupportedIds() const;
    FKey CurrentKey(FName Id) const;
    FKey DefaultKey(FName Id) const;
    FString AppliedContexts() const;
    static FKey PhysicalKey(FKey LayoutKey);

    bool IsGameplayEnabled() const { return bGameplayEnabled; }

    FFoundationActionSignal ActionSignal;
    FFoundationInputReset ResetSignal;
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
