#include "Voxel/VoxelRenderDistance.h"
#include "Dom/JsonValue.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "HAL/IConsoleManager.h"
#include "Menu/MenuPreferences.h"
#include "Voxel/VoxelConsole.h"
#include "Voxel/VoxelWorldSettings.h"
#include "VoxelLog.h"

namespace
{
    TAutoConsoleVariable<bool> CVarRenderDistanceFromOptions(TEXT("voxel.RenderDistance.FromOptions"), true,
        TEXT(
            "Options > Render Distance sets voxel.ViewRadius (the slider, 2-32) and voxel.FarRadius (twice that, up to "
            "24) at game-override priority, so the command line and the console still win. 0 puts back the project "
            "settings' radii (12 and 24)."));
}

VoxelRenderDistance::FRadii VoxelRenderDistance::FromChunks(int32 Chunks)
{
    const int32 View = FMath::Clamp(Chunks, MinChunks, MaxChunks);
    return {View, FMath::Min(2 * View, MaxFarRadius)};
}

VoxelRenderDistance::FRadii VoxelRenderDistance::GetWanted(
    bool bFromOptions, const FJsonValue* Preference, const FRadii& ProjectSettings)
{
    if (!bFromOptions) return ProjectSettings;
    return FromChunks(
        Preference && Preference->Type == EJson::Number ? FMath::RoundToInt32(Preference->AsNumber()) : DefaultChunks);
}

bool VoxelRenderDistance::SetRadius(IConsoleVariable& Variable, int32 Value)
{
    if ((Variable.GetFlags() & ECVF_SetByMask) > ECVF_SetByGameOverride || Variable.GetInt() == Value) return false;
    Variable.Set(Value, ECVF_SetByGameOverride);
    return true;
}

void VoxelRenderDistance::Apply(const UWorld* World)
{
    const UGameInstance* Game = World ? World->GetGameInstance() : nullptr;
    const UMenuPreferences* Preferences = Game ? Game->GetSubsystem<UMenuPreferences>() : nullptr;
    if (!Preferences) return;
    const UVoxelWorldSettings& Settings = UVoxelWorldSettings::Get();
    const TSharedPtr<const FJsonValue> Preference = Preferences->FindPreference(TEXT("renderDistance"));
    const FRadii Wanted = GetWanted(CVarRenderDistanceFromOptions.GetValueOnGameThread(), Preference.Get(),
        {Settings.ViewRadius, Settings.FarRadius});
    const bool bView = SetRadius(*VoxelConsole::CVarViewRadius.AsVariable(), Wanted.ViewRadius);
    const bool bFar = SetRadius(*VoxelConsole::CVarFarRadius.AsVariable(), Wanted.FarRadius);
    if (bView || bFar)
        UE_LOG(LogUnnamedVoxel, Display, TEXT("Render distance: voxel.ViewRadius %d, voxel.FarRadius %d (%s)."),
            VoxelConsole::CVarViewRadius.GetValueOnGameThread(), VoxelConsole::CVarFarRadius.GetValueOnGameThread(),
            CVarRenderDistanceFromOptions.GetValueOnGameThread() ? TEXT("Options > Render Distance")
                                                                 : TEXT("project settings"));
}
