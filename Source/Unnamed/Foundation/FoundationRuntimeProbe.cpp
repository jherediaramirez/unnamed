#include "Foundation/FoundationRuntimeProbe.h"
#include "Foundation/FoundationLab.h"
#include "Foundation/FoundationPawn.h"
#include "Foundation/FoundationController.h"
#include "Menu/MenuInputBridge.h"
#include "Input/UnnamedInputSubsystem.h"
#include "Input/UnnamedInputSettings.h"
#include "Engine/LocalPlayer.h"
#include "Engine/World.h"
#include "Camera/CameraComponent.h"
#include "InputKeyEventArgs.h"
#include "HAL/IConsoleManager.h"
#include "HAL/FileManager.h"
#include "HAL/PlatformMisc.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "Misc/Paths.h"
#include "Misc/FileHelper.h"
#include "Dom/JsonObject.h"
#include "Serialization/JsonSerializer.h"
#include "UnrealClient.h"

UFoundationRuntimeProbe::UFoundationRuntimeProbe()
{
    PrimaryComponentTick.bCanEverTick = true;
    PrimaryComponentTick.TickGroup = TG_PostUpdateWork;
}

void UFoundationRuntimeProbe::Key(FKey K, bool Down)
{
    if (Frames < DeferKeysUntilFrame)
    {
        QueuedKeys.Add({K, Down});
        return;
    }
    if (Down)
        DownKeys.AddUnique(K);
    else
        DownKeys.Remove(K);
    PC->Diagnostics->RouteKey(K, Down);
}

void UFoundationRuntimeProbe::Reset(FVector P)
{
    DeferKeysUntilFrame = 0;
    QueuedKeys.Reset();
    auto Keys = DownKeys;
    for (FKey K : Keys)
        Key(K, false);
    PC->Diagnostics->Input()->ClearIntent();
    Pawn->Simulation.Reset(P);
    Pawn->Simulation.bGrounded = true;
    Pawn->SetActorLocation(P);
    PC->SetControlRotation(FRotator::ZeroRotator);
    DeferKeysUntilFrame = Frames + 2;
}

void UFoundationRuntimeProbe::Record(FString Name, bool Pass, FString Detail)
{
    auto R = MakeShared<FJsonObject>();
    R->SetStringField(TEXT("scenario"), Name);
    R->SetBoolField(TEXT("pass"), Pass);
    R->SetStringField(TEXT("detail"), Detail);
    Results.Add(MakeShared<FJsonValueObject>(R));
    if (!Pass)
    {
        ++Failures;
        R->SetStringField(TEXT("runtime_snapshot"), PC->Diagnostics->Snapshot());
    }
    UE_LOG(LogTemp, Display, TEXT("FOUNDATION CHECK %s: %s | %s"), Pass ? TEXT("PASS") : TEXT("FAIL"), *Name, *Detail);
}

void UFoundationRuntimeProbe::Measure(FString Name, double Actual, double Expected, double Tolerance)
{
    Record(Name, FMath::Abs(Actual - Expected) <= Tolerance,
        FString::Printf(TEXT("actual=%.8f expected=%.8f tolerance=%.8f"), Actual, Expected, Tolerance));
}

void UFoundationRuntimeProbe::Add(FString Name, double Duration, TFunction<void()> Start, TFunction<void()> End)
{
    Cases.Add({MoveTemp(Name), Duration, MoveTemp(Start), MoveTemp(End)});
}

void UFoundationRuntimeProbe::BeginPlay()
{
    Super::BeginPlay();
    Pawn = Cast<AFoundationPawn>(GetOwner());
    PC = Pawn ? Cast<AFoundationController>(Pawn->GetController()) : nullptr;
    if (!Pawn || !PC || !FParse::Value(FCommandLine::Get(), TEXT("FoundationProbe="), Output))
    {
        SetComponentTickEnabled(false);
        return;
    }
    FParse::Value(FCommandLine::Get(), TEXT("FoundationFPS="), RequestedFPS);
    IConsoleManager::Get().FindConsoleVariable(TEXT("r.VSync"))->Set(0, ECVF_SetByCode);
    IConsoleManager::Get().FindConsoleVariable(TEXT("t.MaxFPS"))->Set(RequestedFPS, ECVF_SetByCode);
    Bridge = NewObject<UMenuInputBridge>(this);
    Bridge->Initialize(PC->Diagnostics->Input());
    if (FParse::Param(FCommandLine::Get(), TEXT("FoundationMenuCycle")))
    {
        static int32 Sequence = 0;
        Output = FPaths::ChangeExtension(Output, FString::Printf(TEXT("%d.json"), ++Sequence));
        Add(
            TEXT("menu possession"), .5, [] {},
            [this]
            {
                Record(TEXT("menu selected Foundation map and pawn"),
                    PC->GetPawn() == Pawn && Pawn->GetLab() != nullptr, GetWorld()->GetMapName());
            });
        Add(
            TEXT("menu-bound movement"), .3,
            [this] { Key(PC->Diagnostics->Input()->CurrentKey(TEXT("key.forward")), true); },
            [this]
            {
                Record(
                    TEXT("menu binding drives actual pawn"), Pawn->Simulation.Feet.X > 20, PC->Diagnostics->Snapshot());
                Key(PC->Diagnostics->Input()->CurrentKey(TEXT("key.forward")), false);
            });
        Add(
            TEXT("menu screenshot"), .5, [] {}, [this]
            { FScreenshotRequest::RequestScreenshot(FPaths::ChangeExtension(Output, TEXT("png")), true, false); });
        Add(TEXT("menu screenshot flush"), .3, [] {}, [] {});
        TotalStarted = Started = FPlatformTime::Seconds();
        return;
    }
    FString Persistence;
    FParse::Value(FCommandLine::Get(), TEXT("FoundationPersistence="), Persistence);
    if (!Persistence.IsEmpty())
    {
        Add(
            TEXT("persistence"), 1,
            [this, Persistence]
            {
                if (Persistence == TEXT("write"))
                {
                    Bridge->ResetBindings(true);
                    Bridge->ApplyBinding(TEXT("key.forward"), TEXT("K"));
                    Bridge->ApplyBinding(TEXT("key.jump"), TEXT("Enter"));
                    Bridge->ApplyBinding(TEXT("key.sprint"), TEXT("RightControl"));
                    Bridge->ApplyBinding(TEXT("key.sneak"), TEXT("RightShift"));
                    Bridge->ApplyBinding(TEXT("key.attack"), TEXT("ThumbMouseButton"));
                    Bridge->ApplyPolicy(TEXT("toggleCrouch"), true);
                    auto* S = PC->Diagnostics->Input()->Settings();
                    S->DebugTextScale = 1.4f;
                    FString E;
                    Record(TEXT("native SaveGame write"), S->SaveChecked(E), E);
                }
            },
            [this]
            {
                auto* I = PC->Diagnostics->Input();
                Record(TEXT("five bindings persisted"),
                    I->CurrentKey(TEXT("key.forward")) == EKeys::K && I->CurrentKey(TEXT("key.jump")) == EKeys::Enter &&
                        I->CurrentKey(TEXT("key.sprint")) == EKeys::RightControl &&
                        I->CurrentKey(TEXT("key.sneak")) == EKeys::RightShift &&
                        I->CurrentKey(TEXT("key.attack")) == EKeys::ThumbMouseButton,
                    Bridge->GetState());
                Record(TEXT("policy and debug scale persisted"),
                    I->Settings()->bToggleCrouch && FMath::IsNearlyEqual(I->Settings()->DebugTextScale, 1.4f),
                    TEXT("native SaveGame profile readback"));
            });
        TotalStarted = Started = FPlatformTime::Seconds();
        return;
    }
    Bridge->ResetBindings(true);
    Add(
        TEXT("startup"), 1, [] {},
        [this]
        {
            Record(TEXT("possession and geometry"),
                PC->GetPawn() == Pawn && Pawn->GetLab() && Pawn->GetLab()->CollisionBoxes.Num() > 20,
                TEXT("real game world, controller, pawn and native primitives"));
            Measure(TEXT("standing height cm"), Pawn->Simulation.HeightCm, 180, .1);
            Measure(TEXT("standing eyes cm"), Pawn->EyePosition().Z - Pawn->Simulation.Feet.Z, 162, .1);
        });
    Add(
        TEXT("walk"), 2,
        [this]
        {
            Reset();
            Key(EKeys::W, true);
        },
        [this]
        { Measure(TEXT("walk blocks/s"), Pawn->Simulation.LastDisplacement.Size2D() / 5., 4.3171806, .0431718); });
    Add(
        TEXT("stop"), 1, [this] { Key(EKeys::W, false); },
        [this] { Measure(TEXT("stopped speed"), Pawn->Simulation.LastDisplacement.Size2D(), 0, .001); });
    Add(
        TEXT("sprint"), 2,
        [this]
        {
            Reset();
            Key(EKeys::W, true);
            Key(EKeys::LeftShift, true);
        },
        [this]
        {
            Measure(TEXT("sprint blocks/s"), Pawn->Simulation.LastDisplacement.Size2D() / 5., 5.6123348, .0561233);
            Record(TEXT("Shift sprints without crouching"), Pawn->Simulation.bSprinting && !Pawn->Simulation.bCrouched,
                TEXT("project personal default LeftShift"));
        });
    Add(
        TEXT("sprint release"), .2, [this] { Key(EKeys::LeftShift, false); },
        [this]
        {
            Record(TEXT("sprint release latch"), Pawn->Simulation.bSprinting,
                TEXT("continues until forward/eligibility/collision ends it"));
        });
    Add(
        TEXT("sneak"), 2,
        [this]
        {
            Reset();
            Key(EKeys::W, true);
            Key(EKeys::C, true);
        },
        [this]
        {
            Measure(TEXT("sneak blocks/s"), Pawn->Simulation.LastDisplacement.Size2D() / 5., 1.2951542, .0129515);
            Measure(TEXT("crouch height cm"), Pawn->Simulation.HeightCm, 150, .1);
            Measure(TEXT("crouch eye cm"), Pawn->Simulation.EyeCm, 127, .1);
            Record(TEXT("C requests crouch"), Pawn->LastIntent.bSneak && Pawn->Simulation.bCrouched,
                TEXT("project personal default C"));
        });
    Add(
        TEXT("old sprint key"), .3,
        [this]
        {
            Reset();
            Key(EKeys::W, true);
            Key(EKeys::LeftControl, true);
        },
        [this]
        {
            Record(TEXT("Ctrl no longer requests sprint"), !Pawn->LastIntent.bSprint && !Pawn->Simulation.bSprinting,
                TEXT("fixed Ctrl stack-drop modifier is unchanged"));
        });
    Add(
        TEXT("jump"), .8,
        [this]
        {
            Reset();
            Apex = 0;
            LaunchTick = LandTick = -1;
            Key(EKeys::SpaceBar, true);
        },
        [this]
        {
            Measure(TEXT("jump apex blocks"), Apex / 100., 1.25220334, .02);
            Measure(TEXT("jump airtime ticks"), double(LandTick - LaunchTick + 1), 12, 1);
        });
    Add(
        TEXT("diagonal"), 2,
        [this]
        {
            Reset({-1800, 100, 0});
            Key(EKeys::W, true);
            Key(EKeys::D, true);
        },
        [this]
        { Measure(TEXT("diagonal blocks/s"), Pawn->Simulation.LastDisplacement.Size2D() / 5., 4.40528634, .0440529); });
    Add(
        TEXT("opposing"), .3,
        [this]
        {
            Reset();
            Key(EKeys::W, true);
            Key(EKeys::S, true);
        },
        [this] { Measure(TEXT("opposing keys displacement"), Pawn->Simulation.Feet.Size(), 0, .001); });
    Add(
        TEXT("rebind old"), .3,
        [this]
        {
            Reset();
            Bridge->ApplyBinding(TEXT("key.forward"), TEXT("K"));
            Key(EKeys::W, true);
        },
        [this] { Measure(TEXT("removed W binding"), Pawn->Simulation.Feet.Size(), 0, .001); });
    Add(
        TEXT("rebind new"), .15,
        [this]
        {
            Key(EKeys::W, false);
            Key(EKeys::K, true);
        },
        [this]
        { Record(TEXT("new K binding moves"), Pawn->Simulation.Feet.X > 10, TEXT("native profile forward=K")); });
    Add(TEXT("forward tap gap"), .1, [this] { Key(EKeys::K, false); }, [] {});
    Add(
        TEXT("forward double tap"), .15, [this] { Key(EKeys::K, true); },
        [this]
        {
            Record(TEXT("double tap follows remapped forward"), Pawn->Simulation.bSprinting,
                TEXT("K tap-release-K inside7 reference ticks"));
        });
    Add(
        TEXT("blocked stand"), .3,
        [this]
        {
            Reset({400, 400, 0});
            Key(EKeys::C, true);
        },
        [this]
        {
            Record(TEXT("low ceiling crouch"), Pawn->Simulation.bCrouched && !Pawn->Simulation.bHeadroom,
                TEXT("155cm ceiling"));
        });
    Add(
        TEXT("blocked uncrouch"), .2, [this] { Key(EKeys::C, false); },
        [this]
        {
            Record(TEXT("release cannot stand through ceiling"),
                Pawn->Simulation.bCrouched && !Pawn->Simulation.bHeadroom,
                TEXT("resolved pose survives released intent"));
        });
    Add(
        TEXT("mouse"), .15,
        [this]
        {
            Reset();
            PC->InputKey(FInputKeyEventArgs(nullptr, FInputDeviceId::CreateFromInternalId(0), EKeys::MouseX, 100.f,
                1.f / RequestedFPS, 1, FPlatformTime::Cycles64()));
        },
        [this] { Measure(TEXT("100 mouse counts yaw degrees"), PC->GetControlRotation().Yaw, 15, .05); });
    Add(
        TEXT("pause held"), .25,
        [this]
        {
            Reset();
            Key(EKeys::K, true);
            Key(EKeys::Escape, true);
            Key(EKeys::Escape, false);
        },
        [this]
        {
            Record(TEXT("pause removes contexts"), !PC->Diagnostics->Input()->IsGameplayEnabled() && PC->IsMenuOpen(),
                TEXT("existing CEF menu opened; no sim movement"));
        });
    Add(
        TEXT("release while paused"), .15,
        [this]
        {
            Key(EKeys::K, false);
            PC->ResumeFoundation();
        },
        [this]
        {
            Record(TEXT("resume clears stale movement"), Pawn->LastIntent.Move.IsNearlyZero(),
                TEXT("release happened in UIOnly context"));
        });
    Add(
        TEXT("text"), .15,
        [this]
        {
            PC->Diagnostics->OpenText(false);
            Key(EKeys::K, true);
        },
        [this]
        {
            Record(TEXT("text does not move"), !PC->Diagnostics->IsGameplay() && Pawn->LastIntent.Move.IsNearlyZero(),
                TEXT("native editable text focus; no chat backend"));
            Key(EKeys::K, false);
            PC->Diagnostics->ClosePanel();
        });
    Add(
        TEXT("debug chord"), .15,
        [this]
        {
            Bridge->ApplyBinding(TEXT("key.togglePerspective"), TEXT("F6"));
            PC->Diagnostics->bOverlay = false;
            Key(EKeys::F3, true);
            Key(EKeys::F6, true);
            Key(EKeys::F6, false);
            Key(EKeys::F3, false);
        },
        [this]
        {
            Record(TEXT("F3+F6 options and no perspective"),
                PC->Diagnostics->bPanel && PC->Diagnostics->bOverlay && Pawn->GetPerspective() == 0,
                TEXT("perspective rebound to F6; chord consumes it"));
            PC->Diagnostics->ClosePanel();
        });
    Add(
        TEXT("F3 alone"), .15,
        [this]
        {
            Key(EKeys::F3, true);
            Key(EKeys::F3, false);
        },
        [this] { Record(TEXT("F3 alone toggles"), !PC->Diagnostics->bOverlay, TEXT("toggle on release")); });
    Add(
        TEXT("F3 C sharing"), .15,
        [this]
        {
            Reset();
            Key(EKeys::F3, true);
            Key(EKeys::C, true);
        },
        [this]
        {
            Record(TEXT("F3+C does not also crouch"), !Pawn->LastIntent.bSneak && !Pawn->Simulation.bCrouched,
                TEXT("debug chord consumes shared C binding"));
            Key(EKeys::C, false);
            Key(EKeys::F3, false);
        });
    Add(
        TEXT("HUD"), .15, [this] { Key(EKeys::F1, true); },
        [this]
        {
            Record(TEXT("HUD hides via action"), PC->Diagnostics->bHideHud, TEXT("Enhanced Input F1 action"));
            Key(EKeys::F1, false);
            PC->Diagnostics->bHideHud = false;
        });
    Add(
        TEXT("capture"), .15,
        [this]
        {
            PC->Diagnostics->SetCapture(true);
            Key(EKeys::K, true);
        },
        [this]
        {
            Record(TEXT("capture consumes gameplay intent"), Pawn->LastIntent.Move.IsNearlyZero(),
                TEXT("capture input never reaches Enhanced Input"));
            PC->Diagnostics->SetCapture(false);
            Key(EKeys::K, false);
        });
    Add(
        TEXT("capture F3"), .15,
        [this]
        {
            PC->Diagnostics->bOverlay = false;
            PC->Diagnostics->SetCapture(true);
            Key(EKeys::F3, true);
            PC->Diagnostics->SetCapture(false);
            Key(EKeys::F3, false);
        },
        [this]
        {
            Record(TEXT("capture F3 release cannot toggle overlay"), !PC->Diagnostics->bOverlay,
                TEXT("captured press has no standalone release action"));
        });
    Add(
        TEXT("inventory close"), .15,
        [this]
        {
            PC->Diagnostics->OpenInventory();
            Key(EKeys::E, true);
            Key(EKeys::E, false);
        },
        [this]
        {
            Record(TEXT("logical inventory binding closes fixture"),
                !PC->Diagnostics->bPanel && PC->Diagnostics->IsGameplay(),
                TEXT("same configurable E binding in inventory context"));
        });
    Add(
        TEXT("focus"), .15,
        [this]
        {
            Key(EKeys::K, true);
            PC->Diagnostics->FocusChanged(false);
            Key(EKeys::K, false);
        },
        [this]
        {
            Record(TEXT("focus loss cancels"), PC->IsMenuOpen() && Pawn->LastIntent.Move.IsNearlyZero(),
                TEXT("application activation handler invoked, not physical OS focus test"));
            PC->Diagnostics->FocusChanged(true);
            PC->ResumeFoundation();
        });
    Add(
        TEXT("wheel binding"), .25,
        [this]
        {
            Reset();
            Bridge->ApplyBinding(TEXT("key.jump"), TEXT("MouseScrollUp"));
            PC->Diagnostics->RouteWheel(1);
        },
        [this]
        {
            Record(TEXT("wheel pulse reaches rebound jump"), Pawn->Simulation.Feet.Z > 30,
                TEXT("Enhanced Input pulse survives one reference step"));
        });
    Add(
        TEXT("action target"), .15,
        [this]
        {
            Reset({250, 1050, 0});
            Key(EKeys::LeftMouseButton, true);
        },
        [this]
        {
            Record(TEXT("native attack reaches fixture"), PC->Diagnostics->Actions.bBreaking,
                TEXT("eye ray at block fixture2.5m"));
        });
    Add(
        TEXT("action cancel"), .15,
        [this]
        {
            Key(EKeys::Escape, true);
            Key(EKeys::Escape, false);
            Key(EKeys::LeftMouseButton, false);
        },
        [this]
        {
            Record(TEXT("pause cancels attack/use"),
                !PC->Diagnostics->Actions.bBreaking && !PC->Diagnostics->Actions.bUsing, TEXT("context removal"));
            PC->ResumeFoundation();
        });
    Add(
        TEXT("menu cycles"), .15,
        [this]
        {
            for (int32 I = 0; I < 4; ++I)
            {
                PC->PauseFoundation();
                PC->ResumeFoundation();
            }
            Bridge->ResetBindings(true);
        },
        [this]
        {
            Record(TEXT("repeated pause/resume stays playable"),
                PC->Diagnostics->IsGameplay() && PC->Diagnostics->Input()->IsGameplayEnabled(),
                TEXT("4 native browser/context trips"));
        });
    Add(
        TEXT("screenshot"), 1,
        [this]
        {
            Reset({-100, -850, 0});
            PC->SetControlRotation(FRotator(-10, 35, 0));
            PC->Diagnostics->bOverlay = true;
            PC->Diagnostics->bShowCollision = true;
        },
        [this] { FScreenshotRequest::RequestScreenshot(FPaths::ChangeExtension(Output, TEXT("png")), true, false); });
    Add(TEXT("capture flush"), 1, [] {}, [] {});
    TotalStarted = Started = FPlatformTime::Seconds();
}

void UFoundationRuntimeProbe::TickComponent(float Delta, ELevelTick Type, FActorComponentTickFunction* Function)
{
    Super::TickComponent(Delta, Type, Function);
    if (bDone || Cases.IsEmpty()) return;
    const double Now = FPlatformTime::Seconds();
    ++Frames;
    if (QueuedKeys.Num() && Frames >= DeferKeysUntilFrame)
    {
        auto Pending = MoveTemp(QueuedKeys);
        QueuedKeys.Reset();
        for (const auto& Pair : Pending)
            Key(Pair.Key, Pair.Value);
    }
    if (Phase >= 0 && Cases[Phase].Name == TEXT("jump"))
    {
        Apex = FMath::Max(Apex, Pawn->Simulation.Feet.Z);
        if (LaunchTick < 0 && Pawn->Simulation.Feet.Z > 0) LaunchTick = Pawn->Simulation.Steps;
        if (LaunchTick >= 0 && LandTick < 0 && Pawn->Simulation.bGrounded)
        {
            LandTick = Pawn->Simulation.Steps;
            Key(EKeys::SpaceBar, false);
        }
    }
    if (Phase >= 0 && Now - PhaseStarted < Cases[Phase].Duration) return;
    if (Phase >= 0)
    {
        auto Rate = MakeShared<FJsonObject>();
        Rate->SetStringField(TEXT("phase"), Cases[Phase].Name);
        Rate->SetNumberField(TEXT("fps"), (Frames - PhaseStartFrame) / (Now - PhaseStarted));
        PhaseRates.Add(MakeShared<FJsonValueObject>(Rate));
        Cases[Phase].End();
    }
    if (++Phase >= Cases.Num())
    {
        Finish();
        return;
    }
    PhaseStarted = Now;
    PhaseStartFrame = Frames;
    Cases[Phase].Start();
}

void UFoundationRuntimeProbe::Finish()
{
    bDone = true;
    auto Root = MakeShared<FJsonObject>();
    Root->SetStringField(TEXT("evidence"),
        TEXT("real Unreal game world and Enhanced Input; simulated application input, not native hardware input; "
             "expected values source-derived from exact Java26.3"));
    Root->SetNumberField(TEXT("requested_fps"), RequestedFPS);
    Root->SetNumberField(TEXT("observed_mean_fps"), Frames / (FPlatformTime::Seconds() - TotalStarted));
    Root->SetNumberField(TEXT("failures"), Failures);
    Root->SetArrayField(TEXT("checks"), Results);
    Root->SetArrayField(TEXT("phase_frame_rates"), PhaseRates);
    FString Json;
    FJsonSerializer::Serialize(Root, TJsonWriterFactory<>::Create(&Json));
    IFileManager::Get().MakeDirectory(*FPaths::GetPath(Output), true);
    FFileHelper::SaveStringToFile(Json, *Output);
    if (FParse::Param(FCommandLine::Get(), TEXT("FoundationMenuCycle")))
    {
        PC->Diagnostics->ReturnToMenu();
        return;
    }
    FPlatformMisc::RequestExitWithStatus(false, Failures ? 1 : 0);
}
