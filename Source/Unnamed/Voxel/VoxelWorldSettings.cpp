#include "Voxel/VoxelWorldSettings.h"
#include "Voxel/VoxelConsole.h"

void UVoxelWorldSettings::PostInitProperties()
{
    Super::PostInitProperties();
    // Config is loaded before PostInitProperties, so the default object pushes the saved values once at startup.
    if (HasAnyFlags(RF_ClassDefaultObject)) PushToConsoleVariables();
}

#if WITH_EDITOR
void UVoxelWorldSettings::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
    Super::PostEditChangeProperty(PropertyChangedEvent);
    PushToConsoleVariables();
}
#endif

void UVoxelWorldSettings::PushToConsoleVariables() const
{
    using namespace VoxelConsole;
    CVarViewRadius->Set(ViewRadius, ECVF_SetByProjectSetting);
    CVarFarRadius->Set(FarRadius, ECVF_SetByProjectSetting);
    CVarUnloadMargin->Set(UnloadMargin, ECVF_SetByProjectSetting);
    CVarApplyBudgetMs->Set(ApplyBudgetMs, ECVF_SetByProjectSetting);
    CVarMaxTasksInFlight->Set(MaxTasksInFlight, ECVF_SetByProjectSetting);
    CVarAmbientOcclusion->Set(bAmbientOcclusion, ECVF_SetByProjectSetting);
    CVarGreedyMeshing->Set(bGreedyMeshing, ECVF_SetByProjectSetting);
}
