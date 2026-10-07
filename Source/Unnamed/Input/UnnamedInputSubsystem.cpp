#include "UnnamedInputSubsystem.h"
#include "UnnamedInputSettings.h"
#include "EnhancedInputComponent.h"
#include "EnhancedInputSubsystems.h"
#include "EnhancedPlayerInput.h"
#include "Engine/LocalPlayer.h"
#include "Framework/Application/SlateApplication.h"
#include "GameFramework/PlayerController.h"
#include "InputAction.h"
#include "InputMappingContext.h"
#if PLATFORM_WINDOWS
#include "Windows/WindowsHWrapper.h"
#endif

bool UUnnamedInputSubsystem::EnsureReady(FString& OutError)
{
    OutError.Empty();
    auto* Enhanced = GetLocalPlayer()->GetSubsystem<UEnhancedInputLocalPlayerSubsystem>();
    if (!Enhanced || !Settings()) { OutError = TEXT("Enhanced Input settings are unavailable. Restart after building the project."); return false; }
    if (!OnFootContext)
    {
        OnFootContext = LoadObject<UInputMappingContext>(nullptr, TEXT("/Game/Input/MappingContexts/IMC_OnFoot.IMC_OnFoot"));
        if (!OnFootContext) { OutError = TEXT("The on-foot input assets are missing. Run CreateInputAssets.py."); return false; }
        if (!Settings()->RegisterInputMappingContext(OnFootContext)) { OnFootContext = nullptr; OutError = TEXT("The on-foot bindings could not be registered."); return false; }
        FoundationContext=LoadObject<UInputMappingContext>(nullptr,TEXT("/Game/Input/MappingContexts/IMC_FoundationActions.IMC_FoundationActions"));
        if(FoundationContext)
        {
            Settings()->RegisterInputMappingContext(FoundationContext);
            // Debug mappings remain registered for settings, but only the chord
            // router can activate them. Bare D/C/B must still reach gameplay.
            RuntimeActionContext=NewObject<UInputMappingContext>(this,TEXT("FoundationActions"));
            for(const auto& M:FoundationContext->GetMappings())if(!M.GetMappingName().ToString().StartsWith(TEXT("key.debug.")))RuntimeActionContext->MapKey(M.Action,M.Key)=M;
        }
        RefreshPolicies();
        // Enhanced Input migrates registered default rows and restores customized
        // keys by stable mapping name. Never rewrite existing custom bindings.
        Settings()->FoundationSchemaVersion=1;
        if (FSlateApplication::IsInitialized())
            ActivationHandle = FSlateApplication::Get().OnApplicationActivationStateChanged().AddUObject(this, &ThisClass::OnApplicationActivationChanged);
    }
    return true;
}

UUnnamedInputSettings* UUnnamedInputSubsystem::Settings() const
{
    auto* Enhanced = GetLocalPlayer()->GetSubsystem<UEnhancedInputLocalPlayerSubsystem>();
    return Enhanced ? Enhanced->GetUserSettings<UUnnamedInputSettings>() : nullptr;
}

void UUnnamedInputSubsystem::RefreshPolicies()
{
    ClearIntent();
    if (auto* UserSettings = Settings()) Model.SetPolicies(UserSettings->bToggleSprint, UserSettings->bToggleCrouch);
}

bool UUnnamedInputSubsystem::BindGameplayInput(UEnhancedInputComponent* Component)
{
    FString Error;
    if (!Component || !EnsureReady(Error)) return false;
    if (BoundComponent == Component) return true;
    if (BoundComponent.IsValid()) for (uint32 Handle : BindingHandles) BoundComponent->RemoveBindingByHandle(Handle);
    BindingHandles.Empty();
    BoundComponent = Component;
    auto* Move = LoadObject<UInputAction>(nullptr, TEXT("/Game/Input/Actions/IA_Move.IA_Move"));
    auto* Look = LoadObject<UInputAction>(nullptr, TEXT("/Game/Input/Actions/IA_Look.IA_Look"));
    auto* Sprint = LoadObject<UInputAction>(nullptr, TEXT("/Game/Input/Actions/IA_Sprint.IA_Sprint"));
    auto* Crouch = LoadObject<UInputAction>(nullptr, TEXT("/Game/Input/Actions/IA_Crouch.IA_Crouch"));
    auto* Jump = LoadObject<UInputAction>(nullptr, TEXT("/Game/Input/Actions/IA_Jump.IA_Jump"));
    if (!Move || !Look || !Sprint || !Crouch || !Jump) return false;
    for (ETriggerEvent Event : { ETriggerEvent::Triggered, ETriggerEvent::Completed, ETriggerEvent::Canceled })
    {
        BindingHandles.Add(Component->BindAction(Move, Event, this, &ThisClass::OnMove).GetHandle());
        BindingHandles.Add(Component->BindAction(Look, Event, this, &ThisClass::OnLook).GetHandle());
    }
    BindingHandles.Add(Component->BindAction(Sprint, ETriggerEvent::Started, this, &ThisClass::SprintStart).GetHandle());
    BindingHandles.Add(Component->BindAction(Crouch, ETriggerEvent::Started, this, &ThisClass::CrouchStart).GetHandle());
    BindingHandles.Add(Component->BindAction(Jump, ETriggerEvent::Started, this, &ThisClass::JumpStart).GetHandle());
    if(RuntimeActionContext) for(const auto& Mapping:RuntimeActionContext->GetMappings())
    {
        BindingHandles.Add(Component->BindAction(Mapping.Action,ETriggerEvent::Started,this,&ThisClass::ActionStart).GetHandle());
        for(auto Event:{ETriggerEvent::Completed,ETriggerEvent::Canceled})
            BindingHandles.Add(Component->BindAction(Mapping.Action,Event,this,&ThisClass::ActionEnd).GetHandle());
    }
    for (ETriggerEvent Event : { ETriggerEvent::Completed, ETriggerEvent::Canceled })
    {
        BindingHandles.Add(Component->BindAction(Sprint, Event, this, &ThisClass::SprintEnd).GetHandle());
        BindingHandles.Add(Component->BindAction(Crouch, Event, this, &ThisClass::CrouchEnd).GetHandle());
        BindingHandles.Add(Component->BindAction(Jump, Event, this, &ThisClass::JumpEnd).GetHandle());
    }
    return true;
}

void UUnnamedInputSubsystem::SetGameplayInputEnabled(bool bEnabled)
{
    FString Error;
    if (!EnsureReady(Error)) return;
    ClearIntent();
    auto* Enhanced = GetLocalPlayer()->GetSubsystem<UEnhancedInputLocalPlayerSubsystem>();
    FModifyContextOptions Options;
    Options.bIgnoreAllPressedKeysUntilRelease = true;
    Options.bForceImmediately = true;
    bGameplayEnabled = bEnabled && BoundComponent.IsValid();
    if (bGameplayEnabled) Enhanced->AddMappingContext(OnFootContext, 0, Options);
    else Enhanced->RemoveMappingContext(OnFootContext, Options);
    if(RuntimeActionContext) {if(bGameplayEnabled)Enhanced->AddMappingContext(RuntimeActionContext,10,Options);else Enhanced->RemoveMappingContext(RuntimeActionContext,Options);}
}

void UUnnamedInputSubsystem::ClearIntent()
{
    Model.Reset();
    ResetSignal.Broadcast();
    auto* Enhanced = GetLocalPlayer()->GetSubsystem<UEnhancedInputLocalPlayerSubsystem>();
    if (Enhanced && Enhanced->GetPlayerInput()) Enhanced->GetPlayerInput()->FlushPressedKeys();
}

FUnnamedInputIntent UUnnamedInputSubsystem::ConsumeIntent()
{
    FUnnamedInputIntent Result = Model.Intent;
    Model.Intent.bJumpPressed = false;
    Model.Intent.Look = FVector2D::ZeroVector;
    return Result;
}
void UUnnamedInputSubsystem::OnMove(const FInputActionValue& Value)
{
    if(!bGameplayEnabled)return;const auto Old=Model.Intent.Move;Model.Move(Value.Get<FVector2D>());const auto Next=Model.Intent.Move;
    const TArray<FName> Names={TEXT("key.forward"),TEXT("key.back"),TEXT("key.left"),TEXT("key.right")};
    const bool Before[]={Old.Y>0,Old.Y<0,Old.X<0,Old.X>0},After[]={Next.Y>0,Next.Y<0,Next.X<0,Next.X>0};
    for(int32 I=0;I<4;++I)if(Before[I]!=After[I])ActionSignal.Broadcast(Names[I],After[I]);
}
void UUnnamedInputSubsystem::OnLook(const FInputActionValue& Value) { if (bGameplayEnabled) Model.Intent.Look = Value.Get<FVector2D>(); }
void UUnnamedInputSubsystem::SprintStart() { if (bGameplayEnabled){Model.Sprint(true);ActionSignal.Broadcast(TEXT("key.sprint"),true);} }
void UUnnamedInputSubsystem::SprintEnd() { Model.Sprint(false);ActionSignal.Broadcast(TEXT("key.sprint"),false); }
void UUnnamedInputSubsystem::CrouchStart() { if (bGameplayEnabled){Model.Crouch(true);ActionSignal.Broadcast(TEXT("key.sneak"),true);} }
void UUnnamedInputSubsystem::CrouchEnd() { Model.Crouch(false);ActionSignal.Broadcast(TEXT("key.sneak"),false); }
void UUnnamedInputSubsystem::JumpStart() { if (bGameplayEnabled){Model.Jump(true);ActionSignal.Broadcast(TEXT("key.jump"),true);} }
void UUnnamedInputSubsystem::JumpEnd() { Model.Jump(false);ActionSignal.Broadcast(TEXT("key.jump"),false); }
void UUnnamedInputSubsystem::OnApplicationActivationChanged(bool bActive) { if (!bActive) ClearIntent(); }
void UUnnamedInputSubsystem::Deinitialize()
{
    if (OnFootContext) SetGameplayInputEnabled(false);
    if (BoundComponent.IsValid()) for (uint32 Handle : BindingHandles) BoundComponent->RemoveBindingByHandle(Handle);
    if (FSlateApplication::IsInitialized()) FSlateApplication::Get().OnApplicationActivationStateChanged().Remove(ActivationHandle);
    Super::Deinitialize();
}

TArray<FName> UUnnamedInputSubsystem::SupportedIds() const
{
    TArray<FName> Result;
    for(const auto* C:{OnFootContext.Get(),FoundationContext.Get()})if(C)for(const auto& M:C->GetMappings())if(!M.GetMappingName().IsNone())Result.AddUnique(M.GetMappingName());
    return Result;
}
FString UUnnamedInputSubsystem::AppliedContexts() const
{
    auto* Enhanced=GetLocalPlayer()->GetSubsystem<UEnhancedInputLocalPlayerSubsystem>();FString Result;
    for(const auto* Context:{OnFootContext.Get(),RuntimeActionContext.Get()})
    {int32 Priority=0;if(Context&&Enhanced&&Enhanced->HasMappingContext(Context,Priority))Result+=Context->GetName()+FString::Printf(TEXT(":%d "),Priority);}
    return Result.IsEmpty()?TEXT("none"):Result;
}
FKey UUnnamedInputSubsystem::CurrentKey(FName Id) const
{
    auto* S=Settings();auto* Profile=S?S->GetActiveKeyProfile():nullptr;FMapPlayerKeyArgs Args;Args.MappingName=Id;Args.Slot=EPlayerMappableKeySlot::First;
    const auto* Mapping=Profile?Profile->FindKeyMapping(Args):nullptr;return Mapping?Mapping->GetCurrentKey():EKeys::Invalid;
}
FKey UUnnamedInputSubsystem::DefaultKey(FName Id) const
{
    for(const auto* C:{OnFootContext.Get(),FoundationContext.Get()})if(C)for(const auto& M:C->GetMappings())if(M.GetMappingName()==Id)return M.Key;
    return EKeys::Invalid;
}
FKey UUnnamedInputSubsystem::PhysicalKey(FKey Key)
{
#if PLATFORM_WINDOWS
    if(Key.IsMouseButton()||Key.IsAnalog())return Key;
    const uint32 *VK=nullptr,*Character=nullptr;FInputKeyManager::Get().GetCodesFromKey(Key,VK,Character);
    if(VK && *VK>=0x30 && *VK<=0xDE)
    {
        const auto Layout=GetKeyboardLayout(0);
        // Layout mapping only; never activate/change the user's keyboard layout.
        const UINT Scan=MapVirtualKeyExW(*VK,MAPVK_VK_TO_VSC_EX,Layout);
        const UINT US=MapVirtualKeyExW(Scan,MAPVK_VSC_TO_VK_EX,reinterpret_cast<HKL>(static_cast<UPTRINT>(0x04090409)));
        const FKey Physical=FInputKeyManager::Get().GetKeyFromCodes(US,0);if(Physical.IsValid())return Physical;
    }
#endif
    return Key;
}
void UUnnamedInputSubsystem::ActionStart(const FInputActionInstance& Instance)
{
    if(bGameplayEnabled&&FoundationContext)for(const auto& M:FoundationContext->GetMappings())if(M.Action==Instance.GetSourceAction()){ActionSignal.Broadcast(M.GetMappingName(),true);break;}
}
void UUnnamedInputSubsystem::ActionEnd(const FInputActionInstance& Instance)
{
    if(FoundationContext)for(const auto& M:FoundationContext->GetMappings())if(M.Action==Instance.GetSourceAction()){ActionSignal.Broadcast(M.GetMappingName(),false);break;}
}
