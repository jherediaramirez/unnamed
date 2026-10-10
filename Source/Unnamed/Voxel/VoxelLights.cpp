#include "Voxel/VoxelLights.h"
#include "Components/PointLightComponent.h"
#include "Foundation/FoundationProfile.h"
#include "HAL/IConsoleManager.h"
#include "HAL/PlatformTime.h"
#include "Voxel/VoxelWorld.h"
#include "VoxelConstants.h"
#include "VoxelLog.h"

namespace
{
    constexpr double LightsBlockCm = UFoundationProfile::BlockCm;
    // The torch's flame is small: a 5 cm source keeps its shadows sharp (T1's probe lights).
    constexpr float LightsSourceRadiusCm = 5;

    TAutoConsoleVariable<int32> CVarLights(TEXT("voxel.Lights"), 1,
        TEXT("The torch lights (session T3): 1 gives the brightest light sources around the camera a movable point "
             "light each while r.MegaLights.EnableForProject is 1; 0 turns them off. Either switch takes effect on the "
             "next frame; with MegaLights off the world holds no light component."),
        ECVF_Default);
    TAutoConsoleVariable<int32> CVarLightCount(TEXT("voxel.LightCount"), 64,
        TEXT("The most torch lights around the camera (K), 0 to 100. Sources closer than 2 blocks to a light's seed "
             "merge into it."),
        ECVF_Default);
    TAutoConsoleVariable<float> CVarLightRadius(TEXT("voxel.LightRadius"), 48,
        TEXT("Light sources farther than this from the camera (blocks) get no torch light."), ECVF_Default);
    TAutoConsoleVariable<float> CVarLightIntensity(TEXT("voxel.LightIntensity"), 1000,
        TEXT("Candelas of one torch light (a level-14 source); merged and dimmer sources scale it by their summed "
             "level over 14."),
        ECVF_Default);
    TAutoConsoleVariable<float> CVarLightTemperature(
        TEXT("voxel.LightTemperature"), 3000, TEXT("The torch lights' colour temperature (K)."), ECVF_Default);
    TAutoConsoleVariable<int32> CVarLightMinLevel(TEXT("voxel.LightMinLevel"), VoxelLightSources::DefaultMinLevel,
        TEXT("The light level a block state needs for a torch light (a torch is 14). A change makes a new light table, "
             "which a column uses from its next mesh; voxel.Regenerate meshes every column again."),
        ECVF_Default);

    bool IsSameSources(TConstArrayView<FVoxelLightSource> A, TConstArrayView<FVoxelLightSource> B)
    {
        if (A.Num() != B.Num()) return false;
        for (int32 Index = 0; Index < A.Num(); ++Index)
            if (A[Index].Key != B[Index].Key || A[Index].Level != B[Index].Level || A[Index].Point != B[Index].Point)
                return false;
        return true;
    }

    // ReadSettings runs every frame: the renderer's variable is looked up until found, then kept (as
    // FVoxelLightShots::Build does). The voxel.* variables above are read through their TAutoConsoleVariable, no
    // lookup.
    int32 GetMegaLightsForProject()
    {
        static const IConsoleVariable* MegaLights = nullptr;
        if (!MegaLights) MegaLights = IConsoleManager::Get().FindConsoleVariable(TEXT("r.MegaLights.EnableForProject"));
        return MegaLights ? MegaLights->GetInt() : 0;
    }
}

FVoxelLights::FVoxelLights(AActor* InOwner, TArray<TObjectPtr<UPointLightComponent>>& InComponents)
    : Owner(InOwner), bHasOwner(InOwner != nullptr), Components(InComponents)
{
}

FVoxelLights::FSettings FVoxelLights::ReadSettings()
{
    FSettings Settings;
    Settings.bLive = CVarLights.GetValueOnGameThread() != 0 && GetMegaLightsForProject() == 1;
    Settings.Count = FMath::Clamp(CVarLightCount.GetValueOnGameThread(), 0, VoxelLightSources::MaxLights);
    Settings.Radius = FMath::Max(0.f, CVarLightRadius.GetValueOnGameThread());
    Settings.IntensityCd = FMath::Max(0.f, CVarLightIntensity.GetValueOnGameThread());
    Settings.Temperature = FMath::Clamp(CVarLightTemperature.GetValueOnGameThread(), 1700.f, 12000.f);
    return Settings;
}

int32 FVoxelLights::ReadMinLevel()
{
    return FMath::Clamp(CVarLightMinLevel.GetValueOnGameThread(), 1, 15);
}

void FVoxelLights::SetColumn(FIntPoint Column, TArray<FVoxelLightSource>&& LightSources)
{
    TArray<FVoxelLightSource>* Found = Columns.Find(Column);
    if (LightSources.IsEmpty())
    {
        if (Found) ClearColumn(Column);
        return;
    }
    if (Found && IsSameSources(*Found, LightSources)) return;
    SourceCount += LightSources.Num() - (Found ? Found->Num() : 0);
    if (bPicked && IsInReach(Column, LastCamera, LastSettings.Radius)) bDirty = true;
    Columns.Add(Column, MoveTemp(LightSources));
}

void FVoxelLights::ClearColumn(FIntPoint Column)
{
    const TArray<FVoxelLightSource>* Found = Columns.Find(Column);
    if (!Found) return;
    SourceCount -= Found->Num();
    if (bPicked && IsInReach(Column, LastCamera, LastSettings.Radius)) bDirty = true;
    Columns.Remove(Column);
}

void FVoxelLights::Reset()
{
    Columns.Reset();
    SourceCount = 0;
    GatheredCount = 0;
    DestroyComponents();
    Picks.Reset();
    bDirty = false;
    bPicked = false;
    // The next update starts over, live or not.
    bLive = false;
}

void FVoxelLights::SetOrigin(const FVector& InOriginCm)
{
    // Only the origin is stored, for the next placements. The components are not moved: they are attached to the
    // actor's root, and a world rebase (AVoxelWorld::ApplyLogicalOffset runs inside the engine's ApplyWorldOffset)
    // already shifts the root and so every attached child, exactly as the chunk components, placed once in
    // AcquireChunk, are never set again. Setting them here would shift them twice. The picks are in blocks.
    OriginCm = InOriginCm;
}

bool FVoxelLights::Update(const FVector& CameraCm, const FSettings& InSettings)
{
    FSettings Settings = InSettings;
    Settings.Count = FMath::Clamp(Settings.Count, 0, VoxelLightSources::MaxLights);
    Settings.Radius = FMath::Max(0., Settings.Radius);
    if (!Settings.bLive)
    {
        LastSettings = Settings;
        if (!bLive && Slots.IsEmpty() && Components.IsEmpty()) return false;
        // Switched off: no light component may touch the frame.
        const double Start = FPlatformTime::Seconds();
        LastChanges = FChanges();
        LastChanges.Destroyed = Components.Num();
        DestroyComponents();
        Picks.Reset();
        bLive = false;
        bPicked = false;
        bDirty = false;
        LastMs = (FPlatformTime::Seconds() - Start) * 1000.;
        PeakMs = FMath::Max(PeakMs, LastMs);
        UE_LOG(LogUnnamedVoxel, Verbose, TEXT("voxel.Lights: off, %d components destroyed in %.3f ms"),
            LastChanges.Destroyed, LastMs);
        return false;
    }
    const FVector Camera = (CameraCm - OriginCm) / LightsBlockCm;
    const bool bChanged = !bLive || !(Settings == LastSettings);
    const bool bMoved = !bPicked || FVector::DistSquared(Camera, LastCamera) >= 1;
    if (!bChanged && !bMoved && !bDirty) return false;

    const double Start = FPlatformTime::Seconds();
    const bool bRetuneAll =
        bLive && (Settings.IntensityCd != LastSettings.IntensityCd || Settings.Temperature != LastSettings.Temperature);
    bLive = true;
    bPicked = true;
    bDirty = false;
    LastSettings = Settings;
    LastCamera = Camera;
    // Only the columns within reach: their sources are the only ones PickLights could keep.
    Gathered.Reset();
    for (const TPair<FIntPoint, TArray<FVoxelLightSource>>& Pair : Columns)
        if (IsInReach(Pair.Key, Camera, Settings.Radius)) Gathered.Append(Pair.Value);
    GatheredCount = Gathered.Num();
    VoxelLightSources::PickLights(
        Gathered, Camera, Settings.Count, Settings.Radius, VoxelLightSources::DefaultMergeDistance, Picks);
    Assign(Settings, bRetuneAll);
    ++UpdateCount;
    LastMs = (FPlatformTime::Seconds() - Start) * 1000.;
    PeakMs = FMath::Max(PeakMs, LastMs);
    UE_LOG(LogUnnamedVoxel, Verbose,
        TEXT("voxel.Lights: %d picks from %d sources in reach (%s) in %.3f ms: %d added, %d moved, %d retuned, %d "
             "freed, %d kept, %d created, %d destroyed"),
        Picks.Num(), GatheredCount,
        bChanged     ? TEXT("settings")
            : bMoved ? TEXT("camera moved")
                     : TEXT("sources changed"),
        LastMs, LastChanges.Added, LastChanges.Moved, LastChanges.Retuned, LastChanges.Freed, LastChanges.Kept,
        LastChanges.Created, LastChanges.Destroyed);
    return true;
}

void FVoxelLights::Assign(const FSettings& Settings, bool bRetuneAll)
{
    LastChanges = FChanges();
    // A pick whose key a slot shows keeps that slot.
    TMap<uint64, int32> SlotByKey;
    for (int32 Slot = 0; Slot < Slots.Num(); ++Slot)
        if (Slots[Slot].bUsed) SlotByKey.Add(Slots[Slot].Key, Slot);
    TArray<int32> PickSlots;
    PickSlots.Init(INDEX_NONE, Picks.Num());
    TArray<bool> Taken;
    Taken.Init(false, Slots.Num());
    for (int32 Index = 0; Index < Picks.Num(); ++Index)
        if (const int32* Found = SlotByKey.Find(Picks[Index].Key))
        {
            PickSlots[Index] = *Found;
            Taken[*Found] = true;
        }
    for (int32 Slot = 0; Slot < Slots.Num(); ++Slot)
        if (Slots[Slot].bUsed && !Taken[Slot]) Free(Slot);
    // New picks take the free slots in order, then new ones.
    int32 NextFree = 0;
    for (int32 Index = 0; Index < Picks.Num(); ++Index)
    {
        int32 Slot = PickSlots[Index];
        if (Slot == INDEX_NONE)
        {
            while (NextFree < Slots.Num() && Taken[NextFree])
                ++NextFree;
            if (NextFree < Slots.Num())
                Slot = NextFree;
            else
            {
                Slot = Slots.AddDefaulted();
                Taken.Add(false);
                if (bHasOwner)
                {
                    Components.Add(CreateComponent(Settings));
                    ++LastChanges.Created;
                }
            }
            Taken[Slot] = true;
        }
        Show(Slot, Picks[Index], Settings, bRetuneAll);
    }
    // Free slots past the count go, so a smaller count holds fewer components.
    while (Slots.Num() > Settings.Count && !Slots.Last().bUsed)
    {
        if (Components.Num() == Slots.Num())
        {
            if (UPointLightComponent* Light = Components.Pop().Get()) Light->DestroyComponent();
            ++LastChanges.Destroyed;
        }
        Slots.Pop();
    }
}

void FVoxelLights::Show(int32 Slot, const FVoxelLightPick& Pick, const FSettings& Settings, bool bRetuneAll)
{
    FSlot& State = Slots[Slot];
    const bool bNew = !State.bUsed || State.Key != Pick.Key;
    const bool bMove = bNew || State.Centroid != Pick.Centroid;
    const bool bRetune = bNew || bRetuneAll || State.Weight != Pick.Weight || State.Level != Pick.Level;
    if (!bMove && !bRetune)
    {
        ++LastChanges.Kept;
        return;
    }
    LastChanges.Added += bNew;
    LastChanges.Moved += !bNew && bMove;
    LastChanges.Retuned += !bNew && bRetune;
    State.bUsed = true;
    State.Key = Pick.Key;
    State.Centroid = Pick.Centroid;
    State.Weight = Pick.Weight;
    State.Level = Pick.Level;
    UPointLightComponent* Light = GetComponent(Slot);
    if (!Light) return;
    if (bMove) Light->SetWorldLocation(OriginCm + Pick.Centroid * LightsBlockCm);
    if (bRetune)
    {
        Light->SetIntensity(Settings.IntensityCd * Pick.Weight);
        // A level-L source lights L - 1 blocks around it.
        Light->SetAttenuationRadius(float(FMath::Max(1, int32(Pick.Level) - 1) * LightsBlockCm));
        Light->SetTemperature(Settings.Temperature);
    }
    if (!Light->IsVisible()) Light->SetVisibility(true);
}

void FVoxelLights::Free(int32 Slot)
{
    Slots[Slot] = FSlot();
    ++LastChanges.Freed;
    if (UPointLightComponent* Light = GetComponent(Slot)) Light->SetVisibility(false);
}

UPointLightComponent* FVoxelLights::GetComponent(int32 Slot) const
{
    return Components.IsValidIndex(Slot) ? Components[Slot].Get() : nullptr;
}

UPointLightComponent* FVoxelLights::CreateComponent(const FSettings& Settings)
{
    AActor* Actor = Owner.Get();
    if (!Actor) return nullptr;
    // T1's "megalights" probe light: movable, shadowed, MegaLights allowed, in candelas with inverse-squared falloff.
    UPointLightComponent* Light = NewObject<UPointLightComponent>(Actor, NAME_None, RF_Transient);
    Light->SetMobility(EComponentMobility::Movable);
    Light->CastShadows = true;
    Light->bAllowMegaLights = true;
    Light->IntensityUnits = ELightUnits::Candelas;
    Light->bUseInverseSquaredFalloff = true;
    Light->SourceRadius = LightsSourceRadiusCm;
    Light->bUseTemperature = true;
    Light->Temperature = Settings.Temperature;
    // Hidden until a pick shows it. Attached to the actor's root, as the chunks are, so world rebasing moves it.
    Light->SetVisibility(false);
    if (USceneComponent* Root = Actor->GetRootComponent()) Light->SetupAttachment(Root);
    if (Actor->GetWorld()) Light->RegisterComponent();
    return Light;
}

void FVoxelLights::DestroyComponents()
{
    for (const TObjectPtr<UPointLightComponent>& Light : Components)
        if (Light) Light->DestroyComponent();
    Components.Reset();
    Slots.Reset();
}

bool FVoxelLights::IsInReach(FIntPoint Column, const FVector& Camera, double Radius)
{
    // A light point can sit just outside its column (a cube's, 0.1 block past the cell): one block of margin.
    constexpr double Size = VoxelConstants::ColumnSize;
    const double MinX = Column.X * Size, MinY = Column.Y * Size;
    const double DX = FMath::Max3(MinX - Camera.X, 0., Camera.X - (MinX + Size));
    const double DY = FMath::Max3(MinY - Camera.Y, 0., Camera.Y - (MinY + Size));
    return DX * DX + DY * DY <= FMath::Square(Radius + 1);
}

TArray<TOptional<uint64>> FVoxelLights::GetSlotKeys() const
{
    TArray<TOptional<uint64>> Keys;
    for (const FSlot& Slot : Slots)
        Keys.Add(Slot.bUsed ? TOptional<uint64>(Slot.Key) : TOptional<uint64>());
    return Keys;
}

int32 FVoxelLights::GetComponentCount() const
{
    int32 Count = 0;
    for (const TObjectPtr<UPointLightComponent>& Light : Components)
        Count += Light != nullptr;
    return Count;
}

FString FVoxelLights::Describe() const
{
    int32 Shown = 0, Merged = 0;
    for (int32 Slot = 0; Slot < Slots.Num(); ++Slot)
        if (const UPointLightComponent* Light = GetComponent(Slot))
            Shown += Light->IsRegistered() && Light->IsVisible();
    for (const FVoxelLightPick& Pick : Picks)
        Merged += Pick.Members;
    return FString::Printf(
        TEXT("voxel.LightsInfo: pool %s (voxel.Lights %d, r.MegaLights.EnableForProject %d); %d columns with %d "
             "sources known, %d in reach at the last pick; %d picks of %d sources (K %d, radius %.0f blocks, %.0f cd, "
             "%.0f K, min level %d); %d components, %d shown; %lld updates, last %.3f ms, peak %.3f ms; the last: %d "
             "added, %d moved, %d retuned, %d freed, %d kept, %d created, %d destroyed"),
        bLive ? TEXT("live") : TEXT("not live"), CVarLights.GetValueOnGameThread(), GetMegaLightsForProject(),
        Columns.Num(), SourceCount, GatheredCount, Picks.Num(), Merged, LastSettings.Count, LastSettings.Radius,
        LastSettings.IntensityCd, LastSettings.Temperature, ReadMinLevel(), GetComponentCount(), Shown, UpdateCount,
        LastMs, PeakMs, LastChanges.Added, LastChanges.Moved, LastChanges.Retuned, LastChanges.Freed, LastChanges.Kept,
        LastChanges.Created, LastChanges.Destroyed);
}

namespace
{
    void RunLightsInfoCommand(UWorld* World)
    {
        const AVoxelWorld* Voxel = AVoxelWorld::Find(World);
        const FVoxelLights* Lights = Voxel ? Voxel->GetLights() : nullptr;
        if (!Lights)
        {
            UE_LOG(LogUnnamedVoxel, Display, TEXT("voxel.LightsInfo: no voxel world (L_Generated)"));
            return;
        }
        UE_LOG(LogUnnamedVoxel, Display, TEXT("%s"), *Lights->Describe());
    }

    FAutoConsoleCommandWithWorld GVoxelLightsInfoCommand(TEXT("voxel.LightsInfo"),
        TEXT("The torch lights' pool: whether it is live, the columns and sources it knows, its picks and components, "
             "and the last and peak update times."),
        FConsoleCommandWithWorldDelegate::CreateStatic(&RunLightsInfoCommand));
}
