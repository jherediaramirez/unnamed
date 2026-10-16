#include "Foundation/FoundationRuntimeProbe.h"
#include "Foundation/FoundationAuthority.h"
#include "Foundation/FoundationBreakEffects.h"
#include "Foundation/FoundationController.h"
#include "Foundation/FoundationFootsteps.h"
#include "Foundation/FoundationInteractionView.h"
#include "Foundation/FoundationPawn.h"
#include "Foundation/FoundationPlayground.h"
#include "Foundation/FoundationProfile.h"
#include "HAL/IConsoleManager.h"

// Player footsteps (step 13a2) in the interaction suite: with foundation.Player.Footsteps 1 a 1.5 s walk along a
// stone strip high above the Playground (its lab floor is geometry, not blocks, so it plays no step) counts steps on
// the walked-distance cadence, and each plays a block step event at the sound type's volume x 0.15; switch 0 is the
// control (before 13a2): the same walk counts and plays none.
void UFoundationRuntimeProbe::AddFootstepCases()
{
#if WITH_DEV_AUTOMATION_TESTS
    auto SetSwitch = [](int32 Value)
    {
        if (IConsoleVariable* Switch = IConsoleManager::Get().FindConsoleVariable(TEXT("foundation.Player.Footsteps")))
            Switch->Set(Value, ECVF_SetByCode);
    };
    auto Effects = [this]() -> FFoundationBreakEffects*
    {
        const AFoundationPlayground* Playground = Cast<AFoundationPlayground>(Pawn->GetLab());
        return Playground && Playground->GetView() ? Playground->GetView()->GetBreakEffects() : nullptr;
    };
    auto Previous = MakeShared<int32>(1);
    auto Start = MakeShared<FoundationFootsteps::FState>();
    auto StartCount = MakeShared<int32>(0);
    // A 16-block stone strip along +X at cell height 40 (clear sky), put down by the walks and taken up by the restore.
    constexpr int32 StripLength = 16, StripZ = 40;
    auto Strip = [this](bool bPut)
    {
        FFoundationAuthority* Authority = Pawn->GetAuthority();
        const FFoundationBlockType* Stone = Authority ? Authority->World.FindBlock(TEXT("stone")) : nullptr;
        if (!Stone) return;
        for (int32 X = 0; X < StripLength; ++X)
            Authority->World.Set({X, 0, StripZ}, bPut ? Stone->GetDefaultState() : FFoundationBlockState());
    };
    auto Begin = [this, Effects, Start, StartCount, Strip]
    {
        Strip(true);
        const FFoundationAuthority* Authority = Pawn->GetAuthority();
        const double Cm = UFoundationProfile::BlockCm;
        ResetPlayer((Authority ? Authority->World.OriginCm : FVector::ZeroVector) +
            FVector(.5 * Cm, .5 * Cm, (StripZ + 1) * Cm));
        Controller->SetControlRotation(FRotator(0, 0, 0));
        if (FFoundationBreakEffects* Found = Effects()) Found->ClearSoundLog();
        *Start = Pawn->Footsteps;
        *StartCount = Pawn->FootstepCount;
        PressKey(EKeys::W, true);
    };
    // The block step events logged since Begin: primary (volume above the under-block's) and all.
    auto Logged = [Effects](int32& OutPrimary, FString& OutDetail)
    {
        int32 All = 0;
        OutPrimary = 0;
        if (const FFoundationBreakEffects* Found = Effects())
            for (const FoundationBreak::FSoundLogEntry& Entry : Found->GetSoundLog())
            {
                const FString Name = Entry.Event.ToString();
                if (!Name.StartsWith(TEXT("block.")) || !Name.EndsWith(TEXT(".step"))) continue;
                ++All;
                const bool bPrimary = Entry.Volume > FoundationFootsteps::UnderVolumeScale * 1.5f;
                OutPrimary += bPrimary;
                if (All <= 3)
                    OutDetail += FString::Printf(
                        TEXT(" %s %s vol %.3f pitch %.2f;"), *Name, *Entry.File.ToString(), Entry.Volume, Entry.Pitch);
            }
        return All;
    };

    AddCase(
        TEXT("footsteps 1: walk"), 1.5,
        [this, SetSwitch, Previous, Begin]
        {
            if (const IConsoleVariable* Switch =
                    IConsoleManager::Get().FindConsoleVariable(TEXT("foundation.Player.Footsteps")))
                *Previous = Switch->GetInt();
            SetSwitch(1);
            Begin();
        },
        [this] { PressKey(EKeys::W, false); });
    // Read once the pawn has stopped and the view has played every counted step.
    AddCase(
        TEXT("footsteps 1: settle"), .5, [] {},
        [this, Start, StartCount, Logged]
        {
            const int32 Steps = Pawn->FootstepCount - *StartCount;
            // The cadence's own count: every whole number the walked distance passed.
            const int32 Expected =
                FMath::Max(0, int32(FMath::CeilToDouble(Pawn->Footsteps.Distance) - Start->NextStep));
            int32 Primary = 0;
            FString Detail;
            const int32 All = Logged(Primary, Detail);
            Record(TEXT("footsteps 1: a walk plays block steps on the cadence"),
                Steps >= 2 && Steps == Expected && Primary == Steps,
                FString::Printf(TEXT("walked %.2f (from %.2f), steps %d (cadence %d), logged %d (primary %d):%s"),
                    Pawn->Footsteps.Distance, Start->Distance, Steps, Expected, All, Primary, *Detail));
        });
    AddCase(
        TEXT("footsteps 0: walk"), 1.5,
        [SetSwitch, Begin]
        {
            SetSwitch(0);
            Begin();
        },
        [this] { PressKey(EKeys::W, false); });
    AddCase(
        TEXT("footsteps 0: settle"), .5, [] {},
        [this, StartCount, Logged]
        {
            int32 Primary = 0;
            FString Detail;
            const int32 All = Logged(Primary, Detail);
            Record(TEXT("footsteps 0: the same walk is silent"), All == 0 && Pawn->FootstepCount == *StartCount,
                FString::Printf(TEXT("steps %d, logged %d:%s, feet %s"), Pawn->FootstepCount - *StartCount, All,
                    *Detail, *Pawn->Simulation.Feet.ToCompactString()));
        });
    AddCase(
        TEXT("footsteps: restore"), .2,
        [this, SetSwitch, Previous, Strip]
        {
            SetSwitch(*Previous);
            Strip(false);
            ResetPlayer();
        },
        [] {});
#endif
}
