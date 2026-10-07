#include "UnnamedProjectSettings.h"

#include "InputAction.h"
#include "InputMappingContext.h"
#include "UObject/UnrealType.h"

namespace
{
    FUnnamedWorldEntry MakeWorld(
        const TCHAR* Id, const TCHAR* Map, const TCHAR* Name, const TCHAR* Summary, const TCHAR* Status)
    {
        FUnnamedWorldEntry Entry;
        Entry.Id = Id;
        Entry.Map = FSoftObjectPath(Map);
        Entry.Name = Name;
        Entry.Summary = Summary;
        Entry.Status = Status;
        return Entry;
    }

    template <typename T> TSoftObjectPtr<T> Asset(const TCHAR* Path)
    {
        return TSoftObjectPtr<T>(FSoftObjectPath(Path));
    }
}

UUnnamedProjectSettings::UUnnamedProjectSettings()
{
    CategoryName = TEXT("Game");
    MenuMap = FSoftObjectPath(TEXT("/Game/Menu/L_Menu.L_Menu"));
    Worlds = {
        MakeWorld(TEXT("foundation"), TEXT("/Game/Maps/L_Foundation.L_Foundation"), TEXT("Player Foundation"),
            TEXT("First-person movement, controls and diagnostics"), TEXT("Playable - Java 26.3 foundation")),
        MakeWorld(TEXT("super-flat"), TEXT("/Game/Maps/L_SuperFlat.L_SuperFlat"), TEXT("Super Flat"),
            TEXT("2 km x 2 km - flat test ground"), TEXT("Playable - temporary fly camera")),
    };
    OnFootContext = Asset<UInputMappingContext>(TEXT("/Game/Input/MappingContexts/IMC_OnFoot.IMC_OnFoot"));
    FoundationActionsContext =
        Asset<UInputMappingContext>(TEXT("/Game/Input/MappingContexts/IMC_FoundationActions.IMC_FoundationActions"));
    MoveAction = Asset<UInputAction>(TEXT("/Game/Input/Actions/IA_Move.IA_Move"));
    LookAction = Asset<UInputAction>(TEXT("/Game/Input/Actions/IA_Look.IA_Look"));
    SprintAction = Asset<UInputAction>(TEXT("/Game/Input/Actions/IA_Sprint.IA_Sprint"));
    CrouchAction = Asset<UInputAction>(TEXT("/Game/Input/Actions/IA_Crouch.IA_Crouch"));
    JumpAction = Asset<UInputAction>(TEXT("/Game/Input/Actions/IA_Jump.IA_Jump"));
}

const FUnnamedWorldEntry* UUnnamedProjectSettings::FindWorld(const FString& Id) const
{
    // Case-sensitive: the browser must send the exact catalog ID.
    return Worlds.FindByPredicate(
        [&Id](const FUnnamedWorldEntry& Entry) { return Entry.Id.ToString().Equals(Id, ESearchCase::CaseSensitive); });
}

FString UUnnamedProjectSettings::GetMenuMapPackage()
{
    return Get().MenuMap.GetLongPackageName();
}

FString UUnnamedProjectSettings::GetWorldMapPackage(const FString& WorldId)
{
    const FUnnamedWorldEntry* World = Get().FindWorld(WorldId);
    return World ? World->Map.GetLongPackageName() : FString();
}

FString UUnnamedProjectSettings::GetInputAssetPath(FName AssetName)
{
    const FSoftObjectProperty* Property = FindFProperty<FSoftObjectProperty>(StaticClass(), AssetName);
    return Property ? Property->GetPropertyValue_InContainer(&Get()).ToSoftObjectPath().ToString() : FString();
}
