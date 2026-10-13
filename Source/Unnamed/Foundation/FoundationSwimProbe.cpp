#include "Foundation/FoundationRuntimeProbe.h"
#include "Foundation/FoundationLab.h"
#include "Foundation/FoundationPawn.h"
#include "Foundation/FoundationProfile.h"
#include "HAL/IConsoleManager.h"

// The swimming control pair in the Playground's pool (x 1000-1300, y 400-700, water 0-200 cm, on the floor, no
// walls): foundation.Swim 0 wades as on land, 1 swims. Expected values come from the simulation's own model over
// the steps each case actually ran (Unnamed.Foundation.Swim pins the model against the probe and wiki values).
void UFoundationRuntimeProbe::AddSwimCases()
{
#if WITH_DEV_AUTOMATION_TESTS
    auto SetSwim = [](int32 Value)
    {
        if (IConsoleVariable* Swim = IConsoleManager::Get().FindConsoleVariable(TEXT("foundation.Swim")))
            Swim->Set(Value, ECVF_SetByCode);
    };
    auto InWater = [this]
    {
        const auto& P = *GetDefault<UFoundationProfile>();
        return Pawn->GetLab()->GetFluid(FFoundationSimulation::Bounds(
                   Pawn->Simulation.Feet, Pawn->Simulation.HeightCm, P.WidthCm)) == EFoundationFluid::Water;
    };
    auto StartSteps = MakeShared<int64>(0);
    auto Before = MakeShared<int32>(0);
    auto Speed = [this] { return Pawn->Simulation.LastDisplacement.Size2D() / 5.; };
    AddCase(
        TEXT("swim 0: wade"), .6,
        [this, SetSwim, Before]
        {
            if (const IConsoleVariable* Swim = IConsoleManager::Get().FindConsoleVariable(TEXT("foundation.Swim")))
                *Before = Swim->GetInt();
            SetSwim(0);
            ResetPlayer({1030, 550, 0});
            PressKey(EKeys::W, true);
        },
        [this, Speed, InWater]
        {
            Record(TEXT("swim 0: still in the pool"), InWater(), Pawn->Simulation.Feet.ToCompactString());
            Measure(TEXT("swim 0: wading blocks/s (land walk)"), Speed(), 4.3171806, .0431718);
        });
    AddCase(
        TEXT("swim 1: sink"), 2,
        [this, SetSwim, StartSteps]
        {
            SetSwim(1);
            ResetPlayer({1150, 550, 100});
            *StartSteps = Pawn->Simulation.Steps;
        },
        [this, StartSteps]
        {
            // At rest: Vz(k) = -2.5 (1 - .8^k), dZ(n) = -2.5 (n - (1 - .8^n) / .2) cm.
            const int64 N = Pawn->Simulation.Steps - *StartSteps;
            Measure(FString::Printf(TEXT("swim 1: sank cm in %lld steps"), N), Pawn->Simulation.Feet.Z - 100,
                -2.5 * (N - (1 - FMath::Pow(.8, double(N))) / .2), .05);
        });
    AddCase(
        TEXT("swim 1: walk on the pool floor"), 1.2,
        [this]
        {
            ResetPlayer({1030, 550, 0});
            PressKey(EKeys::W, true);
        },
        [this, Speed, InWater]
        {
            Record(TEXT("swim 1: still in the pool"), InWater(), Pawn->Simulation.Feet.ToCompactString());
            Measure(TEXT("swim 1: swim blocks/s"), Speed(), 1.96, .0197);
        });
    AddCase(
        TEXT("swim 1: hold jump"), .6,
        [this, StartSteps]
        {
            ResetPlayer({1150, 550, 0});
            PressKey(EKeys::SpaceBar, true);
            *StartSteps = Pawn->Simulation.Steps;
        },
        [this, StartSteps, InWater]
        {
            // Held jump: Vz(k) = 13.5 (1 - .8^k) while in water.
            const int64 N = Pawn->Simulation.Steps - *StartSteps;
            Record(TEXT("swim 1: rose, still in water"), InWater() && Pawn->Simulation.Feet.Z > 50,
                Pawn->Simulation.Feet.ToCompactString());
            Measure(FString::Printf(TEXT("swim 1: rise cm/tick after %lld steps"), N),
                Pawn->Simulation.VelocityPerTick.Z, 13.5 * (1 - FMath::Pow(.8, double(N))), .01);
        });
    AddCase(
        TEXT("swim 1: sneak"), .6,
        [this, StartSteps]
        {
            ResetPlayer({1150, 550, 100});
            PressKey(EKeys::C, true);
            *StartSteps = Pawn->Simulation.Steps;
        },
        [this, StartSteps]
        {
            const int64 N = Pawn->Simulation.Steps - *StartSteps;
            const double Idle = -2.5 * (N - (1 - FMath::Pow(.8, double(N))) / .2);
            Record(TEXT("swim 1: sneak sinks faster than idle"), Pawn->Simulation.Feet.Z - 100 < Idle - 50,
                FString::Printf(TEXT("dZ %.2f cm vs idle %.2f in %lld steps"), Pawn->Simulation.Feet.Z - 100, Idle, N));
        });
    AddCase(
        TEXT("swim 1: walk out"), 1.6,
        [this]
        {
            ResetPlayer({1150, 550, 0});
            PressKey(EKeys::W, true);
        },
        [this, Speed, InWater]
        {
            Record(TEXT("swim 1: out of the pool"), !InWater() && Pawn->Simulation.Feet.X > 1330,
                Pawn->Simulation.Feet.ToCompactString());
            Measure(TEXT("swim 1: land walk after leaving blocks/s"), Speed(), 4.3171806, .0431718);
        });
    AddCase(
        TEXT("swim: restore"), .1,
        [this, SetSwim, Before]
        {
            ResetPlayer();
            SetSwim(*Before);
        },
        [] {});
#endif
}
