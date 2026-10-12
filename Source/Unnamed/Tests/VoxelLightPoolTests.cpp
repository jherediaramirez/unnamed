#if WITH_DEV_AUTOMATION_TESTS
#include "Blocks/VoxelBlockTable.h"
#include "Components/PointLightComponent.h"
#include "Foundation/FoundationBlockRegistry.h"
#include "GameFramework/Actor.h"
#include "HAL/IConsoleManager.h"
#include "Misc/AutomationTest.h"
#include "Random/VoxelSeed.h"
#include "Storage/VoxelColumn.h"
#include "Streaming/VoxelStreamingPlanner.h"
#include "Terrain/VoxelHeightTerrain.h"
#include "Voxel/VoxelBlocks.h"
#include "Voxel/VoxelColumnPipeline.h"
#include "Voxel/VoxelLightSources.h"
#include "Voxel/VoxelLights.h"
#include "VoxelConstants.h"

namespace
{
    // A torch-like source at a cell: its point 11 pixels above the cell's floor.
    FVoxelLightSource MakePoolTestSource(const FIntVector& Cell, uint8 Level = 14)
    {
        FVoxelLightSource Source;
        Source.Cell = Cell;
        Source.Point = FVector(Cell) + FVector(.5, .5, VoxelLightSources::FloorTorchHeight);
        Source.Level = Level;
        Source.Key = VoxelLightSources::MakeKey(Cell);
        return Source;
    }

    FIntPoint GetPoolTestColumn(const FIntVector& Cell)
    {
        return FIntPoint(FMath::FloorToInt32(Cell.X / 16.), FMath::FloorToInt32(Cell.Y / 16.));
    }

    // Hands the pool every source, per column, as the streaming would.
    void SetPoolTestSources(FVoxelLights& Pool, TConstArrayView<FVoxelLightSource> LightSources, TSet<FIntPoint>& Known)
    {
        TMap<FIntPoint, TArray<FVoxelLightSource>> ByColumn;
        for (const FVoxelLightSource& Source : LightSources)
            ByColumn.FindOrAdd(GetPoolTestColumn(Source.Cell)).Add(Source);
        for (const FIntPoint Column : Known)
            if (!ByColumn.Contains(Column)) Pool.ClearColumn(Column);
        Known.Reset();
        for (TPair<FIntPoint, TArray<FVoxelLightSource>>& Pair : ByColumn)
        {
            Known.Add(Pair.Key);
            Pool.SetColumn(Pair.Key, MoveTemp(Pair.Value));
        }
    }

    // The slot each key is shown in.
    TMap<uint64, int32> GetPoolTestSlots(const FVoxelLights& Pool)
    {
        TMap<uint64, int32> Slots;
        const TArray<TOptional<uint64>> Keys = Pool.GetSlotKeys();
        for (int32 Slot = 0; Slot < Keys.Num(); ++Slot)
            if (Keys[Slot]) Slots.Add(*Keys[Slot], Slot);
        return Slots;
    }

    int32 SlotOf(const TMap<uint64, int32>& Slots, uint64 Key, int32 Missing)
    {
        const int32* Found = Slots.Find(Key);
        return Found ? *Found : Missing;
    }

    // Saves a console variable's value and set-by priority, and restores both when it goes, even if a row returns
    // early. The rows set by code, which outranks a project setting: a plain Set at the saved priority would be
    // ignored, so the code entry is unset (the history, where there is one) or the value set by code and the
    // priority put back.
    class FPoolTestCVarGuard
    {
    public:
        explicit FPoolTestCVarGuard(IConsoleVariable& InVariable)
            : Variable(InVariable), Value(InVariable.GetString()), SetBy(GetSetBy(InVariable))
        {
        }
        ~FPoolTestCVarGuard()
        {
            if (SetBy < ECVF_SetByCode) Variable.Unset(ECVF_SetByCode);
            if (!IsRestored())
            {
                Variable.Set(*Value, ECVF_SetByCode);
                Variable.SetFlags(
                    EConsoleVariableFlags((uint32(Variable.GetFlags()) & ~uint32(ECVF_SetByMask)) | SetBy));
            }
        }
        FPoolTestCVarGuard(const FPoolTestCVarGuard&) = delete;
        FPoolTestCVarGuard& operator=(const FPoolTestCVarGuard&) = delete;

        static uint32 GetSetBy(const IConsoleVariable& Each) { return uint32(Each.GetFlags()) & ECVF_SetByMask; }
        bool IsRestored() const { return Variable.GetString() == Value && GetSetBy(Variable) == SetBy; }

    private:
        IConsoleVariable& Variable;
        const FString Value;
        const uint32 SetBy;
    };
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVoxelLightPoolTest, "Unnamed.Voxel.LightPool",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FVoxelLightPoolTest::RunTest(const FString& Parameters)
{
    int32 Rows = 0, Passed = 0;
    auto Row = [this, &Rows, &Passed](const FString& What, bool bOk)
    {
        ++Rows;
        Passed += bOk;
        TestTrue(What, bOk);
    };
    // The defaults the console variables start at: warm A, Jesus's pick in the torch look round (T3b).
    {
        const FVoxelLights::FSettings Defaults;
        Row(TEXT("Defaults: 64 lights within 48 blocks, 600 cd at 2,200 K, inverse squared (falloff 0)"),
            Defaults.Count == 64 && Defaults.Radius == 48 && Defaults.IntensityCd == 600 &&
                Defaults.Temperature == 2200 && Defaults.Falloff == 0);
    }
    FVoxelLights::FSettings Live;
    Live.bLive = true;
    Live.Count = 4;
    Live.Radius = 48;

    // --- Assignment by key -----------------------------------------------------------------------------------------
    {
        // No owner: the pool keeps its slots and picks without components.
        TArray<TObjectPtr<UPointLightComponent>> Components;
        FVoxelLights Pool(nullptr, Components);
        TSet<FIntPoint> Known;
        // Six sources 4 blocks apart along X, in two columns; K = 4 keeps the four nearest the camera at the origin.
        TArray<FVoxelLightSource> LightSources;
        for (int32 Index = 0; Index < 6; ++Index)
            LightSources.Add(MakePoolTestSource(FIntVector(Index * 4 + 1, 2, 64)));
        SetPoolTestSources(Pool, LightSources, Known);
        const FVector Camera = FVector(0, 2, 65) * 100;
        Row(TEXT("Assign: the first update picks"), Pool.Update(Camera, Live));
        Row(TEXT("Assign: 6 sources in 2 columns"), Pool.GetSourceCount() == 6 && Pool.GetColumnCount() == 2);
        Row(TEXT("Assign: 4 picks, 4 slots, no components without an owner"),
            Pool.GetPicks().Num() == 4 && Pool.GetSlotKeys().Num() == 4 && Pool.GetComponentCount() == 0 &&
                Components.IsEmpty());
        const TMap<uint64, int32> First = GetPoolTestSlots(Pool);

        // The nearest source goes: the three others keep their slots, and the fifth takes the freed one.
        TArray<FVoxelLightSource> Fewer = LightSources;
        Fewer.RemoveAt(0);
        SetPoolTestSources(Pool, Fewer, Known);
        Row(TEXT("Assign: a changed column within reach re-picks"), Pool.Update(Camera, Live));
        const TMap<uint64, int32> Second = GetPoolTestSlots(Pool);
        bool bKept = Second.Num() == 4;
        for (int32 Index = 1; Index < 4; ++Index)
            bKept &= SlotOf(Second, LightSources[Index].Key, -1) == SlotOf(First, LightSources[Index].Key, -2);
        Row(TEXT("Assign: the lights that stay keep their slot"), bKept);
        Row(TEXT("Assign: the new light takes the freed slot"),
            SlotOf(Second, LightSources[4].Key, -1) == SlotOf(First, LightSources[0].Key, -2));

        // The same sources again change nothing.
        SetPoolTestSources(Pool, Fewer, Known);
        const int64 Updates = Pool.GetUpdateCount();
        Row(TEXT("Assign: the same sources again do not re-pick"),
            !Pool.Update(Camera, Live) && Pool.GetUpdateCount() == Updates);
        // A source far out of reach changes nothing either.
        TArray<FVoxelLightSource> Far = Fewer;
        Far.Add(MakePoolTestSource(FIntVector(1000, 1000, 64)));
        SetPoolTestSources(Pool, Far, Known);
        Row(TEXT("Assign: a column out of reach does not re-pick"), !Pool.Update(Camera, Live));

        // --- Camera moves ------------------------------------------------------------------------------------------
        Row(TEXT("Camera: 0.9 blocks does not re-pick"), !Pool.Update(Camera + FVector(90, 0, 0), Live));
        Row(TEXT("Camera: 0.9 blocks the other way does not re-pick"), !Pool.Update(Camera + FVector(0, -90, 0), Live));
        Row(TEXT("Camera: 1 block from the last pick re-picks"), Pool.Update(Camera + FVector(100, 0, 0), Live));
        Row(TEXT("Camera: the moved pick keeps every slot of the lights that stay"),
            GetPoolTestSlots(Pool).OrderIndependentCompareEqual(Second));
        Row(TEXT("Camera: only the 1-block move re-picked"), Pool.GetUpdateCount() == Updates + 1);

        // --- Settings ----------------------------------------------------------------------------------------------
        FVoxelLights::FSettings Brighter = Live;
        Brighter.IntensityCd *= 2;
        Row(TEXT("Settings: a new intensity re-picks"), Pool.Update(Camera + FVector(100, 0, 0), Brighter));
        Row(TEXT("Settings: and keeps every slot"), GetPoolTestSlots(Pool).OrderIndependentCompareEqual(Second));
        FVoxelLights::FSettings Two = Brighter;
        Two.Count = 2;
        Row(TEXT("Settings: K = 2 re-picks"), Pool.Update(Camera + FVector(100, 0, 0), Two));
        // The camera is at block X 1: the nearest that stay are the sources at X 5 and 9.
        const TSet<uint64> Nearest = {LightSources[1].Key, LightSources[2].Key};
        TSet<uint64> Picked;
        for (const FVoxelLightPick& Pick : Pool.GetPicks())
            Picked.Add(Pick.Key);
        TSet<uint64> Shown;
        GetPoolTestSlots(Pool).GetKeys(Shown);
        Row(TEXT("Settings: K = 2 shows the two nearest"),
            Pool.GetPicks().Num() == 2 && Picked.Num() == 2 && Picked.Includes(Nearest) && Shown.Num() == 2 &&
                Shown.Includes(Nearest));

        // --- The switch --------------------------------------------------------------------------------------------
        FVoxelLights::FSettings Off = Two;
        Off.bLive = false;
        Pool.Update(Camera, Off);
        Row(TEXT("Switch: off holds no picks and no slots"),
            !Pool.IsLive() && Pool.GetPicks().IsEmpty() && Pool.GetSlotKeys().IsEmpty());
        Row(TEXT("Switch: off keeps the sources for later"), Pool.GetSourceCount() == 6);
        Row(TEXT("Switch: on again re-picks at once"), Pool.Update(Camera, Two) && Pool.GetPicks().Num() == 2);
        Pool.Reset();
        Row(TEXT("Reset: forgets every column"),
            Pool.GetColumnCount() == 0 && Pool.GetSourceCount() == 0 && Pool.GetSlotKeys().IsEmpty());
    }

    // --- The console switches -------------------------------------------------------------------------------------
    {
        IConsoleVariable* MegaLights =
            IConsoleManager::Get().FindConsoleVariable(TEXT("r.MegaLights.EnableForProject"));
        IConsoleVariable* Lights = IConsoleManager::Get().FindConsoleVariable(TEXT("voxel.Lights"));
        Row(TEXT("Console: r.MegaLights.EnableForProject and voxel.Lights exist"), MegaLights && Lights);
        if (MegaLights && Lights)
        {
            const FString MegaLightsWas = MegaLights->GetString(), LightsWas = Lights->GetString();
            const uint32 MegaLightsSetBy = FPoolTestCVarGuard::GetSetBy(*MegaLights);
            const uint32 LightsSetBy = FPoolTestCVarGuard::GetSetBy(*Lights);
            {
                const FPoolTestCVarGuard MegaLightsGuard(*MegaLights), LightsGuard(*Lights);
                // The rows below only mean something if a set from code takes effect: log what reads back.
                auto ReadBack = [this, MegaLights, Lights](const TCHAR* After)
                {
                    AddInfo(FString::Printf(
                        TEXT("Console: after %s, r.MegaLights.EnableForProject reads %d (set by %s), "
                             "voxel.Lights reads %d"),
                        After, MegaLights->GetInt(),
                        GetConsoleVariableSetByName(EConsoleVariableFlags(FPoolTestCVarGuard::GetSetBy(*MegaLights))),
                        Lights->GetInt()));
                };
                AddInfo(FString::Printf(TEXT("Console: r.MegaLights.EnableForProject was %s (set by %s)"),
                    *MegaLightsWas, GetConsoleVariableSetByName(EConsoleVariableFlags(MegaLightsSetBy))));
                TArray<TObjectPtr<UPointLightComponent>> Components;
                FVoxelLights Pool(nullptr, Components);
                Pool.SetColumn(FIntPoint(0, 0), {MakePoolTestSource(FIntVector(3, 3, 64))});
                const FVector Camera = FVector(3, 3, 66) * 100;
                MegaLights->Set(0, ECVF_SetByCode);
                Lights->Set(1, ECVF_SetByCode);
                ReadBack(TEXT("MegaLights 0, voxel.Lights 1"));
                Row(TEXT("Console: MegaLights 0 and voxel.Lights 1 read back as set"),
                    MegaLights->GetInt() == 0 && Lights->GetInt() == 1);
                Pool.Update(Camera, FVoxelLights::ReadSettings());
                Row(TEXT("Console: MegaLights off, the pool stays empty"),
                    !FVoxelLights::ReadSettings().bLive && !Pool.IsLive() && Pool.GetPicks().IsEmpty());
                MegaLights->Set(1, ECVF_SetByCode);
                ReadBack(TEXT("MegaLights 1"));
                Row(TEXT("Console: MegaLights 1 reads back as set"), MegaLights->GetInt() == 1);
                Row(TEXT("Console: MegaLights on, the pool fills"),
                    FVoxelLights::ReadSettings().bLive && Pool.Update(Camera, FVoxelLights::ReadSettings()) &&
                        Pool.GetPicks().Num() == 1);
                Lights->Set(0, ECVF_SetByCode);
                ReadBack(TEXT("voxel.Lights 0"));
                Row(TEXT("Console: voxel.Lights 0 reads back as set"),
                    Lights->GetInt() == 0 && MegaLights->GetInt() == 1);
                Pool.Update(Camera, FVoxelLights::ReadSettings());
                Row(TEXT("Console: voxel.Lights 0 empties it again"),
                    !FVoxelLights::ReadSettings().bLive && Pool.GetPicks().IsEmpty() && Pool.GetSlotKeys().IsEmpty());
            }
            AddInfo(FString::Printf(TEXT("Console: restored, r.MegaLights.EnableForProject reads %s (set by %s)"),
                *MegaLights->GetString(),
                GetConsoleVariableSetByName(EConsoleVariableFlags(FPoolTestCVarGuard::GetSetBy(*MegaLights)))));
            Row(TEXT("Console: both switches restored, value and set-by priority"),
                MegaLights->GetString() == MegaLightsWas && Lights->GetString() == LightsWas &&
                    FPoolTestCVarGuard::GetSetBy(*MegaLights) == MegaLightsSetBy &&
                    FPoolTestCVarGuard::GetSetBy(*Lights) == LightsSetBy);
        }
    }

    // --- The falloff: voxel.LightFalloff retunes the shown components ---------------------------------------------
    {
        IConsoleVariable* Falloff = IConsoleManager::Get().FindConsoleVariable(TEXT("voxel.LightFalloff"));
        IConsoleVariable* Brightness = IConsoleManager::Get().FindConsoleVariable(TEXT("voxel.LightBrightness"));
        Row(TEXT("Falloff: voxel.LightFalloff and voxel.LightBrightness exist"), Falloff && Brightness);
        if (Falloff && Brightness)
        {
            const FString FalloffWas = Falloff->GetString(), BrightnessWas = Brightness->GetString();
            {
                const FPoolTestCVarGuard FalloffGuard(*Falloff), BrightnessGuard(*Brightness);
                // An owner without a world: the pool makes its components, unregistered, and tunes them as it would
                // registered ones.
                AActor* Owner = NewObject<AActor>(GetTransientPackage(), NAME_None, RF_Transient);
                TArray<TObjectPtr<UPointLightComponent>> Components;
                FVoxelLights Pool(Owner, Components);
                Pool.SetColumn(FIntPoint(0, 0),
                    {MakePoolTestSource(FIntVector(3, 3, 64)), MakePoolTestSource(FIntVector(9, 3, 64))});
                const FVector Camera = FVector(6, 3, 66) * 100;
                // The console's settings, live without MegaLights: the switches are the console block's rows.
                auto ReadLive = []
                {
                    FVoxelLights::FSettings Settings = FVoxelLights::ReadSettings();
                    Settings.bLive = true;
                    Settings.Count = 4;
                    return Settings;
                };
                // Both lights (level 14, weight 1) show this falloff, exponent, units and intensity.
                auto AllTuned = [&Components](bool bInverseSquared, float Exponent, ELightUnits Units, float Intensity)
                {
                    int32 Tuned = 0;
                    for (const TObjectPtr<UPointLightComponent>& Light : Components)
                        Tuned += Light && bool(Light->bUseInverseSquaredFalloff) == bInverseSquared &&
                            Light->LightFalloffExponent == Exponent && Light->IntensityUnits == Units &&
                            FMath::IsNearlyEqual(Light->Intensity, Intensity, .01f);
                    return Components.Num() == 2 && Tuned == 2;
                };
                const float DefaultExponent = GetDefault<UPointLightComponent>()->LightFalloffExponent;
                Falloff->Set(0, ECVF_SetByCode);
                Brightness->Set(500, ECVF_SetByCode);
                const float Candelas = ReadLive().IntensityCd;
                Row(TEXT("Falloff: 0 shows two components, inverse squared in candelas"),
                    Pool.Update(Camera, ReadLive()) &&
                        AllTuned(true, DefaultExponent, ELightUnits::Candelas, Candelas));
                Falloff->Set(8, ECVF_SetByCode);
                Row(TEXT("Falloff: 8 re-picks"), Pool.Update(Camera, ReadLive()) && ReadLive().Falloff == 8);
                Row(TEXT("Falloff: 8 retunes both: no inverse square, exponent 8, unitless at voxel.LightBrightness"),
                    AllTuned(false, 8, ELightUnits::Unitless, 500));
                Brightness->Set(250, ECVF_SetByCode);
                Falloff->Set(2, ECVF_SetByCode);
                Row(TEXT("Falloff: 2 and a new brightness retune both"),
                    Pool.Update(Camera, ReadLive()) && AllTuned(false, 2, ELightUnits::Unitless, 250));
                Falloff->Set(0, ECVF_SetByCode);
                Row(TEXT("Falloff: 0 restores the default: inverse squared, default exponent, candelas"),
                    Pool.Update(Camera, ReadLive()) &&
                        AllTuned(true, DefaultExponent, ELightUnits::Candelas, Candelas));
            }
            Row(TEXT("Falloff: both variables restored"),
                Falloff->GetString() == FalloffWas && Brightness->GetString() == BrightnessWas);
        }
    }

    // --- The pipeline: a torch's column carries its light ----------------------------------------------------------
    {
        const TSharedRef<const FVoxelBlockTable> Table = VoxelBlocks::GetDefault();
        const uint16 Torch = Table->FindState(TEXT("torch"));
        if (!FFoundationBlockRegistry::GetDefault()->GetAssets() || !Torch)
        {
            AddError(TEXT("DA_Blocks is missing: run ImportBlocks.py"));
            return false;
        }
        const TSharedRef<const IVoxelTerrain> Terrain =
            MakeShared<FVoxelHeightTerrain>(FVoxelSeed::Parse(TEXT("Unnamed")).Value, *Table);
        FVoxelColumnPipeline::FSettings Settings;
        Settings.MaxTasksInFlight = 8;
        FVoxelColumnPipeline Pipeline(Table, Terrain, Settings);
        Pipeline.SetLightTable(VoxelLightSources::MakeTable(*FFoundationBlockRegistry::GetDefault(), *Table));
        Pipeline.SetFocus(FVoxelStreamingRings(FIntPoint(0, 0), 2, 1), 0);
        TMap<FIntPoint, TArray<FVoxelLightSource>> Taken;
        auto RunUntilIdle = [&Pipeline, &Taken]
        {
            for (int32 Round = 0; Round < 10000; ++Round)
            {
                Pipeline.Update();
                FVoxelColumnPipeline::FReady Ready;
                while (Pipeline.PopReady(Ready))
                    Taken.Add(Ready.Column, MoveTemp(Ready.Mesh->Lights));
                if (Pipeline.IsIdle()) return true;
                Pipeline.WaitForJobs();
            }
            return false;
        };
        Row(TEXT("Pipeline: the view meshes"), RunUntilIdle() && Taken.Contains(FIntPoint(0, 0)));
        const FVoxelColumn* Column = Pipeline.FindColumn(FIntPoint(0, 0));
        int32 Top = VoxelConstants::MaxZ;
        while (Column && Top > VoxelConstants::MinZ && Column->GetState(8, 8, Top) == 0)
            --Top;
        const FIntVector Cell(8, 8, Top + 1);
        const uint64 Key = VoxelLightSources::MakeKey(Cell);
        auto HasTorch = [&Taken, Key]
        {
            const TArray<FVoxelLightSource>* Lights = Taken.Find(FIntPoint(0, 0));
            return Lights &&
                Lights->ContainsByPredicate(
                    [Key](const FVoxelLightSource& Source) { return Source.Key == Key && Source.Level == 14; });
        };
        Row(TEXT("Pipeline: no light at the cell before the torch"),
            Column && Top < VoxelConstants::MaxZ && !HasTorch());
        Taken.Reset();
        Row(TEXT("Pipeline: the torch is set"), Pipeline.SetCell(Cell, Torch, 1));
        Row(TEXT("Pipeline: its column meshes again"), RunUntilIdle() && Taken.Contains(FIntPoint(0, 0)));
        Row(TEXT("Pipeline: and its mesh carries the torch's light"), HasTorch());
        const TArray<FVoxelLightSource>* Lights = Taken.Find(FIntPoint(0, 0));
        const FVoxelLightSource* Source = Lights
            ? Lights->FindByPredicate([Key](const FVoxelLightSource& Each) { return Each.Key == Key; })
            : nullptr;
        Row(TEXT("Pipeline: at the standing torch's point"),
            Source && Source->Point.Equals(FVector(Cell) + FVector(.5, .5, VoxelLightSources::FloorTorchHeight), 1e-9));
        Taken.Reset();
        Pipeline.SetCell(Cell, 0, 2);
        Row(TEXT("Pipeline: mined, the column's mesh has no light there"),
            RunUntilIdle() && Taken.Contains(FIntPoint(0, 0)) && !HasTorch());
    }

    AddInfo(FString::Printf(TEXT("Unnamed.Voxel.LightPool: %d of %d rows pass"), Passed, Rows));
    return true;
}
#endif
