#include "UnnamedProjectSettings.h"

#include "InputAction.h"
#include "InputMappingContext.h"
#include "Materials/MaterialInterface.h"
#include "Misc/PackageName.h"
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
    // One world for every feature. Retired IDs, never to be reused: foundation, super-flat, interaction-lab.
    Worlds = {
        MakeWorld(TEXT("playground"), TEXT("/Game/Maps/L_Playground.L_Playground"), TEXT("Playground"),
            TEXT("2 km dev grid: movement course, building, mining and combat"),
            TEXT("Playable - Java 26.3 foundation, session only")),
    };
    DevGridMaterial = Asset<UMaterialInterface>(TEXT("/Game/DevGrid/M_DevGrid.M_DevGrid"));
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

FString UUnnamedProjectSettings::GetHudRegistryPath()
{
    return Get().HudRoot / TEXT("DA_HudAssets.DA_HudAssets");
}

FString UUnnamedProjectSettings::GetHudTexturePackage(const FString& RelativeTexture)
{
    return Get().HudRoot / TEXT("Textures") / RelativeTexture;
}

FString UUnnamedProjectSettings::GetBlockAssetPackage(const FString& AssetName)
{
    return Get().BlockRoot / AssetName;
}

FSoftObjectPath UUnnamedProjectSettings::GetBlockAssetPath(const FString& AssetName)
{
    // The object is named after the package's last segment ("Gui/gui/.../tab_items" -> "tab_items").
    const FString Package = GetBlockAssetPackage(AssetName);
    return FSoftObjectPath(Package + TEXT(".") + FPackageName::GetShortName(Package));
}
