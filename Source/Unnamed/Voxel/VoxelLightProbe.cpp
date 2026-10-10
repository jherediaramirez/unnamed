#include "Voxel/VoxelLightProbe.h"
#include "Blocks/VoxelBlockTable.h"
#include "Camera/PlayerCameraManager.h"
#include "Components/PointLightComponent.h"
#include "Foundation/FoundationBlockRegistry.h"
#include "Foundation/FoundationGrid.h"
#include "Foundation/FoundationProfile.h"
#include "HAL/IConsoleManager.h"
#include "Kismet/GameplayStatics.h"
#include "Storage/VoxelColumn.h"
#include "Voxel/VoxelBlocks.h"
#include "Voxel/VoxelColumnPipeline.h"
#include "Voxel/VoxelWorld.h"
#include "Voxel/VoxelWorldSubsystem.h"
#include "VoxelConstants.h"
#include "VoxelLog.h"

namespace
{
    constexpr double LightProbeCellCm = UFoundationProfile::BlockCm;
    // The lights live on the voxel world's actor; weak, so a world that ends takes them along.
    TArray<TWeakObjectPtr<UPointLightComponent>> GLightProbePool;
    // `voxel.LightProbe <N> torches`: a placed torch's cell, what the cell held before, and its world.
    struct FLightProbeTorch
    {
        FIntVector Cell;
        FFoundationBlockState Before;
        TWeakObjectPtr<AVoxelWorld> World;
    };
    TArray<FLightProbeTorch> GLightProbeTorches;
    // The torches RemoveTorches could not put back because their column was unloaded: the next RemoveTorches (also
    // the one PlaceTorches starts with) tries them again, so a torch that comes back with its column still goes.
    TArray<FLightProbeTorch> GLightProbePendingTorches;
}

bool VoxelLightProbe::ParseMode(const FString& Text, EMode& OutMode)
{
    for (const EMode Mode : {EMode::MegaLights, EMode::Vsm, EMode::None})
        if (Text.Equals(GetModeName(Mode), ESearchCase::IgnoreCase))
        {
            OutMode = Mode;
            return true;
        }
    return false;
}

const TCHAR* VoxelLightProbe::GetModeName(EMode Mode)
{
    return Mode == EMode::MegaLights ? TEXT("megalights") : Mode == EMode::Vsm ? TEXT("vsm") : TEXT("none");
}

VoxelLightProbe::FCells::FCells(const AVoxelWorld& World) : Table(VoxelBlocks::GetDefault()), OriginCm(World.OriginCm)
{
    if (const UVoxelWorldSubsystem* Streaming = UVoxelWorldSubsystem::Get(World.GetWorld()))
        Pipeline = Streaming->GetPipeline();
}

uint16 VoxelLightProbe::FCells::GetState(const FIntVector& Cell) const
{
    if (Cell.Z < VoxelConstants::MinZ || Cell.Z > VoxelConstants::MaxZ) return 0;
    constexpr int32 Size = VoxelConstants::ColumnSize;
    const FIntPoint Column(FoundationGrid::FloorDivide(Cell.X, Size), FoundationGrid::FloorDivide(Cell.Y, Size));
    const FVoxelColumn* Data = Pipeline->FindColumn(Column);
    return Data ? Data->GetState(Cell.X - Column.X * Size, Cell.Y - Column.Y * Size, Cell.Z) : uint16(1);
}

bool VoxelLightProbe::FCells::IsAir(const FIntVector& Cell) const
{
    return Table->Get(GetState(Cell)).Shape == EVoxelShape::Air;
}

bool VoxelLightProbe::FCells::IsSolid(const FIntVector& Cell) const
{
    return Table->Get(GetState(Cell)).bSolid;
}

bool VoxelLightProbe::FCells::IsOpaqueCube(const FIntVector& Cell) const
{
    const FVoxelStateInfo& Info = Table->Get(GetState(Cell));
    return Info.Shape == EVoxelShape::Full && Info.bCastsOcclusion;
}

int32 VoxelLightProbe::FCells::GetSurface(int32 X, int32 Y) const
{
    constexpr int32 Size = VoxelConstants::ColumnSize;
    const FIntPoint Column(FoundationGrid::FloorDivide(X, Size), FoundationGrid::FloorDivide(Y, Size));
    const FVoxelColumn* Data = Pipeline->FindColumn(Column);
    return Data ? Data->GetHeight(EVoxelHeightmap::WorldSurface, X - Column.X * Size, Y - Column.Y * Size)
                : VoxelConstants::MinZ - 1;
}

bool VoxelLightProbe::FCells::IsCovered(const FIntVector& Cell) const
{
    return Cell.Z < GetSurface(Cell.X, Cell.Y) - 3;
}

FVector VoxelLightProbe::FCells::ToCm(const FVector& Blocks) const
{
    return OriginCm + Blocks * LightProbeCellCm;
}

FVector VoxelLightProbe::FCells::ToBlocks(const FVector& Cm) const
{
    return (Cm - OriginCm) / LightProbeCellCm;
}

FIntVector VoxelLightProbe::FCells::ToCell(const FVector& Blocks)
{
    return FIntVector(FMath::FloorToInt32(Blocks.X), FMath::FloorToInt32(Blocks.Y), FMath::FloorToInt32(Blocks.Z));
}

bool VoxelLightProbe::FCells::IsClear(
    const FVector& From, const FVector& To, const FIntVector* Ignore, const FIntVector* Blocked) const
{
    // A voxel walk (Amanatides and Woo): the cells the segment crosses, in order.
    FIntVector Cell = ToCell(From);
    const FIntVector End = ToCell(To);
    const FVector Delta = To - From;
    int32 Step[3];
    double Next[3], Span[3];
    for (int32 Axis = 0; Axis < 3; ++Axis)
    {
        Step[Axis] = Delta[Axis] > 0 ? 1 : Delta[Axis] < 0 ? -1 : 0;
        if (Step[Axis] == 0)
        {
            Next[Axis] = Span[Axis] = TNumericLimits<double>::Max();
            continue;
        }
        const double Boundary = FMath::FloorToDouble(From[Axis]) + (Step[Axis] > 0 ? 1 : 0);
        Next[Axis] = (Boundary - From[Axis]) / Delta[Axis];
        Span[Axis] = Step[Axis] / Delta[Axis];
    }
    for (int32 Guard = 0; Guard < 4096 && Cell != End; ++Guard)
    {
        const int32 Axis = Next[0] < Next[1] ? (Next[0] < Next[2] ? 0 : 2) : (Next[1] < Next[2] ? 1 : 2);
        if (Next[Axis] > 1) break;
        Cell[Axis] += Step[Axis];
        Next[Axis] += Span[Axis];
        if (Blocked && Cell == *Blocked) return false;
        if (Cell != End && !IsAir(Cell) && !(Ignore && Cell == *Ignore)) return false;
    }
    return true;
}

TArray<FIntVector> VoxelLightProbe::FindFloorCells(const FCells& Cells, const FIntVector& Start, TSet<FIntVector>* Seen)
{
    TArray<FIntVector> Floors;
    if (!Cells.IsValid() || !Cells.IsAir(Start)) return Floors;
    constexpr int32 Across = 24, UpDown = 16;
    const FIntVector Steps[6] = {{1, 0, 0}, {-1, 0, 0}, {0, 1, 0}, {0, -1, 0}, {0, 0, 1}, {0, 0, -1}};
    const FIntVector Up(0, 0, 1);
    TSet<FIntVector> Filled = {Start};
    TArray<FIntVector> Fill = {Start};
    for (int32 Index = 0; Index < Fill.Num(); ++Index)
    {
        const FIntVector Cell = Fill[Index];
        if (Cells.IsSolid(Cell - Up) && Cells.IsAir(Cell + Up) && Cells.IsCovered(Cell)) Floors.Add(Cell);
        for (const FIntVector& Step : Steps)
        {
            const FIntVector Near = Cell + Step;
            if (FMath::Abs(Near.X - Start.X) > Across || FMath::Abs(Near.Y - Start.Y) > Across ||
                FMath::Abs(Near.Z - Start.Z) > UpDown || Filled.Contains(Near) || !Cells.IsAir(Near) ||
                !Cells.IsCovered(Near))
                continue;
            Filled.Add(Near);
            Fill.Add(Near);
        }
    }
    if (Seen) Seen->Append(Filled);
    return Floors;
}

TArray<FVector> VoxelLightProbe::FindFloorLights(
    const FCells& Cells, const FVector& EyeCm, const FVector& Forward, int32 Count)
{
    TArray<FVector> Positions;
    for (const FIntVector& Cell : FindFloorLightCells(Cells, EyeCm, Forward, Count))
        Positions.Add(Cells.ToCm(FVector(Cell) + FVector(.5, .5, 1)));
    return Positions;
}

TArray<FIntVector> VoxelLightProbe::FindFloorLightCells(
    const FCells& Cells, const FVector& EyeCm, const FVector& Forward, int32 Count)
{
    if (!Cells.IsValid() || Count <= 0) return {};
    const FVector Eye = Cells.ToBlocks(EyeCm);
    TArray<FIntVector> Front, Behind;
    for (const FIntVector& Cell : FindFloorCells(Cells, FCells::ToCell(Eye)))
        (FVector::DotProduct(FVector(Cell) + FVector(.5, .5, 1) - Eye, Forward) > 0 ? Front : Behind).Add(Cell);
    Front.Append(Behind);
    TArray<FIntVector> Kept;
    for (const double Spacing : {2., 0.})
        for (const FIntVector& Cell : Front)
        {
            if (Kept.Num() >= Count) break;
            bool bSpaced = !Kept.Contains(Cell);
            for (const FIntVector& Other : Kept)
                bSpaced &= FVector::DistSquared(FVector(Cell), FVector(Other)) >= Spacing * Spacing;
            if (bSpaced) Kept.Add(Cell);
        }
    return Kept;
}

int32 VoxelLightProbe::PlaceTorches(AVoxelWorld& World, const TArray<FIntVector>& FloorCells)
{
    RemoveTorches();
    FFoundationBlockWorld& Blocks = World.GetAuthority()->World;
    const FFoundationBlockType* Torch = Blocks.FindBlock(TEXT("torch"));
    if (!Torch) return 0;
    const FFoundationBlockState State = Torch->GetDefaultState();
    for (const FIntVector& Cell : FloorCells)
    {
        if (!Blocks.IsKnown(Cell)) continue;
        const FFoundationBlockState Before = Blocks.Get(Cell).State;
        if (Before == State) continue;
        GLightProbeTorches.Add({Cell, Before, TWeakObjectPtr<AVoxelWorld>(&World)});
        Blocks.Set(Cell, State);
    }
    return GLightProbeTorches.Num();
}

int32 VoxelLightProbe::RemoveTorches()
{
    int32 Removed = 0;
    TArray<FLightProbeTorch> Torches = MoveTemp(GLightProbePendingTorches);
    Torches.Append(MoveTemp(GLightProbeTorches));
    GLightProbePendingTorches.Reset();
    GLightProbeTorches.Reset();
    for (FLightProbeTorch& Placed : Torches)
    {
        // A world that ended took its torches along.
        AVoxelWorld* World = Placed.World.Get();
        if (!World) continue;
        FFoundationBlockWorld& Blocks = World->GetAuthority()->World;
        if (!Blocks.IsKnown(Placed.Cell))
        {
            // The column is unloaded: the torch comes back with it, so it is tried again later.
            GLightProbePendingTorches.Add(MoveTemp(Placed));
            continue;
        }
        // A torch that was popped or replaced since stays as it is.
        const FFoundationBlockType* Torch = Blocks.FindBlock(TEXT("torch"));
        if (Torch && Blocks.Get(Placed.Cell).State == Torch->GetDefaultState())
        {
            Blocks.Set(Placed.Cell, Placed.Before);
            ++Removed;
        }
    }
    if (!GLightProbePendingTorches.IsEmpty())
        UE_LOG(LogUnnamedVoxel, Display,
            TEXT("voxel.LightProbe: %d torches put back (%d not restored: column unloaded)"), Removed,
            GLightProbePendingTorches.Num());
    return Removed;
}

int32 VoxelLightProbe::Show(AVoxelWorld& World, const TArray<FVector>& Positions, const FSettings& Settings)
{
    // Lights of another world's actor (an earlier map) are not reused.
    GLightProbePool.RemoveAll(
        [&World](const TWeakObjectPtr<UPointLightComponent>& Light)
        {
            if (Light.IsValid() && Light->GetOwner() == &World) return false;
            if (Light.IsValid()) Light->DestroyComponent();
            return true;
        });
    while (GLightProbePool.Num() < Positions.Num())
    {
        UPointLightComponent* Light = NewObject<UPointLightComponent>(&World, NAME_None, RF_Transient);
        Light->SetMobility(EComponentMobility::Movable);
        Light->SetVisibility(false);
        Light->RegisterComponent();
        GLightProbePool.Add(Light);
    }
    for (int32 Index = 0; Index < GLightProbePool.Num(); ++Index)
    {
        UPointLightComponent* Light = GLightProbePool[Index].Get();
        const bool bShown = Index < Positions.Num();
        if (bShown)
        {
            Light->SetWorldLocation(Positions[Index]);
            Light->SetIntensityUnits(ELightUnits::Candelas);
            Light->SetIntensity(Settings.IntensityCd);
            Light->SetAttenuationRadius(Settings.RadiusCm);
            Light->SetSourceRadius(Settings.SourceRadiusCm);
            Light->SetCastShadows(Settings.Mode != EMode::None);
            Light->bAllowMegaLights = Settings.Mode == EMode::MegaLights;
            Light->MarkRenderStateDirty();
        }
        Light->SetVisibility(bShown);
    }
    return Positions.Num();
}

void VoxelLightProbe::Clear()
{
    for (const TWeakObjectPtr<UPointLightComponent>& Light : GLightProbePool)
        if (Light.IsValid()) Light->DestroyComponent();
    GLightProbePool.Reset();
}

int32 VoxelLightProbe::CountShown()
{
    int32 Shown = 0;
    for (const TWeakObjectPtr<UPointLightComponent>& Light : GLightProbePool)
        Shown += Light.IsValid() && Light->IsRegistered() && Light->IsVisible();
    return Shown;
}

#if !UE_BUILD_SHIPPING
namespace
{
    void RunLightProbeCommand(const TArray<FString>& Args, UWorld* World)
    {
        using namespace VoxelLightProbe;
        AVoxelWorld* Voxel = AVoxelWorld::Find(World);
        const APlayerCameraManager* Camera = UGameplayStatics::GetPlayerCameraManager(World, 0);
        FSettings Settings;
        const bool bTorches = Args.Num() > 1 && Args[1] == TEXT("torches");
        if (!Voxel || !Camera || Args.IsEmpty() || !Args[0].IsNumeric() ||
            (Args.Num() > 1 && !bTorches && !ParseMode(Args[1], Settings.Mode)))
        {
            UE_LOG(LogUnnamedVoxel, Display,
                TEXT("voxel.LightProbe <N> [megalights|vsm|none] [intensity cd] [radius cm], or <N> torches, in "
                     "L_Generated"));
            return;
        }
        const int32 Count = FMath::Clamp(FCString::Atoi(*Args[0]), 0, 1024);
        if (Count == 0)
        {
            Clear();
            const int32 Removed = RemoveTorches();
            UE_LOG(
                LogUnnamedVoxel, Display, TEXT("voxel.LightProbe: the lights are removed (and %d torches)"), Removed);
            return;
        }
        if (bTorches)
        {
            // The pool's lights instead of the probe's: torches on the same floor cells, through the block world.
            Clear();
            const TArray<FIntVector> FloorCells = FindFloorLightCells(
                FCells(*Voxel), Camera->GetCameraLocation(), Camera->GetCameraRotation().Vector(), Count);
            const int32 Placed = PlaceTorches(*Voxel, FloorCells);
            const IConsoleVariable* MegaLights =
                IConsoleManager::Get().FindConsoleVariable(TEXT("r.MegaLights.EnableForProject"));
            UE_LOG(LogUnnamedVoxel, Display,
                TEXT("voxel.LightProbe: %d of %d torches placed on cave floor cells; their lights come from the "
                     "light pool (voxel.LightsInfo)%s"),
                Placed, Count,
                MegaLights && MegaLights->GetInt() == 1
                    ? TEXT("")
                    : TEXT("; the pool is not live while r.MegaLights.EnableForProject is 0"));
            return;
        }
        if (Args.Num() > 2) Settings.IntensityCd = FMath::Max(0., FCString::Atod(*Args[2]));
        if (Args.Num() > 3) Settings.RadiusCm = FMath::Max(1., FCString::Atod(*Args[3]));
        const TArray<FVector> Positions =
            FindFloorLights(FCells(*Voxel), Camera->GetCameraLocation(), Camera->GetCameraRotation().Vector(), Count);
        const int32 Shown = Show(*Voxel, Positions, Settings);
        const IConsoleVariable* MegaLights =
            IConsoleManager::Get().FindConsoleVariable(TEXT("r.MegaLights.EnableForProject"));
        const bool bMegaLightsOff = Settings.Mode == EMode::MegaLights && MegaLights && MegaLights->GetInt() == 0;
        UE_LOG(LogUnnamedVoxel, Display,
            TEXT("voxel.LightProbe: %d of %d lights on cave floor cells (%s, %.0f cd, "
                 "radius %.0f cm)%s"),
            Shown, Count, GetModeName(Settings.Mode), Settings.IntensityCd, Settings.RadiusCm,
            bMegaLightsOff ? TEXT("; MegaLights is off for this run (r.MegaLights.EnableForProject 1 turns it on), "
                                  "so they draw shadow-mapped")
                           : TEXT(""));
    }

    FAutoConsoleCommandWithWorldAndArgs GVoxelLightProbeCommand(TEXT("voxel.LightProbe"),
        TEXT("voxel.LightProbe <N> [megalights|vsm|none] [intensity cd] [radius cm]: development only. N movable "
             "point lights on cave floor cells around the view (1 m up, radius 1300 cm by default); 0 removes them. "
             "megalights needs r.MegaLights.EnableForProject 1 for the run. voxel.LightProbe <N> torches places a "
             "floor torch on each of those cells instead, lit by the light pool; 0 takes them back."),
        FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&RunLightProbeCommand));
}
#endif
