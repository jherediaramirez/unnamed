#include "Foundation/FoundationRuntimeProbe.h"
#include "Foundation/FoundationBlockTextureTable.h"
#include "Foundation/FoundationController.h"
#include "Foundation/FoundationInteractionView.h"
#include "Foundation/FoundationHud.h"
#include "Foundation/FoundationPlayground.h"
#include "Foundation/FoundationPanels.h"
#include "Foundation/FoundationPawn.h"
#include "Hud/HudCreativeScreen.h"
#include "Foundation/FoundationProfile.h"
#include "Foundation/FoundationBreakAssets.h"
#include "Foundation/FoundationBreakEffects.h"
#include "Foundation/FoundationGrid.h"
#include "Menu/MenuPreferences.h"
#include "Dom/JsonObject.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "GameFramework/WorldSettings.h"
#include "HAL/IConsoleManager.h"
#include "HAL/PlatformMisc.h"
#include "Misc/App.h"
#include "Misc/CommandLine.h"
#include "Misc/FileHelper.h"
#include "Misc/Parse.h"
#include "Misc/Paths.h"
#include "UnrealClient.h"

#if WITH_DEV_AUTOMATION_TESTS
namespace
{
    // -FoundationTourBreak, the break lane's capture pack: the row's blocks, west to east, 8 m apart and 30 m north of
    // spawn (clear of the stations, the galleries and the course); the dandelion stands on a grass block.
    const TCHAR* const TourBreakBlocks[] = {TEXT("stone"), TEXT("grass_block"), TEXT("oak_log"), TEXT("glass"),
        TEXT("oak_leaves"), TEXT("sand"), TEXT("oak_slab"), TEXT("dandelion")};
    constexpr int32 TourBreakDandelion = 7;
    FIntVector GetTourBreakCell(int32 Index)
    {
        return FIntVector(12 + 8 * Index, -30, Index == TourBreakDandelion ? 1 : 0);
    }

    // The foundation.Break.* switches, in FTourBreakConfig's order.
    enum ETourBreakSwitch
    {
        TourOutline,
        TourCracks,
        TourCrackSmooth,
        TourParticles,
        TourCollision,
        TourSpray,
        TourSounds,
        TourSwing,
        TourPop,
        // Not a break switch: the destination preview, so the pack's shots and its restore line say which it shows.
        TourPlacePreview,
        // Nor this: place sounds stay off in the pack's configurations, so its sound log holds breaks and hits only.
        TourPlaceSound,
        TourSwitchCount
    };
    const TCHAR* const TourBreakSwitches[TourSwitchCount] = {TEXT("foundation.Break.Outline"),
        TEXT("foundation.Break.Cracks"), TEXT("foundation.Break.CrackSmooth"), TEXT("foundation.Break.Particles"),
        TEXT("foundation.Break.ParticleCollision"), TEXT("foundation.Break.ParticleSpray"),
        TEXT("foundation.Break.Sounds"), TEXT("foundation.Break.Swing"), TEXT("foundation.Break.Pop"),
        TEXT("foundation.Place.Preview"), TEXT("foundation.Place.Sound")};

    // One configuration: the switches and the menu's "particles" pref (empty keeps the run's own).
    struct FTourBreakConfig
    {
        FString Name;
        int32 Switch[TourSwitchCount] = {};
        FString Particles;
    };

    // Break frames are steered to their ages after the Broken event within this (world seconds).
    constexpr double TourBreakTolerance = .00002;

    FString GetTourCVar(const TCHAR* Name)
    {
        const IConsoleVariable* Variable = IConsoleManager::Get().FindConsoleVariable(Name);
        return Variable ? Variable->GetString() : FString();
    }

    // Names SetTourCVar did not find; the pack's last case records them as a failure.
    TArray<FString> TourCVarMisses;

    void SetTourCVar(const TCHAR* Name, const FString& Value)
    {
        if (IConsoleVariable* Variable = IConsoleManager::Get().FindConsoleVariable(Name))
            Variable->Set(*Value, ECVF_SetByConsole);
        else
            TourCVarMisses.AddUnique(Name);
    }

    void GetTourMeanP99(TArray<double> Samples, double& OutMean, double& OutP99)
    {
        OutMean = OutP99 = 0;
        if (Samples.IsEmpty()) return;
        Samples.Sort();
        for (const double Sample : Samples)
            OutMean += Sample;
        OutMean /= Samples.Num();
        OutP99 = Samples[FMath::Min(Samples.Num() - 1, FMath::FloorToInt(Samples.Num() * .99))];
    }

    // What the pack changes (restored by its last case) and what its cases hand on to each other.
    struct FTourBreakRun
    {
        FString SavedSwitches[TourSwitchCount];
        FString SavedAntiAliasing, SavedScreenPercentage, SavedParticles = TEXT("All");
        bool bSavedHighContrast = false, bSavedHideHud = false, bSavedOverlay = false, bSavedShowCollision = false;
        float SavedDilation = 1, SavedMinDilation = .0001f;
        const UFoundationBreakAssets* Assets = nullptr;
        // The frozen point: global time dilation 0, so no reference step and no effect age moves.
        bool bFrozen = false;
        double FrozenProgress = 0, FrozenTime = 0, FrozenReal = 0;
        int64 FrozenStep = 0;
        // The break being captured: world time and frame delta of the frame that removed the block (0: none).
        double BreakTime = 0, BreakDelta = 0, Boundary = 0;
        // World time and reference step of each frame of a cadence mine.
        TArray<double> FrameTimes;
        TArray<int64> FrameSteps;
        int32 Live = 0, Breaks = 0, Checks = 0, PrefFailures = 0, PlaceFailures = 0;
        bool bOk = true, bOutlineOk = true, bPopAt16 = false, bPopAt150 = true;
        FFoundationBlockState State;
        TArray<FBox> Boxes;
        TArray<FString> Notes, Misses, OutlineNotes, SoundRows;
        TArray<double> ViewMs;
        TArray<TSharedPtr<FJsonValue>> Shots;
    };
}
#endif

// -FoundationSuite=tour (never part of "all"): a screenshot tour of the Playground with the HUD visible, taken with
// the probe's ordinary screenshot request. Each shot is a case that sets the scene, waits for the frame to settle and
// requests the capture; a short flush case lets the file reach the disk. Named <report>-tour-<name>.png.
void UFoundationRuntimeProbe::AddTourCases()
{
#if WITH_DEV_AUTOMATION_TESTS
    auto* Lab = Cast<AFoundationPlayground>(Pawn->GetLab());
    if (!Lab)
    {
        AddCase(
            TEXT("playground present"), .1, [] {},
            [this] { Record(TEXT("playground present"), false, TEXT("the map has no AFoundationPlayground")); });
        return;
    }
    auto Authority = [Lab]() -> FFoundationAuthority& { return *Lab->GetAuthority(); };
    auto Click = [this](FKey Key)
    {
        PressKey(Key, true);
        PressKey(Key, false);
    };
    auto Shot = [this](const TCHAR* Name)
    {
        FScreenshotRequest::RequestScreenshot(
            FPaths::ChangeExtension(Output, TEXT("")) + TEXT("-tour-") + Name + TEXT(".png"), true, false);
    };
    // The camera for an aerial view: the player is held with its eyes at Eye looking at Target (no falling).
    auto Hover = [this](FVector Eye, FVector Target)
    {
        FrameHook = [this, Eye, Target]
        {
            const UFoundationProfile& Profile = *GetDefault<UFoundationProfile>();
            const FVector Feet = Eye - FVector(0, 0, Profile.StandingEyesCm);
            Pawn->Simulation.Reset(Feet, Profile);
            Pawn->Simulation.bGrounded = true;
            Pawn->Vitals.ResetFall();
            Pawn->SetActorLocation(Feet);
            Controller->SetControlRotation((Target - Eye).Rotation());
        };
    };
    auto Look = [this](FVector Point) { Controller->SetControlRotation((Point - Pawn->GetEyePosition()).Rotation()); };
    auto Flush = [this](const TCHAR* Name) { AddCase(FString(TEXT("tour flush ")) + Name, .4, [] {}, [] {}); };
    const FVector Course = GetCourseOrigin();

    // -FoundationTourBreak: the break lane's capture pack (Docs/Breaking.md) instead of the tour. The row of 8 blocks
    // is placed by authority commands; each block is seen alone by the player standing 2.5 blocks south of it, looking
    // at its outline's centre, the same pose for every switch. Mining frames freeze (global time dilation 0: no
    // reference step, no effect age) when the bare hand's progress first reaches 0.05, 0.35, 0.65 and 0.95 (or the
    // last step before the break, when that comes first), one shot per crack setting; outline frames for stone and
    // the slab; break frames at +16, +50, +150 and +400 ms after the Broken event for configurations A (every switch
    // 0), B (the picks, particles Minimal) and C (B with particles All), the world clock steered there through the
    // time dilation (FApp's fixed time step is compiled out of the editor build). Every shot
    // is taken twice: the default anti-aliasing (-aa), then r.AntiAliasingMethod 0 at r.ScreenPercentage 100
    // (-noaa); <report>-tour-brk-<kind>-<block>-<point>-<setting>-<aa|noaa>.png. Then the hit cadence and the sound
    // log (<report>-break-sounds.csv) and the view's cost; the last case restores every cvar and pref it changed.
    if (FParse::Param(FCommandLine::Get(), TEXT("FoundationTourBreak")))
    {
        TSharedRef<FTourBreakRun> Run = MakeShared<FTourBreakRun>();
        using FStep = TFunction<bool()>;
        auto Do = [](TFunction<void()> Action) -> FStep
        {
            return [Action]
            {
                Action();
                return true;
            };
        };
        auto Wait = [](int32 Frames) -> FStep { return [Frames, Count = 0]() mutable { return Count++ >= Frames; }; };
        auto WaitSeconds = [](double Seconds) -> FStep
        {
            return [Seconds, End = 0.]() mutable
            {
                const double Now = FPlatformTime::Seconds();
                if (End == 0) End = Now + Seconds;
                return Now >= End;
            };
        };
        // A case running Steps in order, each from the frame it becomes current until it returns true; it ends the
        // frame the last one does. Cleanup runs if the case times out first.
        auto Script = [this](FString Name, double Timeout, TArray<FStep> Steps, TFunction<void()> Cleanup)
        {
            TSharedRef<TArray<FStep>> Shared = MakeShared<TArray<FStep>>(MoveTemp(Steps));
            TSharedRef<int32> Next = MakeShared<int32>(0);
            AddCase(
                Name, Timeout,
                [this, Shared, Next]
                {
                    FrameHook = [this, Shared, Next]
                    {
                        while (Shared->IsValidIndex(*Next) && (*Shared)[*Next]())
                            ++*Next;
                        if (!Shared->IsValidIndex(*Next)) Cases[Phase].Duration = 0;
                    };
                },
                [this, Name, Shared, Next, Cleanup]
                {
                    if (!Shared->IsValidIndex(*Next)) return;
                    Record(Name + TEXT(" finished"), false,
                        FString::Printf(TEXT("timed out at step %d of %d"), *Next, Shared->Num()));
                    Cleanup();
                });
        };
        auto Effects = [Lab]() -> FFoundationBreakEffects*
        { return Lab->GetView() ? Lab->GetView()->GetBreakEffects() : nullptr; };
        auto Live = [Effects] { return Effects() ? Effects()->GetLiveParticleCount() : -1; };
        auto PopVisible = [Effects] { return Effects() && Effects()->IsPopVisible(); };
        auto SetPrefs = [this, Run](const FString& Particles, bool bHighContrast)
        {
            UGameInstance* Instance = GetWorld() ? GetWorld()->GetGameInstance() : nullptr;
            UMenuPreferences* Preferences = Instance ? Instance->GetSubsystem<UMenuPreferences>() : nullptr;
            bool bApplied = false;
            if (Preferences)
            {
                Preferences->ApplyChanges(
                    FString::Printf(TEXT("{\"settings\":{\"particles\":\"%s\",\"highContrastBlockOutline\":%s}}"),
                        *Particles, bHighContrast ? TEXT("true") : TEXT("false")));
                const TSharedPtr<const FJsonValue> Amount = Preferences->FindPreference(TEXT("particles"));
                const TSharedPtr<const FJsonValue> Contrast =
                    Preferences->FindPreference(TEXT("highContrastBlockOutline"));
                bApplied = Amount && Amount->Type == EJson::String && Amount->AsString() == Particles && Contrast &&
                    Contrast->Type == EJson::Boolean && Contrast->AsBool() == bHighContrast;
            }
            Run->PrefFailures += bApplied ? 0 : 1;
        };
        auto Apply = [Run, SetPrefs](const FTourBreakConfig& Config)
        {
            for (int32 Switch = 0; Switch < TourSwitchCount; ++Switch)
                SetTourCVar(TourBreakSwitches[Switch], FString::FromInt(Config.Switch[Switch]));
            SetPrefs(Config.Particles.IsEmpty() ? Run->SavedParticles : Config.Particles, false);
        };
        auto SetPass = [Run](bool bNoAA)
        {
            SetTourCVar(TEXT("r.AntiAliasingMethod"), bNoAA ? FString(TEXT("0")) : Run->SavedAntiAliasing);
            SetTourCVar(TEXT("r.ScreenPercentage"), bNoAA ? FString(TEXT("100")) : Run->SavedScreenPercentage);
        };
        auto Freeze = [this, Run](bool bFreeze)
        {
            AWorldSettings* Settings = GetWorld()->GetWorldSettings();
            Settings->MinGlobalTimeDilation = 0;
            Settings->SetTimeDilation(bFreeze ? 0.f : Run->SavedDilation);
            Run->bFrozen = bFreeze;
        };
        // The next frame's world delta, through the time dilation (this frame's real delta predicts the next one).
        auto SetNextDelta = [this, Run](double Seconds)
        {
            AWorldSettings* Settings = GetWorld()->GetWorldSettings();
            Settings->MinGlobalTimeDilation = 0;
            Settings->SetTimeDilation(float(FMath::Min(1., Seconds / FMath::Max(FApp::GetDeltaTime(), .001))));
            Run->bFrozen = false;
        };
        // Brings the world clock to Target (world seconds) from below: half the remaining time per frame (at most
        // 8 ms), so a real frame up to twice as long as predicted still stops short; true once within 0.02 ms, frozen.
        auto Steer = [this, Freeze, SetNextDelta](double Target)
        {
            const double Remaining = Target - GetWorld()->GetTimeSeconds();
            if (Remaining <= TourBreakTolerance)
            {
                Freeze(true);
                return true;
            }
            SetNextDelta(FMath::Min(Remaining * .5, .008));
            return false;
        };
        auto Cleanup = [this, Freeze, SetPass]
        {
            Freeze(false);
            SetPass(false);
            PressKey(EKeys::LeftMouseButton, false);
        };
        auto HasBlock = [Authority](const FIntVector& Cell, const TCHAR* Item)
        {
            const FFoundationAuthority& A = Authority();
            const FFoundationBlockState State = A.World.Get(Cell).State;
            return !State.IsAir() && A.World.GetBlock(State).Id == FName(Item);
        };
        // A builder takes Item from the creative inventory and places it from above onto the cell below Cell (the
        // staircase's commands): bottom slab, upright log, the dandelion on its grass block.
        auto Place = [this, Authority](const FIntVector& Cell, const TCHAR* Item)
        {
            FFoundationAuthority& A = Authority();
            FFoundationVitals Builder;
            const int32 Id = A.AddPlayer();
            FFoundationPlayerFacts Facts;
            Facts.EyeCm = FVector(Cell.X * 100 + 50, Cell.Y * 100 + 50, Cell.Z * 100 + 120);
            Facts.FeetCm = Facts.EyeCm + FVector(0, 0, 5000);
            Facts.Aim = FVector(0, 0, -1);
            Facts.Step = Pawn->Simulation.Steps * 10000 + 1;
            A.SetFacts(Id, Facts);
            FFoundationCommand Slot;
            Slot.SessionId = A.GetSessionId();
            Slot.Sequence = 1;
            Slot.Type = EFoundationCommandType::SetCreativeSlot;
            Slot.Slot = A.FindPlayer(Id)->Inventory.Selected;
            Slot.CreativeItem = Item;
            Slot.CreativeCount = 1;
            A.Execute(Id, Slot, Builder);
            FFoundationCommand Command;
            Command.SessionId = A.GetSessionId();
            Command.Sequence = 2;
            Command.Type = EFoundationCommandType::UseOn;
            Command.Item = A.FindPlayer(Id)->Inventory.GetHand(EFoundationHand::Main).GetRef();
            Command.Target = A.Resolve(Facts);
            FIntVector Destination;
            EFoundationReason Ignored;
            A.GetDestination(Command.Target, Destination, Ignored);
            Command.Destination = Destination;
            Command.DestinationRevision = A.World.Get(Destination).Revision;
            const bool bPlaced = A.Execute(Id, Command, Builder).bPlaced;
            A.RemovePlayer(Id);
            return bPlaced;
        };
        auto Ensure = [Place, HasBlock](int32 Index)
        {
            const FIntVector Cell = GetTourBreakCell(Index);
            const FIntVector Below = Cell - FIntVector(0, 0, 1);
            if (Index == TourBreakDandelion && !HasBlock(Below, TEXT("grass_block"))) Place(Below, TEXT("grass_block"));
            if (!HasBlock(Cell, TourBreakBlocks[Index])) Place(Cell, TourBreakBlocks[Index]);
            return HasBlock(Cell, TourBreakBlocks[Index]);
        };
        auto Pose = [this, Authority, Click](int32 Index)
        {
            const FFoundationAuthority& A = Authority();
            const FIntVector Cell = GetTourBreakCell(Index);
            TArray<FBox> Boxes;
            FBox Union(ForceInit);
            if (A.World.GetOutlineBoxes(Cell, A.World.Get(Cell).State, Boxes))
                for (const FBox& Box : Boxes)
                    Union += Box;
            const FVector Target =
                Union.IsValid ? Union.GetCenter() : FoundationGrid::CellBox(Cell, A.World.OriginCm).GetCenter();
            ResetPlayer(FVector(Cell.X * 100 + 50, Cell.Y * 100 + 50 + 250, 0));
            Controller->SetControlRotation((Target - Pawn->GetEyePosition()).Rotation());
            // An empty hand: bare-hand mining and no placement preview.
            Click(EKeys::Four);
        };
        // The local player is mining Cell and its progress has reached Target, or its next step breaks the block.
        auto Reached = [this, Authority](const FIntVector& Cell, double Target)
        {
            const FFoundationInteraction& Mining = Pawn->Interaction;
            if (!Mining.bMining || Mining.MiningCell != Cell) return false;
            const FFoundationAuthority& A = Authority();
            const FFoundationPlayer* Player = A.FindPlayer(Mining.PlayerId);
            const double Delta = Player ? A.GetMiningDelta(*Player, A.World.GetBlock(A.World.Get(Cell).State)) : 0;
            return Mining.MiningProgress >= Target - 1e-9 ||
                Mining.MiningProgress + Delta >= 1 - A.Rules.CompletionTolerance;
        };
        auto Capture = [this, Shot, Run](FString Name) -> FStep
        {
            return [this, Shot, Run, Name]
            {
                Shot(*Name);
                const FFoundationInteraction& Mining = Pawn->Interaction;
                auto Entry = MakeShared<FJsonObject>();
                Entry->SetStringField(TEXT("name"), Name);
                Entry->SetNumberField(TEXT("progress"), Mining.bMining ? Mining.MiningProgress : -1);
                Entry->SetNumberField(TEXT("step"), double(Mining.Step));
                Entry->SetNumberField(TEXT("world_seconds"), GetWorld()->GetTimeSeconds());
                if (Run->BreakTime > 0)
                    Entry->SetNumberField(
                        TEXT("age_ms"), (GetWorld()->GetTimeSeconds() - Run->BreakTime + Run->BreakDelta) * 1000);
                Entry->SetBoolField(TEXT("frozen"), Run->bFrozen);
                Entry->SetStringField(TEXT("anti_aliasing"), GetTourCVar(TEXT("r.AntiAliasingMethod")));
                Run->Shots.Add(MakeShared<FJsonValueObject>(Entry));
                return true;
            };
        };
        // One shot per setting in both passes; the frame stays frozen throughout. Check runs just before each shot.
        struct FSetting
        {
            FString Label;
            TFunction<void()> Apply;
            TFunction<void(const FString&)> Check;
        };
        auto AddShots = [Do, Wait, Capture, SetPass](
                            TArray<FStep>& Steps, const FString& Prefix, const TArray<FSetting>& Settings)
        {
            for (const bool bNoAA : {false, true})
            {
                Steps.Add(Do([SetPass, bNoAA] { SetPass(bNoAA); }));
                Steps.Add(Wait(4));
                for (const FSetting& Setting : Settings)
                {
                    const FString Name = Prefix + (Setting.Label.IsEmpty() ? FString() : TEXT("-") + Setting.Label) +
                        (bNoAA ? TEXT("-noaa") : TEXT("-aa"));
                    if (Setting.Apply) Steps.Add(Do(Setting.Apply));
                    // The temporal pass converges on the still frame.
                    Steps.Add(Wait(bNoAA ? 3 : 16));
                    if (Setting.Check) Steps.Add(Do([Check = Setting.Check, Name] { Check(Name); }));
                    Steps.Add(Capture(Name));
                    Steps.Add(Wait(2));
                }
            }
            Steps.Add(Do([SetPass] { SetPass(false); }));
        };

        FTourBreakConfig MiningBase{TEXT("mining")};
        MiningBase.Switch[TourOutline] = 2;
        const FTourBreakConfig ConfigA{TEXT("A")};
        FTourBreakConfig ConfigB{TEXT("B")};
        ConfigB.Switch[TourParticles] = ConfigB.Switch[TourCollision] = ConfigB.Switch[TourSpray] =
            ConfigB.Switch[TourPop] = 1;
        ConfigB.Switch[TourSounds] = ConfigB.Switch[TourSwing] = 2;
        ConfigB.Particles = TEXT("Minimal");
        FTourBreakConfig ConfigC = ConfigB;
        ConfigC.Name = TEXT("C");
        ConfigC.Particles = TEXT("All");

        // Setup: save what the pack changes, tick after the view (a shot and a check see this frame's effects), place
        // the row, load the break assets and warm both passes' shaders.
        {
            TArray<FStep> Steps;
            Steps.Add(Do(
                [this, Lab, Run, Ensure, Apply, Pose, MiningBase]
                {
                    for (int32 Switch = 0; Switch < TourSwitchCount; ++Switch)
                        Run->SavedSwitches[Switch] = GetTourCVar(TourBreakSwitches[Switch]);
                    Run->SavedAntiAliasing = GetTourCVar(TEXT("r.AntiAliasingMethod"));
                    Run->SavedScreenPercentage = GetTourCVar(TEXT("r.ScreenPercentage"));
                    UGameInstance* Instance = GetWorld()->GetGameInstance();
                    if (const UMenuPreferences* Preferences =
                            Instance ? Instance->GetSubsystem<UMenuPreferences>() : nullptr)
                    {
                        const TSharedPtr<const FJsonValue> Amount = Preferences->FindPreference(TEXT("particles"));
                        if (Amount && Amount->Type == EJson::String) Run->SavedParticles = Amount->AsString();
                        const TSharedPtr<const FJsonValue> Contrast =
                            Preferences->FindPreference(TEXT("highContrastBlockOutline"));
                        Run->bSavedHighContrast = Contrast && Contrast->Type == EJson::Boolean && Contrast->AsBool();
                    }
                    const AWorldSettings* Settings = GetWorld()->GetWorldSettings();
                    Run->SavedDilation = Settings->TimeDilation;
                    Run->SavedMinDilation = Settings->MinGlobalTimeDilation;
                    if (Lab->GetView()) AddTickPrerequisiteComponent(Lab->GetView());
                    Run->Assets = LoadObject<UFoundationBreakAssets>(
                        nullptr, TEXT("/Game/Breaking/DA_BreakAssets.DA_BreakAssets"), {}, LOAD_NoWarn | LOAD_Quiet);
                    TourCVarMisses.Reset();
                    Run->bSavedHideHud = Controller->GetHud()->bHideHud;
                    Run->bSavedOverlay = Controller->GetHud()->bOverlay;
                    Run->bSavedShowCollision = Controller->GetHud()->bShowCollision;
                    Controller->GetHud()->bHideHud = Controller->GetHud()->bOverlay =
                        Controller->GetHud()->bShowCollision = false;
                    int32 Placed = 0;
                    for (int32 Index = 0; Index < int32(UE_ARRAY_COUNT(TourBreakBlocks)); ++Index)
                        Placed += Ensure(Index) ? 1 : 0;
                    Record(TEXT("break scene placed"), Placed == int32(UE_ARRAY_COUNT(TourBreakBlocks)),
                        FString::Printf(TEXT("%d of 8 blocks placed by authority commands, cells (12..68 step 8, -30, "
                                             "0), the dandelion at z 1 on grass"),
                            Placed));
                    // Outline 2 turns a switch on: the effects load /Game/Breaking.
                    Apply(MiningBase);
                    Pose(0);
                }));
            Steps.Add(WaitSeconds(3.));
            Steps.Add(Do([SetPass] { SetPass(true); }));
            Steps.Add(WaitSeconds(1.));
            Steps.Add(Do([SetPass] { SetPass(false); }));
            // The steering the break frames use, on a 25 ms target.
            Steps.Add(Do([this, Run] { Run->Boundary = GetWorld()->GetTimeSeconds() + .025; }));
            Steps.Add([Run, Steer] { return Steer(Run->Boundary); });
            Steps.Add(Wait(3));
            Steps.Add(Do(
                [this, Run, Freeze]
                {
                    const double Short = (Run->Boundary - GetWorld()->GetTimeSeconds()) * 1000;
                    Record(TEXT("break time steering"), Short >= 0 && Short < .05,
                        FString::Printf(TEXT("stopped %.4f ms short of a 25 ms target, held 3 frames"), Short));
                    Freeze(false);
                }));
            Steps.Add(WaitSeconds(1.));
            Steps.Add(Do(
                [this, Run, Effects]
                {
                    Record(TEXT("break effects ready"), Effects() && Effects()->HasAssets() && Run->Assets,
                        FString::Printf(TEXT("effects %s, assets %s, DA_BreakAssets %s; anti-aliasing %s, screen "
                                             "percentage %s; particles pref %s, high contrast %s"),
                            Effects() ? TEXT("present") : TEXT("missing"),
                            Effects() && Effects()->HasAssets() ? TEXT("loaded") : TEXT("missing"),
                            Run->Assets ? TEXT("loaded") : TEXT("missing"), *Run->SavedAntiAliasing,
                            *Run->SavedScreenPercentage, *Run->SavedParticles,
                            Run->bSavedHighContrast ? TEXT("on") : TEXT("off")));
                }));
            Script(TEXT("tour break setup"), 30, MoveTemp(Steps), Cleanup);
        }

        // Mining frames, and the outline frames for stone and the slab at 0.35 (with Cracks 2).
        const double Points[] = {.05, .35, .65, .95};
        const TCHAR* const PointNames[] = {TEXT("p05"), TEXT("p35"), TEXT("p65"), TEXT("p95")};
        for (int32 Index = 0; Index < TourBreakDandelion; ++Index)
        {
            const FString Block = TourBreakBlocks[Index];
            const FIntVector Cell = GetTourBreakCell(Index);
            TArray<FStep> Steps;
            Steps.Add(Do(
                [Run, Apply, Ensure, Pose, Index, MiningBase]
                {
                    Apply(MiningBase);
                    Run->PlaceFailures += Ensure(Index) ? 0 : 1;
                    Pose(Index);
                    Run->Notes.Reset();
                    Run->Misses.Reset();
                    Run->OutlineNotes.Reset();
                    Run->Checks = 0;
                    Run->bOk = Run->bOutlineOk = true;
                }));
            Steps.Add(Wait(30));
            Steps.Add(Do([this] { PressKey(EKeys::LeftMouseButton, true); }));
            for (int32 Point = 0; Point < int32(UE_ARRAY_COUNT(Points)); ++Point)
            {
                const double Target = Points[Point];
                Steps.Add(
                    [this, Run, Reached, Freeze, Cell, Target]
                    {
                        if (!Reached(Cell, Target)) return false;
                        Freeze(true);
                        Run->FrozenProgress = Pawn->Interaction.MiningProgress;
                        Run->FrozenStep = Pawn->Interaction.Step;
                        Run->FrozenTime = GetWorld()->GetTimeSeconds();
                        Run->FrozenReal = FPlatformTime::Seconds();
                        return true;
                    });
                TArray<FSetting> Cracks;
                for (const auto& [Label, Value, bSmooth] :
                    {MakeTuple(TEXT("cracks0"), 0, false), MakeTuple(TEXT("cracks1"), 1, false),
                        MakeTuple(TEXT("cracks2"), 2, false), MakeTuple(TEXT("cracks2s"), 2, true)})
                {
                    const int32 Crack = Value;
                    const bool bCrackSmooth = bSmooth;
                    Cracks.Add({Label,
                        [Crack, bCrackSmooth]
                        {
                            SetTourCVar(TourBreakSwitches[TourCracks], FString::FromInt(Crack));
                            SetTourCVar(TourBreakSwitches[TourCrackSmooth], bCrackSmooth ? TEXT("1") : TEXT("0"));
                        },
                        [Run, Effects, Crack](const FString& Name)
                        {
                            const bool bVisible = Effects() && Effects()->IsCrackVisible();
                            ++Run->Checks;
                            if (bVisible != (Crack != 0))
                                Run->Misses.Add(Name + (bVisible ? TEXT(" visible") : TEXT(" hidden")));
                        }});
                }
                AddShots(Steps, FString::Printf(TEXT("brk-mine-%s-%s"), *Block, PointNames[Point]), Cracks);
                if ((Index == 0 || Index == 6) && Point == 1)
                {
                    TArray<FSetting> Outlines;
                    for (const auto& [Label, Value, bContrast] : {MakeTuple(TEXT("outline0"), 0, false),
                             MakeTuple(TEXT("outline1"), 1, false), MakeTuple(TEXT("outline2"), 2, false),
                             MakeTuple(TEXT("outline2hc"), 2, true), MakeTuple(TEXT("outline3"), 3, false)})
                    {
                        const int32 Outline = Value;
                        const bool bHighContrast = bContrast;
                        Outlines.Add({Label,
                            [Run, SetPrefs, Outline, bHighContrast]
                            {
                                SetTourCVar(TourBreakSwitches[TourOutline], FString::FromInt(Outline));
                                SetTourCVar(TourBreakSwitches[TourCracks], TEXT("2"));
                                SetTourCVar(TourBreakSwitches[TourCrackSmooth], TEXT("0"));
                                SetPrefs(Run->SavedParticles, bHighContrast);
                            },
                            [Run, Effects, Outline, bHighContrast](const FString& Name)
                            {
                                const bool bVisible = Effects() && Effects()->IsOutlineVisible();
                                // Outline 0 is the view's own lines: reported, not checked.
                                if (Outline != 0) Run->bOutlineOk &= bVisible == (Outline != 3);
                                // A drawn outline (1-2) used the high-contrast look exactly for outline2hc.
                                if (Outline == 1 || Outline == 2)
                                    Run->bOutlineOk &= Effects() && Effects()->IsHighContrastOutline() == bHighContrast;
                                Run->OutlineNotes.Add(
                                    FString::Printf(TEXT("%s %s"), *Name, bVisible ? TEXT("visible") : TEXT("hidden")));
                            }});
                    }
                    AddShots(Steps, FString::Printf(TEXT("brk-outline-%s-p35"), *Block), Outlines);
                    Steps.Add(Do(
                        [Run, SetPrefs]
                        {
                            SetTourCVar(TourBreakSwitches[TourOutline], TEXT("2"));
                            SetPrefs(Run->SavedParticles, false);
                        }));
                }
                const FString PointName = PointNames[Point];
                Steps.Add(Do(
                    [this, Run, Freeze, PointName]
                    {
                        const FFoundationInteraction& Mining = Pawn->Interaction;
                        const bool bHeld = Mining.MiningProgress == Run->FrozenProgress &&
                            Mining.Step == Run->FrozenStep && GetWorld()->GetTimeSeconds() == Run->FrozenTime;
                        Run->bOk &= bHeld;
                        Run->Notes.Add(FString::Printf(TEXT("%s: progress %.4f at step %lld, then %.4f at step %lld "
                                                            "after %.1f s frozen"),
                            *PointName, Run->FrozenProgress, Run->FrozenStep, Mining.MiningProgress, Mining.Step,
                            FPlatformTime::Seconds() - Run->FrozenReal));
                        Freeze(false);
                    }));
            }
            const bool bOutlines = Index == 0 || Index == 6;
            Steps.Add(Do(
                [this, Run, Block, bOutlines]
                {
                    PressKey(EKeys::LeftMouseButton, false);
                    Record(TEXT("break mining frozen: ") + Block, Run->bOk && Run->Notes.Num() == 4,
                        FString::Join(Run->Notes, TEXT("; ")));
                    Record(TEXT("break crack visible exactly when Cracks != 0: ") + Block,
                        Run->Misses.IsEmpty() && Run->Checks == 32,
                        FString::Printf(TEXT("%d shots checked%s%s"), Run->Checks,
                            Run->Misses.IsEmpty() ? TEXT("") : TEXT("; wrong: "),
                            *FString::Join(Run->Misses, TEXT(", "))));
                    if (bOutlines)
                        Record(TEXT("break outline visible with Outline 1-2, not 3: ") + Block,
                            Run->bOutlineOk && Run->OutlineNotes.Num() == 10,
                            FString::Join(Run->OutlineNotes, TEXT(", ")));
                }));
            Script(TEXT("tour break mining ") + Block, 150, MoveTemp(Steps), Cleanup);
        }

        // Break frames: A, B and C for stone, grass, leaves and the dandelion (a click: it breaks at once).
        auto CheckBreakSound = [this, Run, Effects, Authority](const FString& Label)
        {
            const FFoundationBreakEffects* Breaking = Effects();
            const FName Id = Authority().World.GetBlock(Run->State).Id;
            const FFoundationBreakBlock* Block = Run->Assets
                ? Run->Assets->Blocks.FindByPredicate([Id](const FFoundationBreakBlock& Each) { return Each.Id == Id; })
                : nullptr;
            const FFoundationBreakSoundEvent* Event = Block && Run->Assets->Events.IsValidIndex(Block->BreakEvent)
                ? &Run->Assets->Events[Block->BreakEvent]
                : nullptr;
            bool bOk = Breaking && Event;
            int32 Plays = 0;
            FString Detail;
            if (Breaking)
                for (const FoundationBreak::FSoundLogEntry& Entry : Breaking->GetSoundLog())
                {
                    if (Entry.bHit) continue;
                    ++Plays;
                    const FFoundationBreakSoundFile* File = Event
                        ? Event->Files.FindByPredicate(
                              [&Entry](const FFoundationBreakSoundFile& Each) { return Each.Name == Entry.File; })
                        : nullptr;
                    const double Expected = File ? .8 * Block->Pitch * File->Pitch : -1;
                    const FString Name = Entry.Event.ToString();
                    bOk &= File && Entry.Event == Event->Name && Name.StartsWith(TEXT("block.")) &&
                        Name.EndsWith(TEXT(".break")) && FMath::Abs(Entry.Pitch - Expected) < 1e-4;
                    Detail +=
                        FString::Printf(TEXT("%s %s pitch %.4f (0.8 x type %.3f x file %.3f = %.4f) volume %.3f; "),
                            *Name, *Entry.File.ToString(), Entry.Pitch, Block ? Block->Pitch : 0.f,
                            File ? File->Pitch : 0.f, Expected, Entry.Volume);
                }
            Record(TEXT("break sound: ") + Label, bOk && Plays == 1,
                FString::Printf(TEXT("%d break plays (expected 1, the event of %s: %s): %s"), Plays, *Id.ToString(),
                    Event ? *Event->Name.ToString() : TEXT("none"), *Detail));
        };
        auto LogSounds = [Run, Effects](const FString& Phase)
        {
            if (const FFoundationBreakEffects* Breaking = Effects())
                for (const FoundationBreak::FSoundLogEntry& Entry : Breaking->GetSoundLog())
                    Run->SoundRows.Add(
                        FString::Printf(TEXT("%s,%.4f,%s,%s,%.4f,%.4f,%d"), *Phase, Entry.Time, *Entry.Event.ToString(),
                            *Entry.File.ToString(), Entry.Volume, Entry.Pitch, Entry.bHit ? 1 : 0));
        };
        const int32 Ages[] = {16, 50, 150, 400};
        for (const int32 Index : {0, 1, 4, TourBreakDandelion})
        {
            const FString Block = TourBreakBlocks[Index];
            const FIntVector Cell = GetTourBreakCell(Index);
            const bool bInstant = Index == TourBreakDandelion;
            TArray<FStep> Steps;
            for (const FTourBreakConfig& Config : {ConfigA, ConfigB, ConfigC})
            {
                const FString Label = Block + TEXT(" ") + Config.Name;
                Steps.Add(Do(
                    [Run, Apply, Ensure, Pose, Index, Config]
                    {
                        Apply(Config);
                        Run->PlaceFailures += Ensure(Index) ? 0 : 1;
                        Pose(Index);
                    }));
                // The last configuration's particles, pop and the view's cubes are gone.
                Steps.Add(WaitSeconds(1.5));
                Steps.Add([Live, PopVisible] { return Live() <= 0 && !PopVisible(); });
                Steps.Add(Do(
                    [this, Run, Effects, Live, Authority, Click, Cell, bInstant]
                    {
                        if (FFoundationBreakEffects* Breaking = Effects()) Breaking->ClearSoundLog();
                        const FFoundationAuthority& A = Authority();
                        Run->State = A.World.Get(Cell).State;
                        Run->Boxes.Reset();
                        if (!A.World.GetOutlineBoxes(Cell, Run->State, Run->Boxes))
                            Run->Boxes.Add(FoundationGrid::CellBox(Cell, A.World.OriginCm));
                        Run->Live = Live();
                        Run->BreakTime = 0;
                        Run->bPopAt16 = false;
                        Run->bPopAt150 = true;
                        Run->bOk = true;
                        Run->Notes.Reset();
                        if (bInstant)
                        {
                            // The click acts on the next reference step.
                            Click(EKeys::LeftMouseButton);
                            Run->Boundary = GetWorld()->GetTimeSeconds() +
                                GetDefault<UFoundationProfile>()->TickSeconds - Pawn->Simulation.Accumulator;
                        }
                        else
                            PressKey(EKeys::LeftMouseButton, true);
                    }));
                // Mined at full speed until the next reference step breaks the block.
                if (!bInstant)
                    Steps.Add(
                        [this, Run, Reached, Cell]
                        {
                            if (!Reached(Cell, 2.)) return false;
                            Run->Boundary = GetWorld()->GetTimeSeconds() +
                                GetDefault<UFoundationProfile>()->TickSeconds - Pawn->Simulation.Accumulator;
                            return true;
                        });
                // Stopped 0.3 ms short of that step, the live particles are counted; a ~1 ms frame then crosses it, so
                // no older particle (a hit chip) dies in the break frame and the ages start near 0.
                Steps.Add([Run, Steer] { return Steer(Run->Boundary - .0003); });
                Steps.Add(Do(
                    [Run, Live, SetNextDelta]
                    {
                        Run->Live = Live();
                        SetNextDelta(.001);
                    }));
                Steps.Add(
                    [this, Run, Live, Effects, Authority, Cell, bInstant, Config, Label]
                    {
                        if (!Authority().World.Get(Cell).State.IsAir())
                        {
                            Run->Live = Live();
                            return false;
                        }
                        // This frame's view handled the Broken event: its effects have aged this frame's delta.
                        Run->BreakTime = GetWorld()->GetTimeSeconds();
                        Run->BreakDelta = GetWorld()->GetDeltaSeconds();
                        if (!bInstant) PressKey(EKeys::LeftMouseButton, false);
                        if (Config.Switch[TourParticles] == 1)
                        {
                            const int32 PerBox = FoundationBreak::BreakParticlesPerBox(
                                FoundationBreak::ParticleAmount(Config.Particles));
                            // The effects' own count of the break's spawns: a hit chip on the break frame does not
                            // move it, as it moved the live-count delta.
                            const int32 Spawned = Effects() ? Effects()->GetLastBreakSpawnCount() : -1;
                            Record(TEXT("break particles: ") + Label, Spawned == PerBox * Run->Boxes.Num(),
                                FString::Printf(TEXT("%d spawned by the break; expected %d per box (%s) x %d boxes = "
                                                     "%d; %d live the frame before; break frame %.2f ms"),
                                    Spawned, PerBox, *Config.Particles, Run->Boxes.Num(), PerBox * Run->Boxes.Num(),
                                    Run->Live, Run->BreakDelta * 1000));
                        }
                        return true;
                    });
                for (const int32 Age : Ages)
                {
                    Steps.Add(
                        [this, Run, Steer, Live, PopVisible, Age]
                        {
                            // The effects were born at the start of the break frame.
                            if (!Steer(Run->BreakTime - Run->BreakDelta + Age / 1000.)) return false;
                            const double AgeMs =
                                (GetWorld()->GetTimeSeconds() - Run->BreakTime + Run->BreakDelta) * 1000;
                            Run->FrozenTime = GetWorld()->GetTimeSeconds();
                            const bool bPop = PopVisible();
                            if (Age == 16) Run->bPopAt16 = bPop;
                            if (Age == 150) Run->bPopAt150 = bPop;
                            Run->bOk &= FMath::Abs(AgeMs - Age) < .1;
                            Run->Notes.Add(FString::Printf(TEXT("+%d ms: age %.2f ms, %d live, pop %s"), Age, AgeMs,
                                Live(), bPop ? TEXT("visible") : TEXT("gone")));
                            return true;
                        });
                    AddShots(Steps, FString::Printf(TEXT("brk-break-%s-%s-%03dms"), *Block, *Config.Name, Age),
                        {FSetting{}});
                    Steps.Add(Do(
                        [this, Run, Freeze]
                        {
                            Run->bOk &= GetWorld()->GetTimeSeconds() == Run->FrozenTime;
                            Freeze(false);
                        }));
                }
                Steps.Add(Do(
                    [this, Run, CheckBreakSound, LogSounds, Config, Label]
                    {
                        Run->BreakTime = 0;
                        Record(TEXT("break ages frozen: ") + Label, Run->bOk && Run->Notes.Num() == 4,
                            FString::Join(Run->Notes, TEXT("; ")));
                        if (Config.Switch[TourPop])
                            Record(TEXT("break pop visible at +16 ms, gone by +150 ms: ") + Label,
                                Run->bPopAt16 && !Run->bPopAt150, FString::Join(Run->Notes, TEXT("; ")));
                        if (Config.Switch[TourSounds])
                        {
                            CheckBreakSound(Label);
                            LogSounds(TEXT("break-") + Label.Replace(TEXT(" "), TEXT("-")));
                        }
                    }));
            }
            Script(TEXT("tour break frames ") + Block, 150, MoveTemp(Steps), Cleanup);
        }

        // The hit cadence with B on stone: Swing 2 (each swing's contact tick, 6 steps apart), then Swing 1 (vanilla's
        // every 4 steps); 2 s each at the run's frame rate. Then the sound log.
        {
            TArray<FStep> Steps;
            for (const int32 Swing : {2, 1})
            {
                FTourBreakConfig Config = ConfigB;
                Config.Switch[TourSwing] = Swing;
                Steps.Add(Do(
                    [Run, Apply, Ensure, Pose, Config]
                    {
                        Apply(Config);
                        Run->PlaceFailures += Ensure(0) ? 0 : 1;
                        Pose(0);
                    }));
                Steps.Add(WaitSeconds(1.));
                Steps.Add(Do(
                    [this, Run, Effects]
                    {
                        if (FFoundationBreakEffects* Breaking = Effects()) Breaking->ClearSoundLog();
                        Run->FrameTimes.Reset();
                        Run->FrameSteps.Reset();
                        PressKey(EKeys::LeftMouseButton, true);
                    }));
                // 2 s, noting each frame's world time and reference step: a hit's log time names its frame, so its
                // step.
                Steps.Add(
                    [this, Run, End = 0.]() mutable
                    {
                        const double Now = FPlatformTime::Seconds();
                        if (End == 0) End = Now + 2;
                        Run->FrameTimes.Add(GetWorld()->GetTimeSeconds());
                        Run->FrameSteps.Add(Pawn->Interaction.Step);
                        return Now >= End;
                    });
                Steps.Add(Do(
                    [this, Run, Effects, LogSounds, Swing]
                    {
                        PressKey(EKeys::LeftMouseButton, false);
                        const int32 Expected = Swing == 2 ? FoundationBreak::SwingSteps : FoundationBreak::HitSteps;
                        TArray<int64> HitSteps;
                        TArray<FString> Notes;
                        bool bOk = true;
                        if (const FFoundationBreakEffects* Breaking = Effects())
                            for (const FoundationBreak::FSoundLogEntry& Entry : Breaking->GetSoundLog())
                            {
                                if (!Entry.bHit) continue;
                                const int32 Frame = Run->FrameTimes.IndexOfByKey(Entry.Time);
                                // A frame that ran more than one step leaves the hit's step ambiguous.
                                const bool bKnown =
                                    Frame > 0 && Run->FrameSteps[Frame] - Run->FrameSteps[Frame - 1] == 1;
                                bOk &= bKnown;
                                HitSteps.Add(Frame != INDEX_NONE ? Run->FrameSteps[Frame] : -1);
                                Notes.Add(
                                    FString::Printf(TEXT("%lld%s"), HitSteps.Last(), bKnown ? TEXT("") : TEXT("?")));
                            }
                        bOk &= HitSteps.Num() >= 3;
                        TArray<FString> Gaps;
                        for (int32 Hit = 1; Hit < HitSteps.Num(); ++Hit)
                        {
                            bOk &= HitSteps[Hit] - HitSteps[Hit - 1] == Expected;
                            Gaps.Add(FString::Printf(TEXT("%lld"), HitSteps[Hit] - HitSteps[Hit - 1]));
                        }
                        Record(FString::Printf(TEXT("break hit steps %d apart with Swing %d"), Expected, Swing), bOk,
                            FString::Printf(TEXT("%d hits in 2 s at reference steps %s (press at step %lld); gaps %s"),
                                HitSteps.Num(), *FString::Join(Notes, TEXT(", ")),
                                Run->FrameSteps.IsEmpty() ? -1ll : Run->FrameSteps[0],
                                *FString::Join(Gaps, TEXT(", "))));
                        LogSounds(FString::Printf(TEXT("mine-stone-swing%d"), Swing));
                    }));
            }
            Steps.Add(Do(
                [this, Run]
                {
                    const FString Path = FPaths::ChangeExtension(Output, TEXT("")) + TEXT("-break-sounds.csv");
                    const FString Csv = TEXT("phase,time,event,file,volume,pitch,hit\n") +
                        FString::Join(Run->SoundRows, TEXT("\n")) + TEXT("\n");
                    const bool bSaved = FFileHelper::SaveStringToFile(Csv, *Path);
                    Record(TEXT("break sound log written"), bSaved && Run->SoundRows.Num() > 0,
                        FString::Printf(TEXT("%d plays to %s"), Run->SoundRows.Num(), *Path));
                }));
            Script(TEXT("tour break sounds"), 60, MoveTemp(Steps), Cleanup);
        }

        // The view's cost: LastViewMs over 10 s of mining stone (re-placed as it breaks) with A, then with B.
        {
            TArray<FStep> Steps;
            for (const FTourBreakConfig& Config : {ConfigA, ConfigB})
            {
                Steps.Add(Do(
                    [Run, Apply, Ensure, Pose, Config]
                    {
                        Apply(Config);
                        Run->PlaceFailures += Ensure(0) ? 0 : 1;
                        Pose(0);
                    }));
                Steps.Add(WaitSeconds(1.));
                Steps.Add(Do(
                    [this, Run]
                    {
                        Run->ViewMs.Reset();
                        Run->Breaks = 0;
                        PressKey(EKeys::LeftMouseButton, true);
                    }));
                Steps.Add(
                    [Lab, Run, Authority, Ensure, End = 0.]() mutable
                    {
                        const double Now = FPlatformTime::Seconds();
                        if (End == 0)
                            End = Now + 10;
                        else if (Lab->GetView())
                            Run->ViewMs.Add(Lab->GetView()->GetLastViewMs());
                        if (Authority().World.Get(GetTourBreakCell(0)).State.IsAir())
                        {
                            ++Run->Breaks;
                            Run->PlaceFailures += Ensure(0) ? 0 : 1;
                        }
                        return Now >= End;
                    });
                Steps.Add(Do(
                    [this, Run, Config]
                    {
                        PressKey(EKeys::LeftMouseButton, false);
                        double Mean = 0, P99 = 0;
                        GetTourMeanP99(Run->ViewMs, Mean, P99);
                        // A pure log would pass on an unmeasured run: both means must be real over enough frames.
                        Record(TEXT("break view cost: ") + Config.Name, Mean > 0 && Run->ViewMs.Num() > 100,
                            FString::Printf(TEXT("LastViewMs mean %.4f ms, p99 %.4f ms over %d frames (10 s mining "
                                                 "stone, %d breaks); %s, %s"),
                                Mean, P99, Run->ViewMs.Num(), Run->Breaks,
                                *FPlatformMisc::GetCPUBrand().TrimStartAndEnd(), *FPlatformMisc::GetPrimaryGPUBrand()));
                    }));
            }
            Script(TEXT("tour break cost"), 60, MoveTemp(Steps), Cleanup);
        }

        // Restore every cvar and pref the pack changed, the time and the tick order.
        Script(TEXT("tour break restore"), 10,
            {Do(
                [this, Lab, Run, SetPrefs]
                {
                    PressKey(EKeys::LeftMouseButton, false);
                    for (int32 Switch = 0; Switch < TourSwitchCount; ++Switch)
                        SetTourCVar(TourBreakSwitches[Switch], Run->SavedSwitches[Switch]);
                    SetTourCVar(TEXT("r.AntiAliasingMethod"), Run->SavedAntiAliasing);
                    SetTourCVar(TEXT("r.ScreenPercentage"), Run->SavedScreenPercentage);
                    SetPrefs(Run->SavedParticles, Run->bSavedHighContrast);
                    AWorldSettings* Settings = GetWorld()->GetWorldSettings();
                    Settings->SetTimeDilation(Run->SavedDilation);
                    Settings->MinGlobalTimeDilation = Run->SavedMinDilation;
                    if (Lab->GetView()) RemoveTickPrerequisiteComponent(Lab->GetView());
                    Controller->GetHud()->bHideHud = Run->bSavedHideHud;
                    Controller->GetHud()->bOverlay = Run->bSavedOverlay;
                    Controller->GetHud()->bShowCollision = Run->bSavedShowCollision;
                    Record(TEXT("break cvar names found"), TourCVarMisses.IsEmpty(),
                        FString::Printf(TEXT("unknown console variables: %s"),
                            TourCVarMisses.IsEmpty() ? TEXT("none") : *FString::Join(TourCVarMisses, TEXT(", "))));
                    bool bRestored = GetTourCVar(TEXT("r.AntiAliasingMethod")) == Run->SavedAntiAliasing &&
                        GetTourCVar(TEXT("r.ScreenPercentage")) == Run->SavedScreenPercentage &&
                        Settings->TimeDilation == Run->SavedDilation &&
                        Settings->MinGlobalTimeDilation == Run->SavedMinDilation;
                    for (int32 Switch = 0; Switch < TourSwitchCount; ++Switch)
                        bRestored &= GetTourCVar(TourBreakSwitches[Switch]) == Run->SavedSwitches[Switch];
                    Record(TEXT("break cvars and prefs restored"), bRestored,
                        FString::Printf(TEXT("foundation.Break.* %s, r.AntiAliasingMethod %s, r.ScreenPercentage %s, "
                                             "time dilation %.2f, particles %s, high contrast %s"),
                            *FString::Join(TArrayView<const FString>(Run->SavedSwitches), TEXT("/")),
                            *Run->SavedAntiAliasing, *Run->SavedScreenPercentage, Run->SavedDilation,
                            *Run->SavedParticles, Run->bSavedHighContrast ? TEXT("on") : TEXT("off")));
                    Record(TEXT("break prefs applied"), Run->PrefFailures == 0,
                        FString::Printf(TEXT("%d pref changes did not read back"), Run->PrefFailures));
                    Record(TEXT("break blocks re-placed"), Run->PlaceFailures == 0,
                        FString::Printf(TEXT("%d placements failed"), Run->PlaceFailures));
                    Record(TEXT("break shots taken"), Run->Shots.Num() == 340,
                        FString::Printf(
                            TEXT("%d shots (expected 340: mining 224, outline 20, break 96)"), Run->Shots.Num()));
                    if (!Extra) Extra = MakeShared<FJsonObject>();
                    Extra->SetArrayField(TEXT("break_shots"), Run->Shots);
                })},
            Cleanup);
        return;
    }

    // A moment for shaders to compile and textures to stream before the first picture.
    AddCase(TEXT("tour warm-up"), 4., [this] { ResetPlayer(); }, [] {});

    // a. Spawn overview: from above and southwest, over the stations, signs and training targets.
    AddCase(
        TEXT("tour spawn overview"), 1.,
        [this, Hover]
        {
            ResetPlayer();
            Controller->GetHud()->bHideHud = Controller->GetHud()->bOverlay = Controller->GetHud()->bShowCollision =
                false;
            Hover(FVector(-450, -600, 380), FVector(50, 200, 40));
        },
        [Shot] { Shot(TEXT("01-spawn")); });
    Flush(TEXT("spawn"));

    // b. The dev grid at eye height: 1 m tiles, labels and the red (east) and green (south) axis lines.
    AddCase(
        TEXT("tour dev grid"), .8,
        [this, Look]
        {
            ResetPlayer({-250, -350, 0});
            Look(FVector(250, 0, 0));
        },
        [Shot] { Shot(TEXT("02-grid")); });
    Flush(TEXT("grid"));

    // c. Building: a three-step staircase, three blocks wide, placed through authority commands (a second player
    // aiming straight down onto each column, as the stress scenario does), then seen from its side.
    AddCase(
        TEXT("tour build"), .8,
        [this, Authority, Look]
        {
            FFoundationAuthority& A = Authority();
            const int32 Before = A.World.GetSolidCount();
            FFoundationVitals Builder;
            const int32 Id = A.AddPlayer();
            uint32 Sequence = 1;
            int32 Placed = 0;
            for (int32 Z = 0; Z < 3; ++Z)
                for (int32 X = -3; X <= -1; ++X)
                    for (int32 Y = -8; Y <= -6; ++Y)
                    {
                        // Column X + 4 blocks high: one, two and three steps.
                        if (Z > X + 3) continue;
                        FFoundationPlayerFacts Facts;
                        Facts.EyeCm = FVector(X * 100 + 50, Y * 100 + 50, Z * 100 + 120);
                        Facts.FeetCm = Facts.EyeCm + FVector(0, 0, 5000);
                        Facts.Aim = FVector(0, 0, -1);
                        Facts.Step = Pawn->Simulation.Steps * 10000 + Sequence;
                        A.SetFacts(Id, Facts);
                        FFoundationCommand Command;
                        Command.SessionId = A.GetSessionId();
                        Command.Sequence = Sequence++;
                        Command.Type = EFoundationCommandType::UseOn;
                        Command.Item = A.FindPlayer(Id)->Inventory.GetHand(EFoundationHand::Main).GetRef();
                        Command.Target = A.Resolve(Facts);
                        FIntVector Destination;
                        EFoundationReason Ignored;
                        A.GetDestination(Command.Target, Destination, Ignored);
                        Command.Destination = Destination;
                        Command.DestinationRevision = A.World.Get(Destination).Revision;
                        Placed += A.Execute(Id, Command, Builder).bPlaced ? 1 : 0;
                    }
            Record(TEXT("tour staircase placed"), Placed == 18 && A.World.GetSolidCount() == Before + 18,
                FString::Printf(TEXT("%d placed, %d solid cells"), Placed, A.World.GetSolidCount()));
            // From the north-west corner, so the risers and treads are lit.
            ResetPlayer({-550, -250, 0});
            Look(FVector(-150, -650, 100));
        },
        [Shot] { Shot(TEXT("03-building")); });
    Flush(TEXT("build"));

    // d. Mining in progress on a stone seed block, bare hand (150 reference steps), captured past a third.
    AddCase(
        TEXT("tour mining"), 3.,
        [this, Click, Look]
        {
            ResetPlayer({400, 350, 0});
            Look(FVector(600, 350, 50));
            Click(EKeys::Four);
            PressKey(EKeys::LeftMouseButton, true);
        },
        [this, Shot]
        {
            Record(TEXT("tour mining in progress"), Pawn->Interaction.bMining && Pawn->Interaction.MiningProgress > .3,
                FString::Printf(TEXT("%.0f%%"), Pawn->Interaction.MiningProgress * 100));
            Shot(TEXT("04-mining"));
        });
    AddCase(TEXT("tour mining release"), .4, [this] { PressKey(EKeys::LeftMouseButton, false); }, [] {});

    // e. Combat: the sword facing the Training target, captured on the frame its hit lands (red body flash).
    AddCase(
        TEXT("tour sword charge"), .9,
        [this, Click, Look]
        {
            ResetPlayer({0, 760, 0});
            Click(EKeys::Three);
            Look(FVector(0, 1000, 95));
        },
        [] {});
    AddCase(
        TEXT("tour sword hit"), .8,
        [this, Click, Authority, Shot]
        {
            Click(EKeys::LeftMouseButton);
            FrameHook = [this, Authority, Shot, bShot = false]() mutable
            {
                if (bShot || Authority().Targets[0].Health.Health >= 20) return;
                bShot = true;
                Shot(TEXT("05-combat"));
            };
        },
        [this, Authority]
        {
            Record(TEXT("tour sword hit landed"), Authority().Targets[0].Health.Health < 20,
                FString::Printf(TEXT("target health %.1f"), Authority().Targets[0].Health.Health));
        });
    Flush(TEXT("combat"));

    // f. The movement course from the south and above: steps, ceiling, gaps, ledge, stairs, drop and water.
    AddCase(
        TEXT("tour movement course"), .8,
        [this, Hover, Click, Course]
        {
            ResetPlayer(Course);
            Click(EKeys::One);
            Hover(Course + FVector(200, -2300, 1250), Course + FVector(200, -150, 0));
        },
        [Shot] { Shot(TEXT("06-course")); });
    Flush(TEXT("course"));

    // g. Inside the water column: the eyes are underwater and the air bubbles show.
    AddCase(
        TEXT("tour water"), 1.5,
        [this, Look, Click, Course]
        {
            ResetPlayer(Course + FVector(1650, 450, 0));
            Click(EKeys::One);
            // Out of the column toward the stairs and the drop, with the bubbles over the hotbar.
            Look(Course + FVector(1650, -900, 150));
        },
        [this, Shot]
        {
            Record(TEXT("tour eyes underwater"), Pawn->Vitals.bEyesInWater && Pawn->Vitals.Air < 300,
                FString::Printf(TEXT("air %d"), int32(Pawn->Vitals.Air)));
            Shot(TEXT("07-water"));
        });
    Flush(TEXT("water"));

    // h. The F3 debug overlay, pressed through the router like the keyboard, over the lab controls.
    AddCase(
        TEXT("tour debug overlay"), .8,
        [this, Look, Click]
        {
            ResetPlayer({150, 0, 0});
            Click(EKeys::One);
            Look(FVector(-350, 50, 80));
            PressKey(EKeys::F3, true);
            PressKey(EKeys::F3, false);
        },
        [this, Shot]
        {
            Record(TEXT("tour F3 overlay open"), Controller->GetHud()->bOverlay, TEXT("F3 toggles the overlay"));
            Shot(TEXT("08-debug-f3"));
        });
    Flush(TEXT("debug"));

    // i. The block gallery (Docs/Blocks.md): one shot per showcase row from above and to the south-east, the
    // facing row again from the north-west (the other two furnace fronts), the animated row on two frames, and the
    // registry and every-state grids from above.
    auto RowCenter = [](int32 Row, int32 Blocks)
    {
        const FVector First = FVector(AFoundationPlayground::GetShowcaseCell(Row, 0)) * UFoundationProfile::BlockCm;
        return First + FVector((Blocks - 1) * 100 + 50, 50, 40);
    };
    auto Gallery = [this, Hover, Click](const TCHAR* Name, FVector Eye, FVector Target)
    {
        AddCase(
            FString(TEXT("tour gallery ")) + Name, 1.,
            [this, Hover, Click, Eye, Target]
            {
                ResetPlayer();
                // An empty hand: no placement preview in the picture.
                Click(EKeys::Four);
                Controller->GetHud()->bOverlay = false;
                Hover(Eye, Target);
            },
            [this, Name]
            {
                FScreenshotRequest::RequestScreenshot(
                    FPaths::ChangeExtension(Output, TEXT("")) + TEXT("-tour-") + Name + TEXT(".png"), true, false);
            });
        AddCase(FString(TEXT("tour flush ")) + Name, .4, [] {}, [] {});
    };
    const FVector Showcase = RowCenter(3, 4);
    Gallery(TEXT("09-showcase"), Showcase + FVector(-200, 1500, 900), Showcase + FVector(-200, 0, 0));
    const TCHAR* RowShots[] = {TEXT("10-grass"), TEXT("11-leaves"), TEXT("12-translucent"), TEXT("13-axis-facing-se"),
        TEXT("14-slabs"), TEXT("15-water")};
    const int32 RowBlocks[] = {6, 10, 8, 8, 8, 9};
    for (int32 Row = 0; Row < 6; ++Row)
    {
        const FVector Center = RowCenter(Row, RowBlocks[Row]);
        const double Back = RowBlocks[Row] * 60.;
        Gallery(RowShots[Row], Center + FVector(Back * .6, 260, 230), Center);
    }
    const FVector Facing = RowCenter(3, 8);
    Gallery(TEXT("13-axis-facing-nw"), Facing + FVector(-500, -230, 230), Facing);
    // Two frames of the animated row: the sea lantern changes frame every 5 ticks (a quarter second).
    const FVector Animated = RowCenter(6, 6);
    const FVector AnimatedEye = Animated + FVector(300, 230, 180);
    auto LanternLayer = [Lab]()
    {
        const UFoundationBlockTextureTable* Table = Lab->GetView() ? Lab->GetView()->GetTextureTable() : nullptr;
        const UFoundationBlockAssets* Assets = Lab->GetAuthorityState().World.GetRegistry().GetAssets();
        const int32 Index = Assets ? Assets->Textures.IndexOfByPredicate([](const FFoundationBlockTextureAsset& T)
                                         { return T.Name == TEXT("sea_lantern"); })
                                   : INDEX_NONE;
        return Table && Index != INDEX_NONE ? Table->GetCurrentLayer(Index) : -1;
    };
    TSharedRef<int32> FirstLayer = MakeShared<int32>(-1);
    AddCase(
        TEXT("tour gallery animated a"), 1.,
        [this, Hover, Click, AnimatedEye, Animated]
        {
            ResetPlayer();
            Click(EKeys::Four);
            Hover(AnimatedEye, Animated);
        },
        [Shot, LanternLayer, FirstLayer]
        {
            *FirstLayer = LanternLayer();
            Shot(TEXT("16-animated-a"));
        });
    AddCase(
        TEXT("tour gallery animated b"), .3, [] {},
        [this, Shot, LanternLayer, FirstLayer]
        {
            const int32 A = *FirstLayer, B = LanternLayer();
            Record(TEXT("tour animated frames differ"), A >= 0 && B >= 0 && A != B,
                FString::Printf(TEXT("sea lantern layer %d then %d"), A, B));
            Shot(TEXT("17-animated-b"));
        });
    Flush(TEXT("animated"));
    // The plant row (model blocks, phase 6 S6b): 12 states, the whole row from the south, then its middle up close.
    const FVector Plants = RowCenter(AFoundationPlayground::PlantRow, 12);
    Gallery(TEXT("17p-plants"), Plants + FVector(0, 1100, 650), Plants);
    Gallery(TEXT("17q-plants-near"), Plants + FVector(12 * 60. * .6, 260, 230), Plants);
    // The galleries from above their middle, backed off with their row count (23 and 59 rows before the model blocks
    // joined them).
    const FFoundationBlockRegistry& Blocks = *FFoundationBlockRegistry::GetDefault();
    int32 StateCount = 0;
    for (int32 Block = 1; Block < Blocks.GetBlockCount(); ++Block)
        StateCount += Blocks.GetBlockAt(Block).StateCount;
    const int32 RegistryRows =
        FMath::DivideAndRoundUp(Blocks.GetBlockCount() - 1, AFoundationPlayground::RegistryGalleryColumns);
    const int32 StateRows = FMath::DivideAndRoundUp(StateCount, AFoundationPlayground::StateGalleryColumns);
    const FVector Registry = (FVector(AFoundationPlayground::RegistryGalleryOrigin) +
                                 FVector(AFoundationPlayground::RegistryGalleryColumns, RegistryRows - 1, 0)) *
        UFoundationProfile::BlockCm;
    Gallery(TEXT("18-gallery-registry"), Registry + FVector(0, 3200, 2600) * (RegistryRows / 23.), Registry);
    const FVector States = (FVector(AFoundationPlayground::StateGalleryOrigin) +
                               FVector(AFoundationPlayground::StateGalleryColumns, StateRows - 1, 0)) *
        UFoundationProfile::BlockCm;
    Gallery(TEXT("19-gallery-states"), States + FVector(0, 7000, 5500) * (StateRows / 59.), States);

    // i2. The hotbar's item sprites and durability bars (items S3): the loadout's iron pickaxe and sword worn to 125
    // and 200 of 250, kept for the creative shots' hotbar row and repaired after them.
    auto Wear = [this, Authority](int32 Pickaxe, int32 Sword)
    {
        if (FFoundationPlayer* Player = Authority().FindPlayer(Pawn->Interaction.PlayerId))
        {
            Player->Inventory.Hotbar[1].Damage = Pickaxe;
            Player->Inventory.Hotbar[2].Damage = Sword;
            ++Player->Inventory.Revision;
        }
    };
    AddCase(
        TEXT("tour hotbar worn"), .6,
        [this, Wear]
        {
            ResetPlayer({0, 0, 0});
            Wear(125, 200);
        },
        [this, Shot, Authority]
        {
            const FFoundationPlayer* Player = Authority().FindPlayer(Pawn->Interaction.PlayerId);
            Record(TEXT("tour hotbar worn"), Player && Player->Inventory.Hotbar[1].Damage == 125,
                TEXT("iron pickaxe 125 and iron sword 200 of 250"));
            Shot(TEXT("19h-hotbar-worn"));
        });
    AddCase(TEXT("tour flush hotbar"), .4, [] {}, [] {});

    // i3. Held items (items S4): each item's extruded sprite in first person, standing on the grid facing east at
    // pitch 0 (probe P1's scene; the hand's pose stays provisional until P1 runs), the empty hand as the control and a
    // held block to compare. Slot 4 holds the item; it and the selection are restored after, so the creative shots'
    // hotbar row is unchanged.
    // The selection and slot 4 before the first held case, restored after the last.
    const TSharedRef<int32> Selected = MakeShared<int32>(INDEX_NONE);
    const TSharedRef<FFoundationItemInstance> Slot4 = MakeShared<FFoundationItemInstance>();
    auto Hold = [this, Authority, Selected, Slot4](const FFoundationItemInstance& Stack, bool bRestore)
    {
        FFoundationPlayer* Player = Authority().FindPlayer(Pawn->Interaction.PlayerId);
        if (!Player) return;
        FFoundationInventory& Inventory = Player->Inventory;
        if (*Selected == INDEX_NONE)
        {
            *Selected = Inventory.Selected;
            *Slot4 = Inventory.Hotbar[3];
        }
        Inventory.SetSlot(3, bRestore ? *Slot4 : Stack);
        Inventory.Select(bRestore ? *Selected : 3);
    };
    for (const TCHAR* Item : {TEXT("empty"), TEXT("iron_pickaxe"), TEXT("iron_sword"), TEXT("stick"), TEXT("apple"),
             TEXT("water_bucket"), TEXT("iron_spear"), TEXT("wooden_pickaxe"), TEXT("golden_pickaxe"),
             TEXT("netherite_pickaxe"), TEXT("stone")})
    {
        AddCase(
            FString(TEXT("tour held ")) + Item, .5,
            [this, Authority, Hold, Item]
            {
                ResetPlayer({-250, -350, 0});
                FFoundationPlayer* Player = Authority().FindPlayer(Pawn->Interaction.PlayerId);
                if (!Player) return;
                const bool bEmpty = FName(Item) == TEXT("empty");
                Hold(bEmpty ? FFoundationItemInstance()
                            : Player->Inventory.MakeStack(Authority().Rules.FindItem(Item), 1),
                    false);
            },
            [this, Shot, Lab, Item]
            {
                // An item draws its sprite; the empty hand and the block draw none.
                const FName Sprite = Lab->GetView()->GetHeldSprite();
                const bool bSprite = FName(Item) != TEXT("empty") && FName(Item) != TEXT("stone");
                Record(*(FString(TEXT("tour held ")) + Item),
                    Lab->GetView()->IsHeldItemVisible() && bSprite == !Sprite.IsNone() &&
                        bSprite == Lab->GetView()->GetHeldItem()->IsVisible(),
                    FString::Printf(TEXT("sprite %s"), *Sprite.ToString()));
                Shot(*(FString(TEXT("19i-held-")) + Item));
            });
        // The shot is taken on a later frame: hold the item still until then.
        Flush(*(FString(TEXT("held ")) + Item));
    }
    // The glow's A/B pair: a few items again with foundation.Held.Glow 0, lit by the scene alone.
    // Its value before the first of them, restored after the last.
    const TSharedRef<FString> Glow = MakeShared<FString>();
    struct FGlowShot
    {
        const TCHAR* Item;
        const TCHAR* Glow;
    };
    for (const FGlowShot& Pair : {FGlowShot{TEXT("iron_pickaxe"), TEXT("0")}, FGlowShot{TEXT("iron_sword"), TEXT("0")},
             FGlowShot{TEXT("water_bucket"), TEXT("0")}, FGlowShot{TEXT("apple"), TEXT("0")}})
    {
        AddCase(
            FString::Printf(TEXT("tour held %s glow %s"), Pair.Item, Pair.Glow), .5,
            [this, Authority, Hold, Pair, Glow]
            {
                ResetPlayer({-250, -350, 0});
                if (Glow->IsEmpty()) *Glow = GetTourCVar(TEXT("foundation.Held.Glow"));
                SetTourCVar(TEXT("foundation.Held.Glow"), Pair.Glow);
                if (FFoundationPlayer* Player = Authority().FindPlayer(Pawn->Interaction.PlayerId))
                    Hold(Player->Inventory.MakeStack(Authority().Rules.FindItem(Pair.Item), 1), false);
            },
            [Shot, Pair] { Shot(*FString::Printf(TEXT("19i-held-%s-glow%s"), Pair.Item, Pair.Glow)); });
        Flush(*FString::Printf(TEXT("held %s glow %s"), Pair.Item, Pair.Glow));
    }
    AddCase(
        TEXT("tour held restore"), .4,
        [Hold, Glow]
        {
            if (!Glow->IsEmpty()) SetTourCVar(TEXT("foundation.Held.Glow"), *Glow);
            Hold(FFoundationItemInstance(), true);
        },
        [] {});

    // j. The creative inventory (E): a category tab with a hovered item and its tooltip, the search tab with a
    // query, and the survival inventory tab, each over the darkened world.
    auto Creative = [this](const TCHAR* Name, TFunction<void(SHudCreativeScreen&)> Arrange)
    {
        AddCase(
            FString(TEXT("tour creative ")) + Name, .8,
            [this, Arrange]
            {
                ResetPlayer({0, 0, 0});
                if (!Controller->GetPanels()->IsInventoryOpen()) Controller->GetPanels()->OpenInventory();
                if (TSharedPtr<SHudCreativeScreen> Screen = Controller->GetPanels()->GetCreativeScreen())
                    Arrange(*Screen);
                // A full garbage collection with the screen open: its brushes' textures must survive it (they did not
                // once the held-item shots made the tour long enough to reach the periodic one).
                CollectGarbage(GARBAGE_COLLECTION_KEEPFLAGS, true);
            },
            [this, Name]
            {
                Record(FString(TEXT("tour creative open: ")) + Name,
                    Controller->GetPanels()->GetCreativeScreen().IsValid(),
                    TEXT("E opens the creative inventory in the Playground"));
                FScreenshotRequest::RequestScreenshot(
                    FPaths::ChangeExtension(Output, TEXT("")) + TEXT("-tour-") + Name + TEXT(".png"), true, false);
            });
        AddCase(FString(TEXT("tour flush ")) + Name, .4, [] {}, [] {});
    };
    auto TabIndex = [](SHudCreativeScreen& Screen, const TCHAR* Id)
    {
        return Screen.GetModel().GetTabs().IndexOfByPredicate(
            [Id](const FHudCreativeModel::FTab& Tab) { return Tab.Id == Id; });
    };
    Creative(TEXT("20-creative-building"),
        [TabIndex](SHudCreativeScreen& Screen)
        {
            Screen.GetModel().SelectTab(TabIndex(Screen, TEXT("building_blocks")));
            Screen.SetMouse(FHudCreativeModel::GetSlotPosition(EHudCreativeHit::Grid, 13) + FIntPoint(8, 8));
        });
    Creative(TEXT("21-creative-search"),
        [TabIndex](SHudCreativeScreen& Screen)
        {
            Screen.GetModel().SelectTab(TabIndex(Screen, TEXT("search")));
            Screen.GetModel().SetSearch(TEXT("glass"));
            Screen.SetMouse({-200, -200});
        });
    Creative(TEXT("22-creative-natural-carried"),
        [TabIndex](SHudCreativeScreen& Screen)
        {
            Screen.GetModel().SelectTab(TabIndex(Screen, TEXT("natural_blocks")));
            Screen.GetModel().Carried = {TEXT("grass_block"), 64};
            Screen.SetMouse({120, 70});
        });
    // The model blocks' items (phase 6, S5): natural blocks rows 8-22, flat sprites and 3D block models.
    for (const auto& [Name, Rows] : {TPair<const TCHAR*, int32>{TEXT("22b-creative-natural-plants-a"), 8},
             {TEXT("22c-creative-natural-plants-b"), 13}, {TEXT("22d-creative-natural-plants-c"), 18}})
        Creative(Name,
            [TabIndex, Rows](SHudCreativeScreen& Screen)
            {
                Screen.GetModel().Carried = {};
                Screen.GetModel().SelectTab(TabIndex(Screen, TEXT("natural_blocks")));
                Screen.GetModel().ScrollRows(Rows);
                Screen.SetMouse({-200, -200});
            });
    // The 26.3 items (items S2, DA_Items): their own sprites, hovering the iron pickaxe and the netherite sword
    // (items S3: the "When in Main Hand" lines; the hotbar row shows the worn tools' bars).
    Creative(TEXT("22e-creative-tools"),
        [TabIndex](SHudCreativeScreen& Screen)
        {
            Screen.GetModel().Carried = {};
            Screen.GetModel().SelectTab(TabIndex(Screen, TEXT("tools_and_utilities")));
            Screen.SetMouse(FHudCreativeModel::GetSlotPosition(EHudCreativeHit::Grid, 13) + FIntPoint(8, 8));
        });
    Creative(TEXT("22f-creative-combat"),
        [TabIndex](SHudCreativeScreen& Screen)
        {
            Screen.GetModel().SelectTab(TabIndex(Screen, TEXT("combat")));
            Screen.SetMouse(FHudCreativeModel::GetSlotPosition(EHudCreativeHit::Grid, 6) + FIntPoint(8, 8));
        });
    Creative(TEXT("23-creative-inventory"),
        [TabIndex](SHudCreativeScreen& Screen)
        {
            Screen.GetModel().Carried = {};
            Screen.GetModel().SelectTab(TabIndex(Screen, TEXT("inventory")));
            Screen.SetMouse(FHudCreativeModel::GetSlotPosition(EHudCreativeHit::Hotbar, 0) + FIntPoint(8, 8));
        });
    AddCase(
        TEXT("tour creative close"), .3,
        [this, Wear]
        {
            Controller->GetPanels()->Close();
            Wear(0, 0);
        },
        [] {});
#endif
}
