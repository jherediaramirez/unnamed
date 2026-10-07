#include "Menu/MenuInputBridge.h"
#include "UnnamedSession.h"
#include "Input/UnnamedInputSettings.h"
#include "Input/UnnamedInputSubsystem.h"
#include "InputMappingContext.h"
#include "InputCoreTypes.h"
#include "Serialization/JsonSerializer.h"

namespace
{
    FMapPlayerKeyArgs Args(FName Id)
    {
        FMapPlayerKeyArgs Result;
        Result.MappingName = Id;
        Result.Slot = EPlayerMappableKeySlot::First;
        return Result;
    }

    FKey CurrentKey(UUnnamedInputSubsystem* Input, FName Id)
    {
        const auto* Profile = Input->Settings()->GetActiveKeyProfile();
        const auto* Mapping = Profile ? Profile->FindKeyMapping(Args(Id)) : nullptr;
        return Mapping ? Mapping->GetCurrentKey() : EKeys::Invalid;
    }

    FKey DefaultKey(UUnnamedInputSubsystem* Input, FName Id)
    {
        return Input->DefaultKey(Id);
    }

    FString Label(const FKey& Key)
    {
        if (!Key.IsValid()) return TEXT("Not Bound");
        if (Key == EKeys::LeftControl) return TEXT("Left Control");
        if (Key == EKeys::RightControl) return TEXT("Right Control");
        if (Key == EKeys::LeftMouseButton) return TEXT("Left Button");
        if (Key == EKeys::MiddleMouseButton) return TEXT("Middle Button");
        if (Key == EKeys::RightMouseButton) return TEXT("Right Button");
        if (Key == EKeys::SpaceBar) return TEXT("Space");
        return Key.GetDisplayName().ToString();
    }
}

void UMenuInputBridge::Initialize(UUnnamedInputSubsystem* InInput)
{
    Input = InInput;
    if (Input.IsValid()) Input->EnsureReady(LastError);
}

FString UMenuInputBridge::GetState()
{
    return Snapshot();
}

FString UMenuInputBridge::ApplyCapturedKey(const FString& Id, const FString& LayoutKeyName)
{
    return ApplyBinding(Id, UUnnamedInputSubsystem::PhysicalKey(FKey(*LayoutKeyName)).GetFName().ToString());
}

FString UMenuInputBridge::ApplyBinding(const FString& Id, const FString& KeyName)
{
    LastError.Empty();
    if (!Input.IsValid() || !Input->EnsureReady(LastError)) return Snapshot(false);
    const FName MappingId(*Id);
    const FKey Key = KeyName == TEXT("None") ? EKeys::Invalid : FKey(*KeyName);
    const auto SupportedIds = Input->SupportedIds();
    if (Id.Len() > 64 || KeyName.Len() > 64 || !SupportedIds.Contains(MappingId) ||
        (KeyName != TEXT("None") &&
            (!Key.IsValid() || Key.IsAnalog() || Key.IsGamepadKey() || Key.IsTouch() ||
                (!Key.IsMouseButton() && !Key.IsBindableInBlueprints()))))
    {
        LastError = TEXT("Choose a supported keyboard key or mouse button for this binding.");
        return Snapshot(false);
    }
    if (Key == EKeys::Escape || Key == EKeys::Tilde)
    {
        LastError = TEXT("Escape and the console key are reserved for menu and system controls.");
        return Snapshot(false);
    }
    if (Key.IsValid())
        for (FName Other : SupportedIds)
            if (Other != MappingId && CurrentKey(Input.Get(), Other) == Key)
            {
                const auto IsChord = [](FName Name)
                {
                    const FString S = Name.ToString();
                    return S.StartsWith(TEXT("key.debug.")) && S != TEXT("key.debug.overlay") &&
                        S != TEXT("key.debug.modifier");
                };
                if (IsChord(Other) != IsChord(MappingId)) continue;
                if ((Id == TEXT("key.debug.overlay") && Other == TEXT("key.debug.modifier")) ||
                    (Id == TEXT("key.debug.modifier") && Other == TEXT("key.debug.overlay")))
                    continue;
                LastError = TEXT(
                    "That key conflicts in the same input context/chord. Unbind or change the other action first.");
                return Snapshot(false);
            }
    auto Request = Args(MappingId);
    Request.NewKey = Key;
    FGameplayTagContainer Failures;
    Input->Settings()->MapPlayerKey(Request, Failures);
    if (!Failures.IsEmpty())
    {
        LastError = TEXT("Unreal could not update that binding.");
        return Snapshot(false);
    }
    Input->ClearIntent();
    Input->Settings()->ApplySettings();
    return Snapshot(Input->Settings()->SaveChecked(LastError));
}

FString UMenuInputBridge::ResetBinding(const FString& Id)
{
    LastError.Empty();
    if (!Input.IsValid() || !Input->EnsureReady(LastError)) return Snapshot(false);
    if (!Input->SupportedIds().Contains(FName(*Id)))
    {
        LastError = TEXT("That action is planned for a later gameplay stage.");
        return Snapshot(false);
    }
    return ApplyBinding(Id, DefaultKey(Input.Get(), FName(*Id)).GetFName().ToString());
}

FString UMenuInputBridge::ResetBindings(bool bResetPolicies)
{
    LastError.Empty();
    if (!Input.IsValid() || !Input->EnsureReady(LastError)) return Snapshot(false);
    for (FName Id : Input->SupportedIds())
    {
        FGameplayTagContainer Failures;
        Input->Settings()->ResetAllPlayerKeysInRow(Args(Id), Failures);
        if (!Failures.IsEmpty())
        {
            LastError = TEXT("Unreal could not reset the key bindings.");
            return Snapshot(false);
        }
    }
    if (bResetPolicies) Input->Settings()->bToggleSprint = Input->Settings()->bToggleCrouch = false;
    Input->RefreshPolicies();
    Input->Settings()->ApplySettings();
    return Snapshot(Input->Settings()->SaveChecked(LastError));
}

FString UMenuInputBridge::ApplyPolicy(const FString& Id, bool bToggle)
{
    LastError.Empty();
    if (!Input.IsValid() || !Input->EnsureReady(LastError)) return Snapshot(false);
    if (Id == TEXT("toggleSprint"))
        Input->Settings()->bToggleSprint = bToggle;
    else if (Id == TEXT("toggleCrouch"))
        Input->Settings()->bToggleCrouch = bToggle;
    else
    {
        LastError = TEXT("That input policy is not implemented yet.");
        return Snapshot(false);
    }
    Input->RefreshPolicies();
    return Snapshot(Input->Settings()->SaveChecked(LastError));
}

bool UMenuInputBridge::FlushForExit(FString& OutError)
{
    return Input.IsValid() && Input->EnsureReady(OutError) && Input->Settings()->SaveChecked(OutError);
}

FString UMenuInputBridge::Snapshot(bool bSuccess)
{
    const auto Result = MakeShared<FJsonObject>();
    const auto Bindings = MakeShared<FJsonObject>();
    const auto Keys = MakeShared<FJsonObject>();
    const auto Defaults = MakeShared<FJsonObject>();
    const auto Policies = MakeShared<FJsonObject>();
    FString ReadyError;
    const bool bReady = Input.IsValid() && Input->EnsureReady(ReadyError);
    TArray<TSharedPtr<FJsonValue>> Supported;
    if (bReady)
    {
        for (FName Id : Input->SupportedIds())
        {
            const FKey Key = CurrentKey(Input.Get(), Id);
            Bindings->SetStringField(Id.ToString(), Label(Key));
            Keys->SetStringField(Id.ToString(), Key.GetFName().ToString());
            Defaults->SetStringField(Id.ToString(), Label(DefaultKey(Input.Get(), Id)));
            Supported.Add(MakeShared<FJsonValueString>(Id.ToString()));
        }
        Policies->SetBoolField(TEXT("toggleSprint"), Input->Settings()->bToggleSprint);
        Policies->SetBoolField(TEXT("toggleCrouch"), Input->Settings()->bToggleCrouch);
    }
    Result->SetBoolField(TEXT("ok"), bSuccess && bReady);
    Result->SetBoolField(TEXT("ready"), bReady);
    Result->SetStringField(TEXT("build"), TEXT("Foundation 26.3"));
    Result->SetNumberField(TEXT("schemaVersion"), bReady ? Input->Settings()->FoundationSchemaVersion : 0);
    Result->SetBoolField(TEXT("persistent"), UnnamedSession::CanSaveSettings());
    Result->SetStringField(TEXT("error"), LastError.IsEmpty() ? ReadyError : LastError);
    Result->SetObjectField(TEXT("bindings"), Bindings);
    Result->SetObjectField(TEXT("nativeKeys"), Keys);
    Result->SetObjectField(TEXT("defaults"), Defaults);
    Result->SetObjectField(TEXT("policies"), Policies);
    Result->SetArrayField(TEXT("supported"), Supported);
    FString Json;
    FJsonSerializer::Serialize(Result, TJsonWriterFactory<>::Create(&Json));
    return Json;
}
