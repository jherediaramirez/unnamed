#include "Voxel/VoxelFluidSystem.h"
#include "Blocks/VoxelBlockTable.h"
#include "Engine/World.h"
#include "HAL/IConsoleManager.h"
#include "Storage/VoxelColumn.h"
#include "Voxel/VoxelColumnPipeline.h"
#include "Voxel/VoxelWorld.h"
#include "VoxelConstants.h"
#include "VoxelLog.h"

namespace
{
    TAutoConsoleVariable<int32> CVarFluids(TEXT("voxel.Fluids"), 0,
        TEXT(
            "Flowing water and lava in voxel worlds (water and sky W2a). 1 runs them; 0, the default until fluids mesh "
            "at their levels (W3), stops them and ignores edits (the control)."));
    TAutoConsoleVariable<float> CVarFluidsBudgetMs(TEXT("voxel.Fluids.BudgetMs"), 0.5f,
        TEXT("Game-thread milliseconds a frame's fluid acts may take (plan Q10). Past it the fluids fall behind and "
             "catch up later; 0 or less, no limit."));
    TAutoConsoleVariable<int32> CVarFluidsSimRadius(TEXT("voxel.Fluids.SimRadius"), -1,
        TEXT("Fluids act at most this many columns from the streaming center on both axes, inside the view less one "
             "column; negative (the default), that whole ring (plan Q6)."));

    // The blocks the rules name, by EVoxelFluidBlock.
    const TCHAR* const FluidBlockNames[] = {nullptr, TEXT("soul_soil"), TEXT("blue_ice"), TEXT("obsidian"),
        TEXT("cobblestone"), TEXT("stone"), TEXT("basalt")};
    static_assert(UE_ARRAY_COUNT(FluidBlockNames) == int32(EVoxelFluidBlock::Basalt) + 1);
}

FVoxelFluidSystem::FSettings FVoxelFluidSystem::FSettings::Read()
{
    FSettings Settings;
    Settings.bEnabled = CVarFluids.GetValueOnGameThread() != 0;
    Settings.BudgetMs = CVarFluidsBudgetMs.GetValueOnGameThread();
    Settings.SimRadius = CVarFluidsSimRadius.GetValueOnGameThread();
    return Settings;
}

FVoxelFluidSystem::~FVoxelFluidSystem()
{
    Stop();
}

bool FVoxelFluidSystem::Start(FFoundationBlockWorld& InWorld, TSharedRef<const FVoxelBlockTable> InTable,
    TFunction<FVoxelColumnPipeline*()> InGetPipeline, const FVoxelFluidOptions& InOptions, uint64 InTick)
{
    Stop();
    Table = InTable;
    FMemory::Memzero(WaterStates);
    FMemory::Memzero(LavaStates);
    for (int32 State = 0; State < Table->GetStateCount(); ++State)
    {
        const FVoxelStateInfo& Info = Table->Get(uint16(State));
        uint16* Levels = !Info.IsFluid()       ? nullptr
            : Info.Fluid == EVoxelFluid::Water ? WaterStates
            : Info.Fluid == EVoxelFluid::Lava  ? LavaStates
                                               : nullptr;
        if (Levels && !Levels[Info.FluidLevel & 15]) Levels[Info.FluidLevel & 15] = uint16(State);
    }
    bool bComplete = true;
    for (int32 Level = 0; Level < 16; ++Level)
        bComplete &= WaterStates[Level] != 0 && LavaStates[Level] != 0;
    for (int32 Block = 1; Block < UE_ARRAY_COUNT(FluidBlockNames); ++Block)
    {
        BlockStates[Block] = Table->FindState(FName(FluidBlockNames[Block]));
        bComplete &= BlockStates[Block] != 0;
    }
    if (!bComplete)
    {
        UE_LOG(LogUnnamedVoxel, Error,
            TEXT("Fluids are off: the block table lacks a water or lava level, or a block "
                 "the fluid rules write."));
        Table.Reset();
        return false;
    }
    Cells.SetNum(Table->GetStateCount());
    for (int32 State = 0; State < Cells.Num(); ++State)
        Cells[State] = MakeCell(uint16(State));

    World = &InWorld;
    GetPipeline = MoveTemp(InGetPipeline);
    Options = InOptions;
    // The switches as they are now, so edits before the first frame wake the fluids when they are on.
    Settings = FSettings::Read();
    Tick = Target = InTick;
    bInTick = false;
    bHasRing = false;
    Stats = FStats();
    World->OnSet = [this](const FIntVector& Cell, FFoundationBlockState Before, FFoundationBlockState After)
    { OnSet(Cell, Before, After); };
    return true;
}

void FVoxelFluidSystem::Stop()
{
    if (World) World->OnSet = nullptr;
    World = nullptr;
    Reset();
}

void FVoxelFluidSystem::Reset()
{
    Scheduler.Reset();
    Overlay.Reset();
    OverlayOrder.Reset();
    // Nothing is left to catch up on.
    Tick = Target;
    bInTick = false;
    bHasRing = false;
}

FVoxelFluidCell FVoxelFluidSystem::MakeCell(uint16 State) const
{
    const FVoxelStateInfo& Info = Table->Get(State);
    FVoxelFluidCell Cell;
    Cell.Fluid = Info.Fluid;
    Cell.Level = Info.Fluid == EVoxelFluid::None ? 0 : Info.FluidLevel;
    Cell.bSolid = Info.bSolid;
    switch (Info.Shape)
    {
    case EVoxelShape::Air:
    case EVoxelShape::Fluid:
        break;
    case EVoxelShape::Full:
        Cell.Walls = 0x3F;
        break;
    case EVoxelShape::SlabBottom:
        Cell.Walls = uint8(1u << int32(EVoxelFace::Down));
        Cell.bWaterloggable = true;
        break;
    case EVoxelShape::SlabTop:
        Cell.Walls = uint8(1u << int32(EVoxelFace::Up));
        Cell.bWaterloggable = true;
        break;
    default:
        // Models and snow layers holding no fluid (plants, torches, doors): walls until W2b's washed-away tags, so
        // fluid never removes a block unseen. Those holding water (kelp, seagrass) are its sources.
        if (Cell.Fluid == EVoxelFluid::None) Cell.Walls = 0x3F;
        break;
    }
    for (int32 Block = 1; Block < UE_ARRAY_COUNT(FluidBlockNames); ++Block)
        if (State == BlockStates[Block] || Info.Block == Table->Get(BlockStates[Block]).Block)
        {
            Cell.Block = EVoxelFluidBlock(Block);
            break;
        }
    return Cell;
}

const FVoxelFluidCell& FVoxelFluidSystem::GetCell(uint16 State) const
{
    static const FVoxelFluidCell Air;
    return State < Cells.Num() ? Cells[State] : Air;
}

uint16 FVoxelFluidSystem::GetState(const FVoxelFluidCell& Cell) const
{
    // The rules write only fluid into cells holding none or fluid alone, and the blocks they make, which replace a cell
    // whole (lava falling on kelp makes stone): no write keeps a block with fluid in it, so none needs its state here.
    if (Cell.Fluid == EVoxelFluid::Water) return WaterStates[Cell.Level & 15];
    if (Cell.Fluid == EVoxelFluid::Lava) return LavaStates[Cell.Level & 15];
    // Air for none.
    return BlockStates[int32(Cell.Block)];
}

FVoxelFluidCell FVoxelFluidSystem::Get(FIntVector Cell) const
{
    if (!Overlay.IsEmpty())
        if (const FOverlayCell* Found = Overlay.Find(Cell)) return Found->Cell;
    // Below the world a floor fluid never leaves through; above it, air nothing reaches (fluid never flows up).
    if (Cell.Z < VoxelConstants::MinZ) return FVoxelFluidCell::MakeSolid();
    if (Cell.Z > VoxelConstants::MaxZ) return FVoxelFluidCell();
    const FIntPoint Column(Cell.X >> 4, Cell.Y >> 4);
    if (!bCached || Column != CachedColumn)
    {
        const FVoxelColumnPipeline* Source = Pipeline ? Pipeline : GetPipeline ? GetPipeline() : nullptr;
        CachedData = Source ? Source->FindColumn(Column) : nullptr;
        CachedColumn = Column;
        // Outside Advance and OnSet the pipeline may change before the next read.
        bCached = Pipeline != nullptr;
    }
    if (!CachedData)
    {
        if (!bReadUnknown)
        {
            bReadUnknown = true;
            UnknownColumn = Column;
        }
        FVoxelFluidCell Unknown;
        Unknown.bUnknown = true;
        return Unknown;
    }
    return GetCell(CachedData->GetState(Cell.X & 15, Cell.Y & 15, Cell.Z));
}

void FVoxelFluidSystem::Set(FIntVector Cell, const FVoxelFluidCell& Value)
{
    FOverlayCell* Found = Overlay.Find(Cell);
    if (!Found)
    {
        Found = &Overlay.Add(Cell);
        OverlayOrder.Add(Cell);
    }
    // Read back as the state it is written as, so the overlay and the flushed world agree.
    Found->State = GetState(Value);
    Found->Cell = GetCell(Found->State);
    ++Stats.Writes;
}

bool FVoxelFluidSystem::IsInRing(FIntPoint Column) const
{
    if (!bHasRing) return false;
    const FIntPoint Offset = Column - Ring.Center;
    if (RingSimRadius >= 0 && FMath::Max(FMath::Abs(Offset.X), FMath::Abs(Offset.Y)) > RingSimRadius) return false;
    // The view less one column: the farthest column of its 3 x 3 is in view.
    return Ring.IsInView(
        Ring.Center + FIntPoint(Offset.X + (Offset.X < 0 ? -1 : 1), Offset.Y + (Offset.Y < 0 ? -1 : 1)));
}

void FVoxelFluidSystem::Advance(int32 Ticks, const FSettings& InSettings)
{
    Settings = InSettings;
    Stats.LastMs = 0;
    Stats.LastActs = 0;
    if (!World || !Settings.bEnabled) return;
    Pipeline = GetPipeline();
    if (!Pipeline) return;
    const uint64 Start = FPlatformTime::Cycles64();
    const uint64 Budget = Settings.BudgetMs > 0
        ? FMath::Max<uint64>(1, uint64(Settings.BudgetMs / 1000. / FPlatformTime::GetSecondsPerCycle64()))
        : MAX_uint64;
    Target += uint64(FMath::Max(Ticks, 0));
    // Until measured: 10 microseconds an act and 2 a flushed cell (scout M's 1-4 per act, W1's 10-18 in the test
    // world).
    if (ActCycles < 0) ActCycles = 10e-6 / FPlatformTime::GetSecondsPerCycle64();
    if (FlushCyclesPerCell < 0) FlushCyclesPerCell = 2e-6 / FPlatformTime::GetSecondsPerCycle64();
    bCached = false;
    UpdateRing(Pipeline->GetRings());
    int32 Acts = 0;
    bool bCut = false;
    for (;;)
    {
        if (!bInTick)
        {
            if (Tick >= Target) break;
            // Ticks with nothing due are skipped.
            uint64 Next = Target, Due = 0;
            if (Scheduler.PeekDue(Due)) Next = FMath::Clamp<uint64>(Due, Tick + 1, Target);
            Tick = Next;
            bInTick = true;
        }
        FVoxelFluidScheduler::FAct Act;
        if (!Scheduler.PopDue(Tick, Act))
        {
            bInTick = false;
            continue;
        }
        if (Acts == 0)
        {
            // The costliest act and flushed cell of the last frames that acted: they fall by a tenth a frame that acts,
            // so one slow act holds the acts back for a few frames, and the quiet frames between waves keep them.
            ActCycles *= .9;
            FlushCyclesPerCell *= .9;
        }
        const uint64 ActStart = FPlatformTime::Cycles64();
        RunAct(Act);
        ++Acts;
        const uint64 Now = FPlatformTime::Cycles64();
        ActCycles = FMath::Max(ActCycles, double(Now - ActStart));
        // The next act and the flush of what the acts wrote come out of the same budget.
        if (Now - Start + uint64(ActCycles + OverlayOrder.Num() * FlushCyclesPerCell) >= Budget)
        {
            // The rest of the tick's acts run next frame, before any later tick's.
            bCut = true;
            break;
        }
    }
    Flush();
    Pipeline = nullptr;
    bCached = false;
    Stats.LastMs = FPlatformTime::ToMilliseconds64(FPlatformTime::Cycles64() - Start);
    Stats.LastActs = Acts;
    Stats.WorstMs = FMath::Max(Stats.WorstMs, Stats.LastMs);
    Stats.OverBudgetFrames += Settings.BudgetMs > 0 && Stats.LastMs > Settings.BudgetMs;
    Stats.CutFrames += bCut;
}

void FVoxelFluidSystem::UpdateRing(const FVoxelStreamingRings& Rings)
{
    if (!bHasRing || !(Rings == Ring) || RingSimRadius != Settings.SimRadius)
    {
        Ring = Rings;
        RingSimRadius = Settings.SimRadius;
        bHasRing = true;
        Stats.Sleeps += Scheduler.ParkAsleep(Tick, [this](FIntPoint Column) { return !IsInRing(Column); });
    }
    // Acts waiting for a column to be final, or (asleep) back in the ring as well, all together in their first order.
    // Never into a tick already started (a frame cut by the budget leaves one open): they run at the next tick whether
    // or not the last frame was cut, so the budget changes when, never what.
    const auto Release = [this](FIntPoint Column)
    {
        return !Pipeline->IsGenerated(Column) ? 0 : IsInRing(Column) ? 2 : 1;
    };
    if (Scheduler.GetParkedCount()) Scheduler.Unpark(Tick, Release, Tick + 1);
}

void FVoxelFluidSystem::RunAct(const FVoxelFluidScheduler::FAct& Act)
{
    const FIntPoint Column = FVoxelFluidScheduler::GetColumn(Act.Cell);
    if (!IsInRing(Column))
    {
        Scheduler.Park(Column, Act, Tick, true);
        ++Stats.Sleeps;
        return;
    }
    Scratch.Reset();
    bReadUnknown = false;
    if (!VoxelFluidRules::Act(*this, Act.Cell, Act.Fluid, Options, Scratch))
    {
        Scheduler.Park(bReadUnknown ? UnknownColumn : Column, Act, Tick, false);
        ++Stats.Waits;
        return;
    }
    ++Stats.Acts;
    for (const FVoxelFluidWrite& Write : Scratch)
        VoxelFluidRules::Apply(*this, Scheduler, Tick, Write, Options);
}

void FVoxelFluidSystem::Flush()
{
    if (OverlayOrder.IsEmpty()) return;
    const uint64 Start = FPlatformTime::Cycles64();
    const int32 Written = OverlayOrder.Num();
    TArray<TPair<FIntVector, FFoundationBlockState>> Changes;
    Changes.Reserve(OverlayOrder.Num());
    for (const FIntVector& Cell : OverlayOrder)
    {
        const uint16 State = Overlay.FindChecked(Cell).State;
        // The IsKnown guard: an act reads every cell it writes, and waits when one is unknown, so this never fires.
        if (!World->IsKnown(Cell))
        {
            ++Stats.Lost;
            continue;
        }
        if (World->Get(Cell).State.Id != State) Changes.Add({Cell, FFoundationBlockState{State}});
    }
    Overlay.Reset();
    OverlayOrder.Reset();
    // The rules woke the neighbours as they wrote: the hook stays out of it.
    bFlushing = true;
    World->SetMany(Changes);
    bFlushing = false;
    bCached = false;
    const double PerCell = double(FPlatformTime::Cycles64() - Start) / Written;
    FlushCyclesPerCell = FMath::Max(FlushCyclesPerCell, PerCell);
}

void FVoxelFluidSystem::OnSet(const FIntVector& Cell, FFoundationBlockState Before, FFoundationBlockState After)
{
    if (bFlushing || !World || !Settings.bEnabled || Before == After) return;
    Pipeline = GetPipeline();
    if (!Pipeline) return;
    bCached = false;
    VoxelFluidRules::WakeAround(*this, Scheduler, Tick, Cell, Options, true);
    Flush();
    Pipeline = nullptr;
    bCached = false;
}

FVoxelFluidSystem::FStats FVoxelFluidSystem::GetStats() const
{
    FStats Out = Stats;
    Out.Tick = Tick;
    Out.Behind = Target - Tick + (bInTick ? 1 : 0);
    Out.Queued = Scheduler.Num();
    Out.Parked = Scheduler.GetParkedCount();
    return Out;
}

FString FVoxelFluidSystem::Describe() const
{
    const FStats Out = GetStats();
    return FString::Printf(TEXT("Fluids %s: tick %llu (%llu behind), %d queued, %d parked; %lld acts, %lld waits, "
                                "%lld sleeps, %lld writes, %lld lost; last frame %.3f ms (%d acts), worst %.3f ms, "
                                "%lld frames over %.2f ms, %lld stopped at it"),
        !World                  ? TEXT("not running")
            : Settings.bEnabled ? TEXT("on")
                                : TEXT("off (voxel.Fluids 0)"),
        Out.Tick, Out.Behind, Out.Queued, Out.Parked, Out.Acts, Out.Waits, Out.Sleeps, Out.Writes, Out.Lost, Out.LastMs,
        Out.LastActs, Out.WorstMs, Out.OverBudgetFrames, Settings.BudgetMs, Out.CutFrames);
}

namespace
{
    FAutoConsoleCommandWithWorld GVoxelFluidsInfoCommand(TEXT("voxel.Fluids.Info"),
        TEXT("Logs the voxel world's fluids: their tick, pending and parked acts, and frame cost."),
        FConsoleCommandWithWorldDelegate::CreateLambda(
            [](UWorld* World)
            {
                const AVoxelWorld* Voxel = AVoxelWorld::Find(World);
                const FVoxelFluidSystem* Fluids = Voxel ? Voxel->GetFluids() : nullptr;
                UE_LOG(LogUnnamedVoxel, Display, TEXT("%s"),
                    Fluids ? *Fluids->Describe() : TEXT("voxel.Fluids.Info: no voxel world"));
            }));
}
