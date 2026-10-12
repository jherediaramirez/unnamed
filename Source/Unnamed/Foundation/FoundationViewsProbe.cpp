#include "Foundation/FoundationRuntimeProbe.h"
#include "Foundation/FoundationBlockRegistry.h"
#include "Foundation/FoundationBreakEffects.h"
#include "Foundation/FoundationController.h"
#include "Foundation/FoundationGrid.h"
#include "Foundation/FoundationHud.h"
#include "Foundation/FoundationInteractionView.h"
#include "Foundation/FoundationPawn.h"
#include "Foundation/FoundationProfile.h"
#include "Foundation/FoundationVitals.h"
#include "Menu/MenuPreferences.h"
#include "Storage/VoxelColumn.h"
#include "Voxel/VoxelColumnPipeline.h"
#include "Voxel/VoxelConsole.h"
#include "Voxel/VoxelRenderDistance.h"
#include "Voxel/VoxelWorld.h"
#include "Voxel/VoxelWorldSubsystem.h"
#include "VoxelConstants.h"
#include "UnnamedLog.h"
#include "Dom/JsonValue.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "HAL/IConsoleManager.h"
#include "Misc/CommandLine.h"
#include "Misc/FileHelper.h"
#include "Misc/Parse.h"
#include "Misc/Paths.h"
#include "UnrealClient.h"

#if WITH_DEV_AUTOMATION_TESTS
namespace
{
    // A general view: the feet cell (the player stands at its centre) and the look, in degrees (yaw 0 is +X, 90 +Y).
    struct FViewsPose
    {
        FIntVector Cell = FIntVector::ZeroValue;
        double Yaw = 0, Pitch = 0;
    };

    // An outline row: its first target's cell (on the floor), then a target every Spacing cells along +X; the player
    // stands on the row's -Y side, on the same floor.
    struct FViewsPad
    {
        FIntVector Cell = FIntVector::ZeroValue;
        int32 Spacing = 2;
    };

    // The poses file (Scripts/Views/DevSeed.txt): "pose <name> X Y Z yaw pitch", "pad <name> X Y Z spacing" and
    // "torch X Y Z" lines, '#' comments.
    struct FViewsPoses
    {
        TMap<FString, FViewsPose> Poses;
        TMap<FString, FViewsPad> Pads;
        TArray<FIntVector> Torches;
        TArray<FString> Errors;
    };

    // The names the suite looks up; any other name in the poses file is an error.
    const TCHAR* const ViewsPoseNames[] = {TEXT("day-open"), TEXT("forest"), TEXT("water"), TEXT("cave")};
    const TCHAR* const ViewsPadNames[] = {TEXT("day"), TEXT("cave")};

    // A line with a field that is not a number (whole for cells and spacing), an unknown or repeated name, or a
    // spacing below 1 is an error and is left out; the errors are reported together as one failing check.
    FViewsPoses LoadViewsPoses(const FString& Path)
    {
        FViewsPoses Out;
        TArray<FString> Lines;
        if (!FFileHelper::LoadFileToStringArray(Lines, *Path))
        {
            Out.Errors.Add(FString::Printf(TEXT("cannot read %s"), *Path));
            return Out;
        }
        auto IsNumber = [](const FString& Text, bool bWhole)
        {
            int32 Digits = 0;
            for (const TCHAR Char : Text)
                Digits += FChar::IsDigit(Char) ? 1 : 0;
            return Digits > 0 && FCString::IsNumeric(*Text) && (!bWhole || !Text.Contains(TEXT(".")));
        };
        auto IsKnown = [](const auto& Names, const FString& Name)
        {
            for (const TCHAR* Known : Names)
                if (Name == Known) return true;
            return false;
        };
        for (int32 Index = 0; Index < Lines.Num(); ++Index)
        {
            FString Line = Lines[Index];
            int32 Hash = INDEX_NONE;
            if (Line.FindChar(TEXT('#'), Hash)) Line.LeftInline(Hash);
            TArray<FString> T;
            Line.ParseIntoArrayWS(T);
            if (T.IsEmpty()) continue;
            auto Error = [&Out, &Lines, Index](const FString& What)
            {
                Out.Errors.Add(
                    FString::Printf(TEXT("line %d %s: %s"), Index + 1, *What, *Lines[Index].TrimStartAndEnd()));
            };
            const bool bPose = T[0] == TEXT("pose") && T.Num() == 7, bPad = T[0] == TEXT("pad") && T.Num() == 6,
                       bTorch = T[0] == TEXT("torch") && T.Num() == 4;
            if (!bPose && !bPad && !bTorch)
            {
                Error(TEXT("not understood"));
                continue;
            }
            bool bNumbers = true;
            for (int32 I = bTorch ? 1 : 2; I < T.Num(); ++I)
            {
                const bool bWhole = !(bPose && I >= 5);
                if (IsNumber(T[I], bWhole)) continue;
                Error(FString::Printf(
                    TEXT("field %d '%s' is not a %s"), I + 1, *T[I], bWhole ? TEXT("whole number") : TEXT("number")));
                bNumbers = false;
            }
            if (!bNumbers) continue;
            auto Int = [&T](int32 I) { return FCString::Atoi(*T[I]); };
            auto Num = [&T](int32 I) { return FCString::Atod(*T[I]); };
            if (bTorch)
            {
                Out.Torches.Add(FIntVector(Int(1), Int(2), Int(3)));
                continue;
            }
            if (!(bPose ? IsKnown(ViewsPoseNames, T[1]) : IsKnown(ViewsPadNames, T[1])))
                Error(FString::Printf(TEXT("unknown %s name '%s'"), *T[0], *T[1]));
            else if (bPose ? Out.Poses.Contains(T[1]) : Out.Pads.Contains(T[1]))
                Error(FString::Printf(TEXT("%s '%s' repeated (the first is used)"), *T[0], *T[1]));
            else if (bPad && Int(5) < 1)
                Error(FString::Printf(TEXT("pad '%s' spacing %d below 1"), *T[1], Int(5)));
            else if (bPose)
                Out.Poses.Add(T[1], {FIntVector(Int(2), Int(3), Int(4)), Num(5), Num(6)});
            else
                Out.Pads.Add(T[1], {FIntVector(Int(2), Int(3), Int(4)), Int(5)});
        }
        return Out;
    }

    // Where the player stands and looks.
    struct FViewsStance
    {
        FVector Feet = FVector::ZeroVector;
        FRotator Look = FRotator::ZeroRotator;
    };

    // The local player's first hotbar slot (its item, None when empty) and the selected slot.
    struct FViewsHotbar
    {
        FName Item;
        int32 Count = 0, Damage = 0, Selected = 0;

        bool operator==(const FViewsHotbar& Other) const = default;
        FString ToString() const
        {
            return FString::Printf(
                TEXT("slot 0 %s x%d (damage %d), selected %d"), *Item.ToString(), Count, Damage, Selected);
        }
    };

    // What the suite changes (restored by its last case) and what its cases hand on to each other.
    struct FViewsRun
    {
        FString SavedPreview, SavedOutline;
        // The engine's on-screen virtual shadow map warnings, kept off the shots.
        FString SavedOverflowMessages;
        bool bSavedHighContrast = false, bSavedPaused = false, bSaved = false;
        // The day clock's total (day and time of day) before the suite sets noon.
        int64 SavedTotalTicks = 0;
        FViewsHotbar SavedHotbar;
        bool bSavedHotbar = false;
        // Restore's reading of the clock just after setting it back, before it resumes.
        int64 RestoredTotalTicks = -1;
        // Per pad: the stone, slab and plant cells as placed (the plant may stand on a placed grass block).
        TMap<FString, TArray<FIntVector>> Targets;
    };

    // The row's targets, in order, and the distances (blocks from the target's near face) each is seen from.
    struct FViewsTarget
    {
        const TCHAR* Name;
        const TCHAR* Item;
        TArray<int32> Distances;
    };

    const TCHAR* const ViewsPreviewCVar = TEXT("foundation.Place.Preview");
    const TCHAR* const ViewsOutlineCVar = TEXT("foundation.Break.Outline");
    const TCHAR* const ViewsOverflowCVar = TEXT("r.Shadow.Virtual.AllowScreenOverflowMessages");

    FString GetViewsCVar(const TCHAR* Name)
    {
        const IConsoleVariable* Variable = IConsoleManager::Get().FindConsoleVariable(Name);
        return Variable ? Variable->GetString() : FString();
    }

    bool SetViewsCVar(const TCHAR* Name, const FString& Value)
    {
        IConsoleVariable* Variable = IConsoleManager::Get().FindConsoleVariable(Name);
        if (Variable) Variable->Set(*Value, ECVF_SetByConsole);
        return Variable != nullptr;
    }

    bool IsViewsTreeBlock(const FString& Id)
    {
        return Id.EndsWith(TEXT("_leaves")) || Id.EndsWith(TEXT("_log"));
    }
}
#endif

// -FoundationSuite=views (never part of "all"): player-view screenshots of the generated world (/Game/Maps/L_Generated
// opened plainly: the dev seed) with the HUD, the block outline and the held item, named
// <report>-views-<pose>-<variant> .png. The poses come from Scripts/Views/DevSeed.txt (-FoundationViewsPoses=<file>
// overrides it), found by the scout
// (-FoundationViewsScout: logs candidate poses to <report>-views-scout.txt and takes no shots). Sections: general views
// holding stone (day-open, forest, water, cave, the cave at midnight by a torch; the sky does not follow voxel.Time
// yet), the placement preview pair and the outline rows (stone, bottom slab, plant on grass; the registry has no stairs
// yet) by day and in the cave, each seen with foundation.Break.Outline 1, 2 and 1 with the high-contrast preference.
// Blocks are placed by authority commands from a builder player; the last case restores the switches, the preference,
// the clock (its total and pause) and the local player's first hotbar slot and selection. The placed blocks stay.
void UFoundationRuntimeProbe::AddViewsCases()
{
#if WITH_DEV_AUTOMATION_TESTS
    AVoxelWorld* Voxel = Cast<AVoxelWorld>(Pawn->GetLab());
    if (!Voxel || !Voxel->GetAuthority())
    {
        AddCase(
            TEXT("views world present"), .1, [] {},
            [this]
            {
                Record(TEXT("views generated world present"), false,
                    TEXT("the map has no AVoxelWorld; the views suite opens /Game/Maps/L_Generated"));
            });
        return;
    }
    auto Authority = [Voxel]() -> FFoundationAuthority& { return *Voxel->GetAuthority(); };
    auto View = [Voxel] { return Voxel->FindComponentByClass<UFoundationInteractionView>(); };
    auto Effects = [View]() -> FFoundationBreakEffects*
    {
        const UFoundationInteractionView* Found = View();
        return Found ? Found->GetBreakEffects() : nullptr;
    };
    auto RunCommand = [this](const FString& Command) { GEngine->Exec(GetWorld(), *Command); };
    auto Preferences = [this]() -> UMenuPreferences*
    {
        UGameInstance* Instance = GetWorld() ? GetWorld()->GetGameInstance() : nullptr;
        return Instance ? Instance->GetSubsystem<UMenuPreferences>() : nullptr;
    };
    auto GetHighContrast = [Preferences]
    {
        const UMenuPreferences* Found = Preferences();
        const TSharedPtr<const FJsonValue> Value =
            Found ? Found->FindPreference(TEXT("highContrastBlockOutline")) : nullptr;
        return Value && Value->Type == EJson::Boolean && Value->AsBool();
    };
    auto SetHighContrast = [Preferences](bool bOn)
    {
        if (UMenuPreferences* Found = Preferences())
            Found->ApplyChanges(FString::Printf(
                TEXT("{\"settings\":{\"highContrastBlockOutline\":%s}}"), bOn ? TEXT("true") : TEXT("false")));
    };
    auto CellMin = [Authority](const FIntVector& Cell)
    { return FoundationGrid::CellMin(Cell, Authority().World.OriginCm); };
    auto BlockId = [Authority](const FIntVector& Cell)
    {
        const FFoundationAuthority& A = Authority();
        return A.World.GetBlock(A.World.Get(Cell).State).Id;
    };
    auto Holding = [this, Authority](FName Item)
    {
        const FFoundationAuthority& A = Authority();
        const FFoundationPlayer* Player = A.FindPlayer(Pawn->Interaction.PlayerId);
        if (!Player) return false;
        const FFoundationItemInstance& Stack = Player->Inventory.GetHand(EFoundationHand::Main);
        return !Stack.IsEmpty() && A.Rules.GetItem(Stack.Item).Id == Item;
    };
    auto ReadHotbar = [this, Authority](FViewsHotbar& Out)
    {
        const FFoundationAuthority& A = Authority();
        const FFoundationPlayer* Player = A.FindPlayer(Pawn->Interaction.PlayerId);
        if (!Player || !Player->Inventory.Hotbar.IsValidIndex(0)) return false;
        const FFoundationItemInstance& Stack = Player->Inventory.Hotbar[0];
        Out = {};
        if (!Stack.IsEmpty()) Out = {A.Rules.GetItem(Stack.Item).Id, Stack.Count, Stack.Damage};
        Out.Selected = Player->Inventory.Selected;
        return true;
    };
    // The state of the view a shot's check reports.
    auto Describe = [this, Voxel, View, Effects, BlockId, GetHighContrast]
    {
        const UFoundationInteractionView* Found = View();
        const FFoundationBreakEffects* Break = Effects();
        const FFoundationHit& Hit = Pawn->Interaction.Preview.Target;
        return FString::Printf(TEXT("time of day %d; Preview %s, Outline %s, high contrast %s; view outline %d, "
                                    "effects outline %d (high contrast %d), preview drawn %d, held item %d; feet %s, "
                                    "look %s; aimed %s %s"),
            Voxel->GetClock().GetTimeOfDay(), *GetViewsCVar(ViewsPreviewCVar), *GetViewsCVar(ViewsOutlineCVar),
            GetHighContrast() ? TEXT("on") : TEXT("off"), int32(Found && Found->IsOutlineVisible()),
            int32(Break && Break->IsOutlineVisible()), int32(Break && Break->IsHighContrastOutline()),
            int32(Found && Found->IsPreviewVisible()), int32(Found && Found->IsHeldItemVisible()),
            *Pawn->Simulation.Feet.ToCompactString(), *Controller->GetControlRotation().ToCompactString(),
            Hit.Kind == EFoundationHitKind::Block ? *Hit.Cell.ToString() : TEXT("nothing"),
            Hit.Kind == EFoundationHitKind::Block ? *BlockId(Hit.Cell).ToString() : TEXT(""));
    };

    // A builder takes Item from the creative inventory and uses it from above on the cell below Cell (the tour's
    // pattern): a bottom slab, the plant on its grass block. False, placing nothing, when the item is unknown.
    auto Place = [this, Authority, CellMin](const FIntVector& Cell, const TCHAR* Item)
    {
        FFoundationAuthority& A = Authority();
        FFoundationVitals Builder;
        const int32 Id = A.AddPlayer();
        FFoundationPlayerFacts Facts;
        Facts.EyeCm = CellMin(Cell) + FVector(50, 50, 120);
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
        if (!A.Execute(Id, Slot, Builder).IsCommitted())
        {
            A.RemovePlayer(Id);
            return false;
        }
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
    // An empty-handed builder breaks what stands in Cell when it breaks at once (hardness 0: grass, flowers), aiming
    // down from the cell above, so no plant hides a target or crosses the view ray.
    auto Clear = [this, Authority, CellMin](const FIntVector& Cell)
    {
        FFoundationAuthority& A = Authority();
        int32 Broken = 0;
        for (int32 Attempt = 0; Attempt < 3; ++Attempt)
        {
            const FFoundationBlockState State = A.World.Get(Cell).State;
            const FFoundationBlockType& Block = A.World.GetBlock(State);
            if (State.IsAir() || Block.Hardness != 0 || Block.IsFluid() ||
                A.World.GetRegistry().GetShape(State) == EFoundationBlockShape::Full)
                break;
            FFoundationVitals Builder;
            const int32 Id = A.AddPlayer();
            FFoundationPlayerFacts Facts;
            Facts.EyeCm = CellMin(Cell) + FVector(50, 50, 120);
            Facts.FeetCm = Facts.EyeCm + FVector(0, 0, 5000);
            Facts.Aim = FVector(0, 0, -1);
            Facts.Step = Pawn->Simulation.Steps * 10000 + 1;
            A.SetFacts(Id, Facts);
            FFoundationCommand Command;
            Command.SessionId = A.GetSessionId();
            Command.Sequence = 1;
            Command.Type = EFoundationCommandType::MineStart;
            Command.Item = A.FindPlayer(Id)->Inventory.GetHand(EFoundationHand::Main).GetRef();
            Command.Target = A.Resolve(Facts);
            Broken += A.Execute(Id, Command, Builder).bBroken ? 1 : 0;
            A.RemovePlayer(Id);
        }
        return Broken;
    };
    auto HasBlock = [BlockId](const FIntVector& Cell, const TCHAR* Item) { return BlockId(Cell) == FName(Item); };

    const TSharedRef<FViewsRun> Run = MakeShared<FViewsRun>();
    FString PosesPath = FPaths::Combine(FPaths::ProjectDir(), TEXT("Scripts/Views/DevSeed.txt"));
    FParse::Value(FCommandLine::Get(), TEXT("FoundationViewsPoses="), PosesPath);
    PosesPath = FPaths::ConvertRelativePathToFull(PosesPath);
    const FViewsPoses Poses = LoadViewsPoses(PosesPath);
    const bool bScout = FParse::Param(FCommandLine::Get(), TEXT("FoundationViewsScout"));
    const FString ShotBase = FPaths::ChangeExtension(Output, TEXT("")) + TEXT("-views-");

    // Stands the player and waits (up to a minute) until the streamed world around it is on screen: at least 45 frames,
    // then 20 ready frames in a row spanning at least 1.5 s (so a high frame cap does not outrun the streaming); then
    // the pose is set again (the columns now hold the real ground) and held 20 frames and at least a third of a second.
    auto Go = [this, Voxel](const FString& Name, TFunction<FViewsStance()> Stance)
    {
        AddCase(
            TEXT("views go ") + Name, 60,
            [this, Voxel, Stance]
            {
                auto Stand = [this, Stance]
                {
                    const FViewsStance Pose = Stance();
                    ResetPlayer(Pose.Feet);
                    Controller->SetControlRotation(Pose.Look);
                };
                Stand();
                FrameHook = [this, Voxel, Stand, Waited = 0, Ready = 0, Settle = -1, Since = 0.]() mutable
                {
                    ++Waited;
                    const double Now = FPlatformTime::Seconds();
                    if (Settle >= 0)
                    {
                        if (++Settle >= 20 && Now - Since >= 1. / 3) Cases[Phase].Duration = 0;
                        return;
                    }
                    Ready = Voxel->IsReady() ? Ready + 1 : 0;
                    if (Ready == 1) Since = Now;
                    if (Waited >= 45 && Ready >= 20 && Now - Since >= 1.5)
                    {
                        Stand();
                        Settle = 0;
                        Since = Now;
                    }
                };
            },
            [this, Voxel, Name]
            {
                Record(TEXT("views ") + Name + TEXT(" world ready"), Voxel->IsReady(),
                    FString::Printf(TEXT("feet %s"), *Pawn->Simulation.Feet.ToCompactString()));
            });
    };
    // One shot: Setup as the case starts; after .7 s (the temporal pass converges) the check and the capture, UI on.
    auto Shot = [this, ShotBase](const FString& Name, TFunction<void()> Setup, TFunction<bool(FString&)> Check)
    {
        AddCase(TEXT("views ") + Name, .7, MoveTemp(Setup),
            [this, ShotBase, Name, Check]
            {
                FString Detail;
                const bool bPassed = Check(Detail);
                Record(TEXT("views shot ") + Name, bPassed, Detail);
                FScreenshotRequest::RequestScreenshot(ShotBase + Name + TEXT(".png"), true, false);
            });
        AddCase(TEXT("views flush ") + Name, .3, [] {}, [] {});
    };
    auto PoseStance = [CellMin](const FViewsPose& Pose) -> TFunction<FViewsStance()>
    {
        return [CellMin, Pose]
        { return FViewsStance{CellMin(Pose.Cell) + FVector(50, 50, 0), FRotator(Pose.Pitch, Pose.Yaw, 0)}; };
    };
    auto SetTime = [RunCommand](const TCHAR* Time) { RunCommand(FString(TEXT("voxel.Time set ")) + Time); };

    // The world on screen around the spawn (the loading screen has gone).
    AddCase(
        TEXT("views ready"), 120,
        [this, Voxel]
        {
            FrameHook = [this, Voxel, Ready = 0]() mutable
            {
                Ready = Voxel->IsReady() ? Ready + 1 : 0;
                if (Ready >= 30) Cases[Phase].Duration = 0;
            };
        },
        [this, Voxel, PosesPath]
        {
            FFoundationLoadState Load;
            Voxel->GetLoadState(Load, false);
            Record(TEXT("views world ready"), Voxel->IsReady() && Load.bReady,
                FString::Printf(TEXT("seed %s; spawn feet %s; poses %s"), *Voxel->GetBuildReport().Left(120),
                    *Pawn->Simulation.Feet.ToCompactString(), *PosesPath));
        });
    // Save what the suite changes, then its defaults: no destination preview, the new outline, no high contrast, noon
    // with the clock paused, stone in the first hotbar slot, selected.
    AddCase(
        TEXT("views setup"), .3,
        [this, Voxel, Run, Authority, GetHighContrast, SetHighContrast, RunCommand, SetTime, ReadHotbar]
        {
            Run->SavedPreview = GetViewsCVar(ViewsPreviewCVar);
            Run->SavedOutline = GetViewsCVar(ViewsOutlineCVar);
            Run->bSavedHighContrast = GetHighContrast();
            Run->bSavedPaused = Voxel->GetClock().GetDayClock().bPaused;
            Run->SavedTotalTicks = Voxel->GetClock().GetDayClock().TotalTicks;
            Run->bSavedHotbar = ReadHotbar(Run->SavedHotbar);
            Run->SavedOverflowMessages = GetViewsCVar(ViewsOverflowCVar);
            Run->bSaved = true;
            if (!Run->SavedOverflowMessages.IsEmpty()) SetViewsCVar(ViewsOverflowCVar, TEXT("0"));
            SetViewsCVar(ViewsPreviewCVar, TEXT("0"));
            SetViewsCVar(ViewsOutlineCVar, TEXT("1"));
            SetHighContrast(false);
            SetTime(TEXT("noon"));
            RunCommand(TEXT("voxel.Time pause"));
            FFoundationAuthority& A = Authority();
            Pawn->Interaction.SetCreativeSlot(A, Pawn->Vitals, 0, TEXT("stone"), 64);
            Pawn->Interaction.SelectSlot(A, Pawn->Vitals, 0);
        },
        [this, Voxel, Run, Holding, GetHighContrast]
        {
            Record(TEXT("views setup"),
                !Run->SavedPreview.IsEmpty() && !Run->SavedOutline.IsEmpty() && Run->bSavedHotbar &&
                    GetViewsCVar(ViewsPreviewCVar) == TEXT("0") && GetViewsCVar(ViewsOutlineCVar) == TEXT("1") &&
                    !GetHighContrast() && Voxel->GetClock().GetTimeOfDay() == 6000 &&
                    Voxel->GetClock().GetDayClock().bPaused && Holding(TEXT("stone")),
                FString::Printf(TEXT("saved Preview %s, Outline %s, high contrast %s, clock paused %s, clock total "
                                     "%lld (time of day %lld), %s; now Preview %s, Outline %s, time of day %d, paused "
                                     "%s, holding stone %s"),
                    *Run->SavedPreview, *Run->SavedOutline, Run->bSavedHighContrast ? TEXT("on") : TEXT("off"),
                    Run->bSavedPaused ? TEXT("yes") : TEXT("no"), Run->SavedTotalTicks,
                    Run->SavedTotalTicks % FVoxelWorldClock::TicksPerDay,
                    Run->bSavedHotbar ? *Run->SavedHotbar.ToString() : TEXT("hotbar not found"),
                    *GetViewsCVar(ViewsPreviewCVar), *GetViewsCVar(ViewsOutlineCVar), Voxel->GetClock().GetTimeOfDay(),
                    Voxel->GetClock().GetDayClock().bPaused ? TEXT("yes") : TEXT("no"),
                    Holding(TEXT("stone")) ? TEXT("yes") : TEXT("no")));
        });
    // The last case: every switch, the preference, the clock (its total, then its pause) and the local player's first
    // hotbar slot and selection as they were, the slot and selection through the same creative-slot and select commands
    // the setup used. What the suite built stays in the world.
    auto Restore = [this, Voxel, Run, Authority, SetHighContrast, GetHighContrast, RunCommand, ReadHotbar]
    {
        AddCase(
            TEXT("views restore"), .3,
            [this, Voxel, Run, Authority, SetHighContrast, RunCommand]
            {
                if (!Run->bSaved) return;
                SetViewsCVar(ViewsPreviewCVar, Run->SavedPreview);
                SetViewsCVar(ViewsOutlineCVar, Run->SavedOutline);
                SetHighContrast(Run->bSavedHighContrast);
                if (!Run->SavedOverflowMessages.IsEmpty()) SetViewsCVar(ViewsOverflowCVar, Run->SavedOverflowMessages);
                // voxel.Time set takes at most MAX_int32 ticks; past that only the time of day goes back (forward).
                if (Run->SavedTotalTicks <= MAX_int32)
                    RunCommand(FString::Printf(TEXT("voxel.Time set %lld"), Run->SavedTotalTicks));
                else
                    RunCommand(FString::Printf(TEXT("voxel.Time add %lld"),
                        (Run->SavedTotalTicks - Voxel->GetClock().GetTimeOfDay()) % FVoxelWorldClock::TicksPerDay));
                Run->RestoredTotalTicks = Voxel->GetClock().GetDayClock().TotalTicks;
                if (!Run->bSavedPaused) RunCommand(TEXT("voxel.Time resume"));
                if (Run->bSavedHotbar)
                {
                    FFoundationAuthority& A = Authority();
                    const FViewsHotbar& Saved = Run->SavedHotbar;
                    Pawn->Interaction.SetCreativeSlot(A, Pawn->Vitals, 0, Saved.Item, Saved.Count, Saved.Damage);
                    Pawn->Interaction.SelectSlot(A, Pawn->Vitals, Saved.Selected);
                }
            },
            [this, Voxel, Run, GetHighContrast, ReadHotbar]
            {
                FViewsHotbar Hotbar;
                const bool bHotbar = ReadHotbar(Hotbar) && Run->bSavedHotbar && Hotbar == Run->SavedHotbar;
                const bool bExactTotal = Run->SavedTotalTicks <= MAX_int32;
                const bool bTime = bExactTotal ? Run->RestoredTotalTicks == Run->SavedTotalTicks
                                               : Run->RestoredTotalTicks % FVoxelWorldClock::TicksPerDay ==
                        Run->SavedTotalTicks % FVoxelWorldClock::TicksPerDay;
                Record(TEXT("views restored"),
                    Run->bSaved && GetViewsCVar(ViewsPreviewCVar) == Run->SavedPreview &&
                        GetViewsCVar(ViewsOutlineCVar) == Run->SavedOutline &&
                        GetHighContrast() == Run->bSavedHighContrast &&
                        Voxel->GetClock().GetDayClock().bPaused == Run->bSavedPaused && bTime && bHotbar,
                    FString::Printf(TEXT("Preview %s, Outline %s, high contrast %s, clock paused %s; clock total %lld "
                                         "set back (saved %lld, %s), time of day now %d; %s (saved %s). The placed "
                                         "target blocks, the grass under a plant, the torches and the plants cleared "
                                         "from the pads stay in the in-memory dev-seed world"),
                        *GetViewsCVar(ViewsPreviewCVar), *GetViewsCVar(ViewsOutlineCVar),
                        GetHighContrast() ? TEXT("on") : TEXT("off"),
                        Voxel->GetClock().GetDayClock().bPaused ? TEXT("yes") : TEXT("no"), Run->RestoredTotalTicks,
                        Run->SavedTotalTicks,
                        bExactTotal ? TEXT("exact") : TEXT("past voxel.Time set, time of day only"),
                        Voxel->GetClock().GetTimeOfDay(), *Hotbar.ToString(),
                        Run->bSavedHotbar ? *Run->SavedHotbar.ToString() : TEXT("not found")));
            });
    };

    // -FoundationViewsScout: candidate poses from the streamed columns around the spawn, as poses-file lines.
    if (bScout)
    {
        AddCase(
            TEXT("views scout"), .2,
            [this, Authority, ShotBase]
            {
                TArray<FString> Lines;
                const UVoxelWorldSubsystem* Streaming = UVoxelWorldSubsystem::Get(GetWorld());
                const FVoxelColumnPipeline* Pipeline = Streaming ? Streaming->GetPipeline() : nullptr;
                const FFoundationBlockWorld& W = Authority().World;
                const FFoundationBlockRegistry& Registry = W.GetRegistry();
                constexpr int32 Size = VoxelConstants::ColumnSize;
                auto Column = [Pipeline](int32 X, int32 Y, int32& LX, int32& LY) -> const FVoxelColumn*
                {
                    const FIntPoint C(FoundationGrid::FloorDivide(X, Size), FoundationGrid::FloorDivide(Y, Size));
                    LX = X - C.X * Size;
                    LY = Y - C.Y * Size;
                    return Pipeline ? Pipeline->FindColumn(C) : nullptr;
                };
                // -1: not loaded.
                auto StateAt = [&Column](int32 X, int32 Y, int32 Z)
                {
                    int32 LX, LY;
                    const FVoxelColumn* C = Column(X, Y, LX, LY);
                    return C ? int32(C->GetState(LX, LY, Z)) : -1;
                };
                auto Name = [&Registry](int32 State)
                { return State < 0 ? FString() : Registry.GetBlock({uint16(State)}).Id.ToString(); };
                auto IsFull = [&Registry](int32 State)
                { return State > 0 && Registry.GetShape({uint16(State)}) == EFoundationBlockShape::Full; };
                auto IsOpen = [&Name](int32 State)
                { return State == 0 || (State > 0 && Name(State) == TEXT("cave_air")); };
                // Air, or a plant the targets' builder breaks at once.
                auto IsClearable = [&Registry](int32 State)
                {
                    if (State == 0) return true;
                    if (State < 0) return false;
                    const FFoundationBlockType& Block = Registry.GetBlock({uint16(State)});
                    return Block.Hardness == 0 && !Block.IsFluid() &&
                        Registry.GetShape({uint16(State)}) != EFoundationBlockShape::Full;
                };
                const FIntVector Spawn = FoundationGrid::CellAt(Pawn->Simulation.Feet + FVector(0, 0, 1), W.OriginCm);
                {
                    int32 LX, LY;
                    const FVoxelColumn* C = Column(Spawn.X, Spawn.Y, LX, LY);
                    const int32 Ground = C ? C->GetHeight(EVoxelHeightmap::MotionBlocking, LX, LY) : 0;
                    Lines.Add(FString::Printf(TEXT("# spawn feet %s cell %d %d %d; motion-blocking ground z %d (%s)"),
                        *Pawn->Simulation.Feet.ToCompactString(), Spawn.X, Spawn.Y, Spawn.Z, Ground,
                        *Name(StateAt(Spawn.X, Spawn.Y, Ground))));
                }
                // The surface around the spawn: ground (motion blocking) height and kind per column.
                constexpr int32 R = 112, N = 2 * R + 1;
                constexpr int32 Loaded = 1, Tree = 2, Water = 4, Stand = 8, Grass = 16;
                TArray<int32> Ground, Surface;
                Surface.Init(MIN_int32, N * N);
                Ground.Init(MIN_int32, N * N);
                TArray<int32> Kind;
                Kind.Init(0, N * N);
                auto At = [](int32 DX, int32 DY) { return (DY + R) * N + DX + R; };
                for (int32 DY = -R; DY <= R; ++DY)
                    for (int32 DX = -R; DX <= R; ++DX)
                    {
                        int32 LX, LY;
                        const FVoxelColumn* C = Column(Spawn.X + DX, Spawn.Y + DY, LX, LY);
                        if (!C) continue;
                        const int32 Mb = C->GetHeight(EVoxelHeightmap::MotionBlocking, LX, LY);
                        const int32 Ws = C->GetHeight(EVoxelHeightmap::WorldSurface, LX, LY);
                        const int32 MbState = C->GetState(LX, LY, Mb);
                        const FString Top = Name(C->GetState(LX, LY, Ws)), Floor = Name(MbState);
                        int32 K = Loaded;
                        if (IsViewsTreeBlock(Top)) K |= Tree;
                        if (Floor == TEXT("water")) K |= Water;
                        if (IsFull(MbState) && !IsViewsTreeBlock(Floor) && Ws <= Mb + 2 &&
                            IsClearable(C->GetState(LX, LY, Mb + 1)) && IsClearable(C->GetState(LX, LY, Mb + 2)))
                            K |= Stand;
                        if (Floor == TEXT("grass_block")) K |= Grass;
                        Ground[At(DX, DY)] = Mb;
                        Surface[At(DX, DY)] = Ws;
                        Kind[At(DX, DY)] = K;
                    }
                // Summed-area tables: how many columns of a kind lie in a square.
                auto Table = [&Kind](int32 Bit)
                {
                    TArray<int32> S;
                    S.Init(0, (N + 1) * (N + 1));
                    for (int32 Y = 0; Y < N; ++Y)
                        for (int32 X = 0; X < N; ++X)
                            S[(Y + 1) * (N + 1) + X + 1] = (Kind[Y * N + X] & Bit ? 1 : 0) + S[Y * (N + 1) + X + 1] +
                                S[(Y + 1) * (N + 1) + X] - S[Y * (N + 1) + X];
                    return S;
                };
                auto Count = [](const TArray<int32>& S, int32 DX, int32 DY, int32 Half)
                {
                    const int32 X0 = FMath::Max(DX - Half + R, 0), X1 = FMath::Min(DX + Half + R, N - 1);
                    const int32 Y0 = FMath::Max(DY - Half + R, 0), Y1 = FMath::Min(DY + Half + R, N - 1);
                    return S[(Y1 + 1) * (N + 1) + X1 + 1] - S[Y0 * (N + 1) + X1 + 1] - S[(Y1 + 1) * (N + 1) + X0] +
                        S[Y0 * (N + 1) + X0];
                };
                // Yaw (degrees) from a column to the centroid of a kind's columns in a square around it.
                auto Toward = [&Kind, At](int32 DX, int32 DY, int32 Half, int32 Bit)
                {
                    FVector2D Sum = FVector2D::ZeroVector;
                    int32 Hits = 0;
                    for (int32 Y = FMath::Max(DY - Half, -R); Y <= FMath::Min(DY + Half, R); ++Y)
                        for (int32 X = FMath::Max(DX - Half, -R); X <= FMath::Min(DX + Half, R); ++X)
                            if (Kind[At(X, Y)] & Bit)
                            {
                                Sum += FVector2D(X - DX, Y - DY);
                                ++Hits;
                            }
                    return Hits ? FMath::RoundToDouble(FMath::RadiansToDegrees(FMath::Atan2(Sum.Y, Sum.X))) : 0.;
                };
                const TArray<int32> Trees = Table(Tree), Waters = Table(Water);
                auto Distance = [](int32 DX, int32 DY) { return FMath::Sqrt(double(DX * DX + DY * DY)); };

                // Day-open: the spawn, looking toward the lowest ground 16 to 96 blocks away (the widest view).
                {
                    double BestMean = 1e9;
                    int32 BestYaw = 0;
                    for (int32 Yaw = 0; Yaw < 360; Yaw += 45)
                    {
                        double Sum = 0;
                        int32 Samples = 0;
                        for (int32 Away = 16; Away <= 96; Away += 8)
                        {
                            const int32 DX = FMath::RoundToInt(Away * FMath::Cos(FMath::DegreesToRadians(Yaw)));
                            const int32 DY = FMath::RoundToInt(Away * FMath::Sin(FMath::DegreesToRadians(Yaw)));
                            if (FMath::Abs(DX) > R || FMath::Abs(DY) > R || !(Kind[At(DX, DY)] & Loaded)) continue;
                            Sum += Ground[At(DX, DY)];
                            ++Samples;
                        }
                        if (Samples && Sum / Samples < BestMean)
                        {
                            BestMean = Sum / Samples;
                            BestYaw = Yaw;
                        }
                    }
                    Lines.Add(
                        FString::Printf(TEXT("# pose day-open %d %d %d %d -8  # the spawn; mean ground %.1f that way"),
                            Spawn.X, Spawn.Y, Spawn.Z, BestYaw, BestMean));
                }
                // Day-open vista: the standing column (the best within 40, 64 and 96 of the spawn) whose view at eye
                // height stays clear the farthest (to 64) along one of 8 yaws: every column of a 3-wide corridor lower
                // than its eyes.
                {
                    TArray<TTuple<int32, double, int32, int32, int32>> Found;
                    for (int32 DY = -96; DY <= 96; ++DY)
                        for (int32 DX = -96; DX <= 96; ++DX)
                        {
                            if (!(Kind[At(DX, DY)] & Stand) || Distance(DX, DY) > 96) continue;
                            const int32 Eye = Ground[At(DX, DY)] + 2;
                            for (int32 Yaw = 0; Yaw < 360; Yaw += 45)
                            {
                                const double CX = FMath::Cos(FMath::DegreesToRadians(Yaw)),
                                             CY = FMath::Sin(FMath::DegreesToRadians(Yaw));
                                int32 Reach = 0;
                                bool bOpen = true;
                                while (Reach < 64 && bOpen)
                                {
                                    ++Reach;
                                    for (int32 Side = -1; Side <= 1 && bOpen; ++Side)
                                    {
                                        const int32 X = DX + FMath::RoundToInt(Reach * CX - Side * CY),
                                                    Y = DY + FMath::RoundToInt(Reach * CY + Side * CX);
                                        bOpen = FMath::Abs(X) <= R && FMath::Abs(Y) <= R && (Kind[At(X, Y)] & Loaded) &&
                                            Surface[At(X, Y)] < Eye;
                                    }
                                }
                                Reach -= bOpen ? 0 : 1;
                                Found.Emplace(Reach, Distance(DX, DY), DX, DY, Yaw);
                            }
                        }
                    Found.Sort(
                        [](const auto& A, const auto& B)
                        {
                            return A.template Get<0>() != B.template Get<0>()
                                ? A.template Get<0>() > B.template Get<0>()
                                : A.template Get<1>() < B.template Get<1>();
                        });
                    for (const double Band : {40., 64., 96.})
                    {
                        const int32 Index = Found.IndexOfByPredicate(
                            [Band](const auto& Entry) { return Entry.template Get<1>() <= Band; });
                        if (Index == INDEX_NONE) continue;
                        const int32 DX = Found[Index].Get<2>(), DY = Found[Index].Get<3>();
                        Lines.Add(FString::Printf(TEXT("pose day-open %d %d %d %d -5  # clear %d blocks, %.0f blocks "
                                                       "from spawn"),
                            Spawn.X + DX, Spawn.Y + DY, Ground[At(DX, DY)] + 1, Found[Index].Get<4>(),
                            Found[Index].Get<0>(), Found[Index].Get<1>()));
                    }
                }
                // Forest: a standing column clear of trunks (none within 1) with the most tree columns within 8.
                {
                    TArray<TTuple<int32, double, int32, int32>> Found;
                    for (int32 DY = -R + 10; DY <= R - 10; ++DY)
                        for (int32 DX = -R + 10; DX <= R - 10; ++DX)
                            if ((Kind[At(DX, DY)] & Stand) && Count(Trees, DX, DY, 1) == 0)
                                Found.Emplace(Count(Trees, DX, DY, 8), Distance(DX, DY), DX, DY);
                    Found.Sort(
                        [](const auto& A, const auto& B)
                        {
                            return A.template Get<0>() / 40 != B.template Get<0>() / 40
                                ? A.template Get<0>() > B.template Get<0>()
                                : A.template Get<1>() < B.template Get<1>();
                        });
                    for (int32 Index = 0; Index < FMath::Min(3, Found.Num()); ++Index)
                    {
                        const int32 DX = Found[Index].Get<2>(), DY = Found[Index].Get<3>();
                        Lines.Add(FString::Printf(TEXT("pose forest %d %d %d %d 5  # %d tree columns within 8, %.0f "
                                                       "blocks from spawn"),
                            Spawn.X + DX, Spawn.Y + DY, Ground[At(DX, DY)] + 1, int32(Toward(DX, DY, 10, Tree)),
                            Found[Index].Get<0>(), Found[Index].Get<1>()));
                    }
                    if (Found.IsEmpty()) Lines.Add(TEXT("# forest: no standing column found"));
                }
                // Water: a dry standing column by the water (some within 2) with water over a third of the square
                // within 10, at most 4 above sea level.
                {
                    TArray<TTuple<double, int32, int32, int32>> Found;
                    for (int32 DY = -R + 12; DY <= R - 12; ++DY)
                        for (int32 DX = -R + 12; DX <= R - 12; ++DX)
                            if ((Kind[At(DX, DY)] & Stand) && Ground[At(DX, DY)] <= VoxelConstants::SeaLevel + 3 &&
                                Count(Waters, DX, DY, 2) > 0 && Count(Waters, DX, DY, 10) >= 150)
                                Found.Emplace(Distance(DX, DY), Count(Waters, DX, DY, 10), DX, DY);
                    Found.Sort([](const auto& A, const auto& B) { return A.template Get<0>() < B.template Get<0>(); });
                    for (int32 Index = 0; Index < FMath::Min(3, Found.Num()); ++Index)
                    {
                        const int32 DX = Found[Index].Get<2>(), DY = Found[Index].Get<3>();
                        Lines.Add(FString::Printf(TEXT("pose water %d %d %d %d -12  # %d water columns within 10, "
                                                       "%.0f blocks from spawn"),
                            Spawn.X + DX, Spawn.Y + DY, Ground[At(DX, DY)] + 1, int32(Toward(DX, DY, 12, Water)),
                            Found[Index].Get<1>(), Found[Index].Get<0>()));
                    }
                    if (Found.IsEmpty()) Lines.Add(TEXT("# water: no shore found"));
                }
                // Day pad: 7 x 5 standing columns of one ground height, grass under the plant's cell (the far corner).
                {
                    TArray<TTuple<double, int32, int32>> Found;
                    for (int32 DY = -R; DY <= R - 4; ++DY)
                        for (int32 DX = -R; DX <= R - 6; ++DX)
                        {
                            const int32 Height = Ground[At(DX, DY)];
                            bool bFlat = (Kind[At(DX + 6, DY + 4)] & Grass) != 0 && Distance(DX, DY) >= 4;
                            for (int32 Y = DY; Y <= DY + 4 && bFlat; ++Y)
                                for (int32 X = DX; X <= DX + 6 && bFlat; ++X)
                                    bFlat = (Kind[At(X, Y)] & Stand) && Ground[At(X, Y)] == Height;
                            if (bFlat) Found.Emplace(Distance(DX + 3, DY + 2), DX, DY);
                        }
                    Found.Sort([](const auto& A, const auto& B) { return A.template Get<0>() < B.template Get<0>(); });
                    for (int32 Index = 0; Index < FMath::Min(3, Found.Num()); ++Index)
                    {
                        const int32 DX = Found[Index].Get<1>(), DY = Found[Index].Get<2>();
                        Lines.Add(FString::Printf(TEXT("pad day %d %d %d 2  # %.0f blocks from spawn, floor %s"),
                            Spawn.X + DX, Spawn.Y + DY + 4, Ground[At(DX, DY)] + 1, Found[Index].Get<0>(),
                            *Name(StateAt(Spawn.X + DX, Spawn.Y + DY, Ground[At(DX, DY)]))));
                    }
                    if (Found.IsEmpty()) Lines.Add(TEXT("# day pad: no flat 7 x 5 found"));
                }
                // Cave: a covered room (ground more than 5 above it) whose 7 x 5 floor of full blocks has 2 open cells
                // above every column (3 at the plant's corner), under a ceiling of 4 full blocks; lit when an open run
                // at eye height reaches a column under the sky within 16.
                {
                    TArray<TTuple<int32, double, FIntVector, FString>> Found;
                    int32 Pockets = 0;
                    const FIntPoint Directions[4] = {{1, 0}, {-1, 0}, {0, 1}, {0, -1}};
                    auto MotionBlocking = [&Column](int32 X, int32 Y)
                    {
                        int32 LX, LY;
                        const FVoxelColumn* C = Column(X, Y, LX, LY);
                        return C ? C->GetHeight(EVoxelHeightmap::MotionBlocking, LX, LY) : MIN_int32;
                    };
                    for (int32 DY = -64; DY <= 64; DY += 2)
                        for (int32 DX = -64; DX <= 64; DX += 2)
                        {
                            const int32 X = Spawn.X + DX, Y = Spawn.Y + DY;
                            const int32 Top = MotionBlocking(X, Y);
                            if (Top == MIN_int32) continue;
                            for (int32 Z = VoxelConstants::MinZ + 8; Z < Top - 6; ++Z)
                            {
                                if (!IsOpen(StateAt(X, Y, Z)) || !IsOpen(StateAt(X, Y, Z + 1)) ||
                                    !IsFull(StateAt(X, Y, Z - 1)))
                                    continue;
                                int32 Ceiling = Z + 2;
                                while (Ceiling < Z + 9 && IsOpen(StateAt(X, Y, Ceiling)))
                                    ++Ceiling;
                                bool bCovered = Ceiling < Z + 9;
                                for (int32 Up = 0; Up < 4 && bCovered; ++Up)
                                    bCovered = IsFull(StateAt(X, Y, Ceiling + Up));
                                if (!bCovered) continue;
                                ++Pockets;
                                bool bRoom = IsOpen(StateAt(X + 6, Y + 4, Z + 2));
                                for (int32 RY = Y; RY <= Y + 4 && bRoom; ++RY)
                                    for (int32 RX = X; RX <= X + 6 && bRoom; ++RX)
                                        bRoom = IsOpen(StateAt(RX, RY, Z)) && IsOpen(StateAt(RX, RY, Z + 1)) &&
                                            IsFull(StateAt(RX, RY, Z - 1)) && MotionBlocking(RX, RY) > Z + 5;
                                if (!bRoom) continue;
                                bool bLit = false;
                                for (const FIntPoint& Direction : Directions)
                                    for (int32 Step = 1; Step <= 16 && !bLit; ++Step)
                                    {
                                        const int32 SX = X + 3 + Direction.X * Step, SY = Y + Direction.Y * Step;
                                        if (!IsOpen(StateAt(SX, SY, Z + 1))) break;
                                        bLit = MotionBlocking(SX, SY) < Z + 1;
                                    }
                                // The general view looks along the longest open run from the stand (eye height).
                                int32 BestRun = -1, BestYaw = 90;
                                for (int32 Yaw = 0; Yaw < 360; Yaw += 45)
                                {
                                    const int32 SX = FMath::RoundToInt(FMath::Cos(FMath::DegreesToRadians(Yaw)));
                                    const int32 SY = FMath::RoundToInt(FMath::Sin(FMath::DegreesToRadians(Yaw)));
                                    int32 OpenRun = 0;
                                    while (OpenRun < 30 &&
                                        IsOpen(StateAt(X + 3 + SX * (OpenRun + 1), Y + SY * (OpenRun + 1), Z + 1)))
                                        ++OpenRun;
                                    if (OpenRun > BestRun)
                                    {
                                        BestRun = OpenRun;
                                        BestYaw = Yaw;
                                    }
                                }
                                Found.Emplace(bLit ? 0 : 1, Distance(DX, DY) + FMath::Abs(Z - Spawn.Z) * .5,
                                    FIntVector(X, Y, Z),
                                    FString::Printf(TEXT("pose cave %d %d %d %d -5  # %s, floor %s, ceiling %s at z "
                                                         "%d, run %d\npad cave %d %d %d 2"),
                                        X + 3, Y, Z, BestYaw, bLit ? TEXT("lit") : TEXT("dark"),
                                        *Name(StateAt(X, Y, Z - 1)), *Name(StateAt(X, Y, Ceiling)), Ceiling, BestRun, X,
                                        Y + 4, Z));
                                break;
                            }
                        }
                    Found.Sort(
                        [](const auto& A, const auto& B)
                        {
                            return A.template Get<0>() != B.template Get<0>()
                                ? A.template Get<0>() < B.template Get<0>()
                                : A.template Get<1>() < B.template Get<1>();
                        });
                    Lines.Add(FString::Printf(TEXT("# cave: %d covered pockets, %d rooms"), Pockets, Found.Num()));
                    int32 Lit = 0, Dark = 0;
                    for (const auto& Room : Found)
                        if ((Room.Get<0>() == 0 ? Lit : Dark)++ < 3) Lines.Add(Room.Get<3>());
                }
                FFileHelper::SaveStringToFile(
                    FString::Join(Lines, TEXT("\n")) + TEXT("\n"), *(ShotBase + TEXT("scout.txt")));
                for (const FString& Line : Lines)
                    UE_LOG(LogUnnamedFoundation, Display, TEXT("VIEWS SCOUT %s"), *Line);
                Record(TEXT("views scout"), Pipeline != nullptr, FString::Join(Lines, TEXT(" | ")));
            },
            [] {});
        Restore();
        return;
    }

    if (Poses.Errors.Num())
        AddCase(
            TEXT("views poses"), .1, [] {},
            [this, Poses, PosesPath]
            {
                Record(
                    TEXT("views poses file"), false, PosesPath + TEXT(": ") + FString::Join(Poses.Errors, TEXT("; ")));
            });
    auto FindPose = [this, &Poses](const TCHAR* Name) -> const FViewsPose*
    {
        const FViewsPose* Pose = Poses.Poses.Find(Name);
        if (!Pose)
            AddCase(
                FString(TEXT("views pose ")) + Name, .1, [] {},
                [this, PoseName = FString(Name)]
                {
                    Record(TEXT("views pose ") + PoseName, false, TEXT("not in the poses file; its shots are skipped"));
                });
        return Pose;
    };
    // A general view: holding stone at the pose (the feet within a block of its feet, grounded, the eyes' cell free of
    // collision), defaults, at the time of day given.
    auto General = [this, Shot, SetTime, Voxel, View, Holding, Describe, Authority, CellMin](
                       const FString& Name, const TCHAR* Time, int32 TimeOfDay, const FViewsPose& Pose)
    {
        Shot(
            Name, [SetTime, Time] { SetTime(Time); },
            [this, Voxel, View, Holding, Describe, Authority, CellMin, TimeOfDay, Pose](FString& Detail)
            {
                const FFoundationAuthority& A = Authority();
                const FVector Expected = CellMin(Pose.Cell) + FVector(50, 50, 0);
                const FVector Feet = Pawn->Simulation.Feet;
                const double Off = FVector::Dist(Feet, Expected);
                const FIntVector EyeCell =
                    FoundationGrid::CellAt(Feet + FVector(0, 0, Pawn->Simulation.EyeCm), A.World.OriginCm);
                const FFoundationBlockState EyeState = A.World.Get(EyeCell).State;
                TArray<FBox> Boxes;
                const bool bEyeSolid = A.World.GetCollisionBoxes(EyeCell, EyeState, Boxes);
                Detail = Describe() +
                    FString::Printf(TEXT("; pose feet %s, %.0f cm off, grounded %d, eye cell %s %s%s"),
                        *Expected.ToCompactString(), Off, int32(Pawn->Simulation.bGrounded), *EyeCell.ToString(),
                        *A.World.GetBlock(EyeState).Id.ToString(), bEyeSolid ? TEXT(" (solid)") : TEXT(""));
                const UFoundationInteractionView* Found = View();
                return Voxel->IsReady() && Holding(TEXT("stone")) && Found && Found->IsHeldItemVisible() &&
                    Voxel->GetClock().GetTimeOfDay() == TimeOfDay && GetViewsCVar(ViewsPreviewCVar) == TEXT("0") &&
                    GetViewsCVar(ViewsOutlineCVar) == TEXT("1") && Off <= 100 && Pawn->Simulation.bGrounded &&
                    !bEyeSolid;
            });
    };

    // 1. General views with the HUD: defaults, holding stone.
    if (const FViewsPose* Pose = FindPose(TEXT("day-open")))
    {
        Go(TEXT("day-open"), PoseStance(*Pose));
        General(TEXT("day-open-default"), TEXT("noon"), 6000, *Pose);
    }
    if (const FViewsPose* Pose = FindPose(TEXT("forest")))
    {
        Go(TEXT("forest"), PoseStance(*Pose));
        General(TEXT("forest-default"), TEXT("noon"), 6000, *Pose);
    }
    if (const FViewsPose* Pose = FindPose(TEXT("water")))
    {
        Go(TEXT("water"), PoseStance(*Pose));
        General(TEXT("water-default"), TEXT("noon"), 6000, *Pose);
    }

    // Options > Render Distance: the day-open view at 4 and 32 chunks, then the default 12, each set through the menu's
    // preferences as the slider sets it and shot once the new rings are on screen; then the run's own value again. A
    // radius set above game-override priority (the console, the command line) keeps its value, so its check is skipped.
    if (const FViewsPose* Pose = FindPose(TEXT("day-open")))
    {
        auto SetRenderDistance = [Preferences](int32 Chunks)
        {
            if (UMenuPreferences* Found = Preferences())
                Found->ApplyChanges(FString::Printf(TEXT("{\"settings\":{\"renderDistance\":%d}}"), Chunks));
        };
        auto ReadRenderDistance = [Preferences]
        {
            const TSharedPtr<const FJsonValue> Value =
                Preferences() ? Preferences()->FindPreference(TEXT("renderDistance")) : nullptr;
            return Value.IsValid() && Value->Type == EJson::Number ? FMath::RoundToInt32(Value->AsNumber()) : 0;
        };
        const TSharedRef<int32> SavedChunks = MakeShared<int32>(0);
        AddCase(
            TEXT("views render-distance save"), .1,
            [SavedChunks, ReadRenderDistance] { *SavedChunks = ReadRenderDistance(); }, [] {});
        for (const int32 Chunks : {4, 32, VoxelRenderDistance::DefaultChunks})
        {
            const FString Name = FString::Printf(TEXT("render-distance-%d"), Chunks);
            AddCase(
                TEXT("views ") + Name + TEXT(" set"), .2, [SetRenderDistance, Chunks] { SetRenderDistance(Chunks); },
                [this, Chunks, Name]
                {
                    const VoxelRenderDistance::FRadii Wanted = VoxelRenderDistance::FromChunks(Chunks);
                    IConsoleVariable* View = VoxelConsole::CVarViewRadius.AsVariable();
                    IConsoleVariable* Far = VoxelConsole::CVarFarRadius.AsVariable();
                    const bool bOverridden = (View->GetFlags() & ECVF_SetByMask) > ECVF_SetByGameOverride ||
                        (Far->GetFlags() & ECVF_SetByMask) > ECVF_SetByGameOverride;
                    Record(TEXT("views ") + Name + TEXT(" radii"),
                        bOverridden || (View->GetInt() == Wanted.ViewRadius && Far->GetInt() == Wanted.FarRadius),
                        FString::Printf(TEXT("voxel.ViewRadius %d, voxel.FarRadius %d (wanted %d, %d)%s"),
                            View->GetInt(), Far->GetInt(), Wanted.ViewRadius, Wanted.FarRadius,
                            bOverridden ? TEXT("; set above game-override priority, check skipped") : TEXT("")));
                });
            Go(Name, PoseStance(*Pose));
            General(Name, TEXT("noon"), 6000, *Pose);
        }
        AddCase(
            TEXT("views render-distance restore"), .2,
            [SetRenderDistance, SavedChunks]
            {
                if (*SavedChunks > 0) SetRenderDistance(*SavedChunks);
            },
            [this, ReadRenderDistance, SavedChunks]
            {
                Record(TEXT("views render-distance restored"), *SavedChunks > 0 && ReadRenderDistance() == *SavedChunks,
                    FString::Printf(TEXT("renderDistance %d (saved %d)"), ReadRenderDistance(), *SavedChunks));
            });
    }

    // The held item at FOV 70 and 110 under foundation.Hand.Fit 0 and 1: stone and an iron pickaxe, with the empty hand
    // as the plate their size is measured against; then the run's FOV, the fit and stone again.
    if (const FViewsPose* Pose = FindPose(TEXT("day-open")))
    {
        const TSharedRef<double> SavedFov = MakeShared<double>(0);
        auto ReadFov = [Preferences]
        {
            const TSharedPtr<const FJsonValue> Value =
                Preferences() ? Preferences()->FindPreference(TEXT("fov")) : nullptr;
            return Value.IsValid() && Value->Type == EJson::Number ? Value->AsNumber() : 0.;
        };
        auto SetFov = [Preferences](double Fov)
        {
            if (Preferences()) Preferences()->ApplyChanges(FString::Printf(TEXT("{\"settings\":{\"fov\":%g}}"), Fov));
        };
        Go(TEXT("held-fov"), PoseStance(*Pose));
        AddCase(TEXT("views held-fov save"), .1, [SavedFov, ReadFov] { *SavedFov = ReadFov(); }, [] {});
        for (const int32 Fov : {70, 110})
            for (const TCHAR* Item : {TEXT("empty"), TEXT("stone"), TEXT("iron_pickaxe")})
                for (const int32 Fit : {0, 1})
                {
                    const bool bEmpty = FName(Item) == TEXT("empty");
                    if (bEmpty && Fit == 1) continue;
                    Shot(
                        FString::Printf(TEXT("held-%s-fov%d-fit%d"), Item, Fov, Fit),
                        [this, Authority, RunCommand, SetFov, Fov, Item, Fit, bEmpty]
                        {
                            SetFov(Fov);
                            RunCommand(FString::Printf(TEXT("foundation.Hand.Fit %d"), Fit));
                            Pawn->Interaction.SetCreativeSlot(
                                Authority(), Pawn->Vitals, 0, bEmpty ? NAME_None : FName(Item), bEmpty ? 0 : 1);
                            Pawn->Interaction.SelectSlot(Authority(), Pawn->Vitals, 0);
                        },
                        [this, Authority, Holding, View, ReadFov, Fov, Item, Fit, bEmpty](FString& Detail)
                        {
                            const FFoundationPlayer* Player = Authority().FindPlayer(Pawn->Interaction.PlayerId);
                            const bool bHand = bEmpty
                                ? Player && Player->Inventory.GetHand(EFoundationHand::Main).IsEmpty()
                                : Holding(FName(Item));
                            const UFoundationInteractionView* Found = View();
                            Detail = FString::Printf(TEXT("fov %g, foundation.Hand.Fit %d, holding %s %d, drawn %d"),
                                ReadFov(), int32(FoundationHand::IsFitOn()), Item, int32(bHand),
                                int32(Found && Found->IsHeldItemVisible()));
                            return bHand && ReadFov() == Fov && FoundationHand::IsFitOn() == (Fit != 0) &&
                                (bEmpty || (Found && Found->IsHeldItemVisible()));
                        });
                }
        AddCase(
            TEXT("views held-fov restore"), .2,
            [this, Authority, RunCommand, SetFov, SavedFov]
            {
                if (*SavedFov > 0) SetFov(*SavedFov);
                RunCommand(TEXT("foundation.Hand.Fit 1"));
                Pawn->Interaction.SetCreativeSlot(Authority(), Pawn->Vitals, 0, TEXT("stone"), 64);
            },
            [this, ReadFov, SavedFov, Holding]
            {
                Record(TEXT("views held-fov restored"),
                    *SavedFov > 0 && ReadFov() == *SavedFov && FoundationHand::IsFitOn() && Holding(TEXT("stone")),
                    FString::Printf(TEXT("fov %g (saved %g)"), ReadFov(), *SavedFov));
            });
    }

    // Targets for the outline rows: the pad's cells are cleared of plants, then the row placed.
    const TArray<FViewsTarget> Targets = {{TEXT("stone"), TEXT("stone"), {1, 3}}, {TEXT("slab"), TEXT("oak_slab"), {2}},
        {TEXT("plant"), TEXT("dandelion"), {2}}};
    auto PlaceRow = [this, Run, Targets, Place, Clear, HasBlock, BlockId](const FString& PadName, const FViewsPad& Pad)
    {
        TSharedRef<FString> Notes = MakeShared<FString>();
        TSharedRef<bool> bPlaced = MakeShared<bool>(false);
        AddCase(
            TEXT("views targets ") + PadName, 1.5,
            [Run, Targets, Place, Clear, HasBlock, BlockId, PadName, Pad, Notes, bPlaced]
            {
                const int32 Width = (Targets.Num() - 1) * Pad.Spacing;
                int32 Cleared = 0;
                for (int32 Y = Pad.Cell.Y - 4; Y <= Pad.Cell.Y; ++Y)
                    for (int32 X = Pad.Cell.X; X <= Pad.Cell.X + Width; ++X)
                        for (int32 Z = Pad.Cell.Z; Z <= Pad.Cell.Z + 1; ++Z)
                            Cleared += Clear(FIntVector(X, Y, Z));
                TArray<FIntVector>& Cells = Run->Targets.FindOrAdd(PadName);
                Cells.Reset();
                *bPlaced = true;
                *Notes = FString::Printf(TEXT("%d plants cleared;"), Cleared);
                for (int32 Index = 0; Index < Targets.Num(); ++Index)
                {
                    FIntVector Cell = Pad.Cell + FIntVector(Index * Pad.Spacing, 0, 0);
                    const FName Below = BlockId(Cell - FIntVector(0, 0, 1));
                    // The plant stands on grass: the floor's own, or a grass block placed on the floor.
                    if (FName(Targets[Index].Item) == TEXT("dandelion") && Below != TEXT("grass_block") &&
                        Below != TEXT("dirt") && Below != TEXT("podzol"))
                    {
                        Place(Cell, TEXT("grass_block"));
                        Cell.Z += 1;
                    }
                    if (!HasBlock(Cell, Targets[Index].Item)) Place(Cell, Targets[Index].Item);
                    const bool bHere = HasBlock(Cell, Targets[Index].Item);
                    *bPlaced &= bHere;
                    Cells.Add(Cell);
                    *Notes += FString::Printf(TEXT(" %s at %s on %s%s;"), Targets[Index].Item, *Cell.ToString(),
                        *BlockId(Cell - FIntVector(0, 0, 1)).ToString(), bHere ? TEXT("") : TEXT(" (missing)"));
                }
            },
            [this, PadName, Notes, bPlaced] { Record(TEXT("views targets placed ") + PadName, *bPlaced, *Notes); });
    };
    // Where the player stands to see a target from Distance blocks (its near face to the player's centre), and where
    // it aims: the outline's centre across, 35% up its height (so a slab and a plant are hit).
    auto TargetStance = [Authority, CellMin](const FViewsPad& Pad, const FIntVector& Target, int32 Distance)
    {
        const FFoundationAuthority& A = Authority();
        TArray<FBox> Boxes;
        FBox Union(ForceInit);
        if (A.World.GetOutlineBoxes(Target, A.World.Get(Target).State, Boxes))
            for (const FBox& Box : Boxes)
                Union += Box;
        if (!Union.IsValid) Union = FoundationGrid::CellBox(Target, A.World.OriginCm);
        const FVector Aim(Union.GetCenter().X, Union.GetCenter().Y, Union.Min.Z + .35 * Union.GetSize().Z);
        const FVector Row = CellMin(FIntVector(Target.X, Pad.Cell.Y, Pad.Cell.Z));
        const FVector Feet(Row.X + 50, Row.Y + 50 - (Distance + .5) * 100, Row.Z);
        const FVector Eye = Feet + FVector(0, 0, GetDefault<UFoundationProfile>()->StandingEyesCm);
        return FViewsStance{Feet, (Aim - Eye).Rotation()};
    };
    // A pad's outline shots: each target from its distances, in the three outline variants.
    auto OutlineShots = [this, Run, Targets, Shot, TargetStance, SetHighContrast, View, Effects, Describe](
                            const FString& PadName, const FViewsPad& Pad)
    {
        struct FVariant
        {
            const TCHAR* Name;
            const TCHAR* Outline;
            bool bHighContrast;
        };
        const FVariant Variants[] = {{TEXT("outline-1"), TEXT("1"), false}, {TEXT("outline-2"), TEXT("2"), false},
            {TEXT("outline-1-hc"), TEXT("1"), true}};
        for (int32 Index = 0; Index < Targets.Num(); ++Index)
            for (const int32 Distance : Targets[Index].Distances)
                for (const FVariant& Variant : Variants)
                {
                    const FString Name =
                        FString::Printf(TEXT("%s-%s-%d-%s"), *PadName, Targets[Index].Name, Distance, Variant.Name);
                    Shot(
                        Name,
                        [this, Run, TargetStance, SetHighContrast, PadName, Pad, Index, Distance, Variant]
                        {
                            const TArray<FIntVector>* Cells = Run->Targets.Find(PadName);
                            if (Cells && Cells->IsValidIndex(Index))
                            {
                                const FViewsStance Stance = TargetStance(Pad, (*Cells)[Index], Distance);
                                ResetPlayer(Stance.Feet);
                                Controller->SetControlRotation(Stance.Look);
                            }
                            SetViewsCVar(ViewsOutlineCVar, Variant.Outline);
                            SetHighContrast(Variant.bHighContrast);
                        },
                        [this, Run, View, Effects, Describe, PadName, Index, Variant](FString& Detail)
                        {
                            Detail = Describe();
                            const TArray<FIntVector>* Cells = Run->Targets.Find(PadName);
                            if (!Cells || !Cells->IsValidIndex(Index))
                            {
                                Detail += TEXT("; the target was not placed");
                                return false;
                            }
                            const FIntVector Target = (*Cells)[Index];
                            Detail += TEXT("; intended ") + Target.ToString();
                            const FFoundationHit& Hit = Pawn->Interaction.Preview.Target;
                            const UFoundationInteractionView* Found = View();
                            const FFoundationBreakEffects* Break = Effects();
                            return Hit.Kind == EFoundationHitKind::Block && Hit.Cell == Target && Hit.bInRange &&
                                Found && Found->IsOutlineVisible() && Break && Break->IsOutlineVisible() &&
                                Break->IsHighContrastOutline() == Variant.bHighContrast &&
                                GetViewsCVar(ViewsOutlineCVar) == Variant.Outline;
                        });
                }
        // The defaults again for whatever follows.
        AddCase(
            TEXT("views defaults ") + PadName, .1,
            [SetHighContrast]
            {
                SetViewsCVar(ViewsOutlineCVar, TEXT("1"));
                SetHighContrast(false);
            },
            [] {});
    };

    // 2 and 3 by day: the pad's row, the placement preview pair (aiming at the floor 2 blocks ahead of the player, on
    // the row's -Y side), then the outlines.
    if (const FViewsPad* Pad = Poses.Pads.Find(TEXT("day")))
    {
        const FViewsPad Day = *Pad;
        const FIntVector Stand(Day.Cell.X + 1, Day.Cell.Y - 4, Day.Cell.Z);
        const FIntVector Floor(Day.Cell.X + 1, Day.Cell.Y - 2, Day.Cell.Z - 1);
        auto PreviewStance = [CellMin, Stand, Floor]
        {
            const FVector Feet = CellMin(Stand) + FVector(50, 50, 0);
            const FVector Eye = Feet + FVector(0, 0, GetDefault<UFoundationProfile>()->StandingEyesCm);
            return FViewsStance{Feet, (CellMin(Floor) + FVector(50, 50, 100) - Eye).Rotation()};
        };
        Go(TEXT("day pad"), PreviewStance);
        PlaceRow(TEXT("day"), Day);
        for (const int32 Mode : {0, 1})
            Shot(
                FString::Printf(TEXT("place-preview-%d"), Mode),
                [this, Mode, PreviewStance, SetTime]
                {
                    SetTime(TEXT("noon"));
                    const FViewsStance Stance = PreviewStance();
                    ResetPlayer(Stance.Feet);
                    Controller->SetControlRotation(Stance.Look);
                    SetViewsCVar(ViewsPreviewCVar, FString::FromInt(Mode));
                },
                [this, Mode, View, Describe, Holding, Floor](FString& Detail)
                {
                    const FFoundationPreview& Preview = Pawn->Interaction.Preview;
                    const FIntVector Expected = Floor + FIntVector(0, 0, 1);
                    Detail = Describe() +
                        FString::Printf(TEXT("; preview cell %s (expected %s), placement %d, valid %d"),
                            *Preview.Cell.ToString(), *Expected.ToString(), int32(Preview.bPlacement),
                            int32(Preview.bValid));
                    const UFoundationInteractionView* Found = View();
                    return Holding(TEXT("stone")) && Preview.Target.Cell == Floor && Preview.bPlacement &&
                        Preview.bValid && Preview.Cell == Expected && Found &&
                        Found->IsPreviewVisible() == (Mode == 1) &&
                        GetViewsCVar(ViewsPreviewCVar) == FString::FromInt(Mode);
                });
        AddCase(TEXT("views preview off"), .1, [] { SetViewsCVar(ViewsPreviewCVar, TEXT("0")); }, [] {});
        OutlineShots(TEXT("day"), Day);
    }
    else
        AddCase(
            TEXT("views pad day"), .1, [] {},
            [this] { Record(TEXT("views pad day"), false, TEXT("not in the poses file; its shots are skipped")); });

    // The cave: its general view, its row's outlines, then (an extra) the cave at midnight by a torch.
    if (const FViewsPose* Pose = FindPose(TEXT("cave")))
    {
        Go(TEXT("cave"), PoseStance(*Pose));
        General(TEXT("cave-default"), TEXT("noon"), 6000, *Pose);
        if (const FViewsPad* Pad = Poses.Pads.Find(TEXT("cave")))
        {
            PlaceRow(TEXT("cave"), *Pad);
            OutlineShots(TEXT("cave"), *Pad);
        }
        else
            AddCase(
                TEXT("views pad cave"), .1, [] {}, [this]
                { Record(TEXT("views pad cave"), false, TEXT("not in the poses file; its shots are skipped")); });
        if (Poses.Torches.Num())
        {
            const TArray<FIntVector> Torches = Poses.Torches;
            const FViewsPose CavePose = *Pose;
            AddCase(
                TEXT("views torches"), 1.5,
                [this, Torches, Place, HasBlock, SetTime, CavePose, PoseStance]
                {
                    SetTime(TEXT("midnight"));
                    for (const FIntVector& Cell : Torches)
                        if (!HasBlock(Cell, TEXT("torch"))) Place(Cell, TEXT("torch"));
                    const FViewsStance Stance = PoseStance(CavePose)();
                    ResetPlayer(Stance.Feet);
                    Controller->SetControlRotation(Stance.Look);
                },
                [this, Torches, HasBlock]
                {
                    int32 Placed = 0;
                    for (const FIntVector& Cell : Torches)
                        Placed += HasBlock(Cell, TEXT("torch")) ? 1 : 0;
                    Record(TEXT("views torches placed"), Placed == Torches.Num(),
                        FString::Printf(TEXT("%d of %d torches"), Placed, Torches.Num()));
                });
            General(TEXT("cave-torch"), TEXT("midnight"), 18000, CavePose);
        }
    }
    Restore();
#endif
}
