#include "Foundation/FoundationRuntimeProbe.h"
#include "UnnamedLog.h"
#include "Foundation/FoundationHud.h"
#include "Foundation/FoundationInputRouter.h"
#include "Foundation/FoundationPanels.h"
#include "Menu/MenuBrowserHost.h"
#include "Foundation/FoundationLab.h"
#include "Foundation/FoundationPawn.h"
#include "Foundation/FoundationProfile.h"
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

void UFoundationRuntimeProbe::PressKey(FKey K, bool bDown)
{
    if (Frames < DeferKeysUntilFrame)
    {
        QueuedKeys.Add({K, bDown});
        return;
    }
    if (bDown)
        DownKeys.AddUnique(K);
    else
        DownKeys.Remove(K);
    Controller->GetInputRouter()->RouteKey(K, bDown);
}

void UFoundationRuntimeProbe::ResetPlayer(FVector Position)
{
    DeferKeysUntilFrame = 0;
    QueuedKeys.Reset();
    auto Keys = DownKeys;
    for (FKey K : Keys)
        PressKey(K, false);
    Controller->GetInputSubsystem()->ClearIntent();
    Pawn->Simulation.Reset(Position, *GetDefault<UFoundationProfile>());
    Pawn->Simulation.bGrounded = true;
    Pawn->SetActorLocation(Position);
    Controller->SetControlRotation(FRotator::ZeroRotator);
    DeferKeysUntilFrame = Frames + 2;
}

void UFoundationRuntimeProbe::Record(FString Name, bool bPassed, FString Detail)
{
    auto Report = MakeShared<FJsonObject>();
    Report->SetStringField(TEXT("scenario"), Name);
    Report->SetBoolField(TEXT("pass"), bPassed);
    Report->SetStringField(TEXT("detail"), Detail);
    Results.Add(MakeShared<FJsonValueObject>(Report));
    if (!bPassed)
    {
        ++Failures;
        Report->SetStringField(TEXT("runtime_snapshot"), Controller->GetHud()->BuildSnapshot());
    }
    UE_LOG(LogUnnamedFoundation, Display, TEXT("FOUNDATION CHECK %s: %s | %s"), bPassed ? TEXT("PASS") : TEXT("FAIL"),
        *Name, *Detail);
}

void UFoundationRuntimeProbe::Measure(FString Name, double Actual, double Expected, double Tolerance)
{
    Record(Name, FMath::Abs(Actual - Expected) <= Tolerance,
        FString::Printf(TEXT("actual=%.8f expected=%.8f tolerance=%.8f"), Actual, Expected, Tolerance));
}

void UFoundationRuntimeProbe::AddCase(FString Name, double Duration, TFunction<void()> Start, TFunction<void()> End)
{
    Cases.Add({MoveTemp(Name), Duration, MoveTemp(Start), MoveTemp(End)});
}

void UFoundationRuntimeProbe::BeginPlay()
{
    Super::BeginPlay();
#if WITH_DEV_AUTOMATION_TESTS
    Pawn = Cast<AFoundationPawn>(GetOwner());
    Controller = Pawn ? Cast<AFoundationController>(Pawn->GetController()) : nullptr;
    if (!Pawn || !Controller || !FParse::Value(FCommandLine::Get(), TEXT("FoundationProbe="), Output))
    {
        SetComponentTickEnabled(false);
        return;
    }
    FParse::Value(FCommandLine::Get(), TEXT("FoundationFPS="), RequestedFPS);
    IConsoleManager::Get().FindConsoleVariable(TEXT("r.VSync"))->Set(0, ECVF_SetByCode);
    IConsoleManager::Get().FindConsoleVariable(TEXT("t.MaxFPS"))->Set(RequestedFPS, ECVF_SetByCode);
    InputBridge = NewObject<UMenuInputBridge>(this);
    InputBridge->Initialize(Controller->GetInputSubsystem());
    if (FParse::Param(FCommandLine::Get(), TEXT("FoundationMenuCycle")))
    {
        static int32 Sequence = 0;
        Output = FPaths::ChangeExtension(Output, FString::Printf(TEXT("%d.json"), ++Sequence));
        AddCase(
            TEXT("menu possession"), .5, [] {},
            [this]
            {
                Record(TEXT("menu selected Foundation map and pawn"),
                    Controller->GetPawn() == Pawn && Pawn->GetLab() != nullptr, GetWorld()->GetMapName());
            });
        AddCase(
            TEXT("menu-bound movement"), .3,
            [this] { PressKey(Controller->GetInputSubsystem()->GetCurrentKey(TEXT("key.forward")), true); },
            [this]
            {
                Record(TEXT("menu binding drives actual pawn"), Pawn->Simulation.Feet.X > 20,
                    Controller->GetHud()->BuildSnapshot());
                PressKey(Controller->GetInputSubsystem()->GetCurrentKey(TEXT("key.forward")), false);
            });
        AddCase(
            TEXT("menu screenshot"), .5, [] {}, [this]
            { FScreenshotRequest::RequestScreenshot(FPaths::ChangeExtension(Output, TEXT("png")), true, false); });
        AddCase(TEXT("menu screenshot flush"), .3, [] {}, [] {});
        TotalStarted = Started = FPlatformTime::Seconds();
        return;
    }
    FString Persistence;
    FParse::Value(FCommandLine::Get(), TEXT("FoundationPersistence="), Persistence);
    if (!Persistence.IsEmpty())
    {
        AddCase(
            TEXT("persistence"), 1,
            [this, Persistence]
            {
                if (Persistence == TEXT("write"))
                {
                    InputBridge->ResetBindings(true);
                    InputBridge->ApplyBinding(TEXT("key.forward"), TEXT("K"));
                    InputBridge->ApplyBinding(TEXT("key.jump"), TEXT("Enter"));
                    InputBridge->ApplyBinding(TEXT("key.sprint"), TEXT("RightControl"));
                    InputBridge->ApplyBinding(TEXT("key.sneak"), TEXT("RightShift"));
                    InputBridge->ApplyBinding(TEXT("key.attack"), TEXT("ThumbMouseButton"));
                    InputBridge->ApplyPolicy(TEXT("toggleCrouch"), true);
                    auto* S = Controller->GetInputSubsystem()->GetSettings();
                    S->DebugTextScale = 1.4f;
                    FString E;
                    Record(TEXT("native SaveGame write"), S->SaveChecked(E), E);
                }
            },
            [this]
            {
                auto* I = Controller->GetInputSubsystem();
                Record(TEXT("five bindings persisted"),
                    I->GetCurrentKey(TEXT("key.forward")) == EKeys::K &&
                        I->GetCurrentKey(TEXT("key.jump")) == EKeys::Enter &&
                        I->GetCurrentKey(TEXT("key.sprint")) == EKeys::RightControl &&
                        I->GetCurrentKey(TEXT("key.sneak")) == EKeys::RightShift &&
                        I->GetCurrentKey(TEXT("key.attack")) == EKeys::ThumbMouseButton,
                    InputBridge->GetState());
                Record(TEXT("policy and debug scale persisted"),
                    I->GetSettings()->bToggleCrouch && FMath::IsNearlyEqual(I->GetSettings()->DebugTextScale, 1.4f),
                    TEXT("native SaveGame profile readback"));
            });
        TotalStarted = Started = FPlatformTime::Seconds();
        return;
    }
    InputBridge->ResetBindings(true);
    AddCase(
        TEXT("startup"), 1, [] {},
        [this]
        {
            Record(TEXT("possession and geometry"),
                Controller->GetPawn() == Pawn && Pawn->GetLab() && Pawn->GetLab()->CollisionBoxes.Num() > 20,
                TEXT("real game world, controller, pawn and native primitives"));
            Measure(TEXT("standing height cm"), Pawn->Simulation.HeightCm, 180, .1);
            Measure(TEXT("standing eyes cm"), Pawn->GetEyePosition().Z - Pawn->Simulation.Feet.Z, 162, .1);
        });
    AddCase(
        TEXT("walk"), 2,
        [this]
        {
            ResetPlayer();
            PressKey(EKeys::W, true);
        },
        [this]
        { Measure(TEXT("walk blocks/s"), Pawn->Simulation.LastDisplacement.Size2D() / 5., 4.3171806, .0431718); });
    AddCase(
        TEXT("stop"), 1, [this] { PressKey(EKeys::W, false); },
        [this] { Measure(TEXT("stopped speed"), Pawn->Simulation.LastDisplacement.Size2D(), 0, .001); });
    AddCase(
        TEXT("sprint"), 2,
        [this]
        {
            ResetPlayer();
            PressKey(EKeys::W, true);
            PressKey(EKeys::LeftShift, true);
        },
        [this]
        {
            Measure(TEXT("sprint blocks/s"), Pawn->Simulation.LastDisplacement.Size2D() / 5., 5.6123348, .0561233);
            Record(TEXT("Shift sprints without crouching"), Pawn->Simulation.bSprinting && !Pawn->Simulation.bCrouched,
                TEXT("project personal default LeftShift"));
        });
    AddCase(
        TEXT("sprint release"), .2, [this] { PressKey(EKeys::LeftShift, false); },
        [this]
        {
            Record(TEXT("sprint release latch"), Pawn->Simulation.bSprinting,
                TEXT("continues until forward/eligibility/collision ends it"));
        });
    AddCase(
        TEXT("crouch"), 2,
        [this]
        {
            ResetPlayer();
            PressKey(EKeys::W, true);
            PressKey(EKeys::C, true);
        },
        [this]
        {
            Measure(TEXT("crouch blocks/s"), Pawn->Simulation.LastDisplacement.Size2D() / 5., 1.2951542, .0129515);
            Measure(TEXT("crouch height cm"), Pawn->Simulation.HeightCm, 150, .1);
            Measure(TEXT("crouch eye cm"), Pawn->Simulation.EyeCm, 127, .1);
            Record(TEXT("C requests crouch"), Pawn->LastIntent.bCrouch && Pawn->Simulation.bCrouched,
                TEXT("project personal default C"));
        });
    AddCase(
        TEXT("old sprint key"), .3,
        [this]
        {
            ResetPlayer();
            PressKey(EKeys::W, true);
            PressKey(EKeys::LeftControl, true);
        },
        [this]
        {
            Record(TEXT("Ctrl no longer requests sprint"), !Pawn->LastIntent.bSprint && !Pawn->Simulation.bSprinting,
                TEXT("fixed Ctrl stack-drop modifier is unchanged"));
        });
    AddCase(
        TEXT("jump"), .8,
        [this]
        {
            ResetPlayer();
            Apex = 0;
            LaunchTick = LandTick = -1;
            PressKey(EKeys::SpaceBar, true);
        },
        [this]
        {
            Measure(TEXT("jump apex blocks"), Apex / 100., 1.25220334, .02);
            Measure(TEXT("jump airtime ticks"), double(LandTick - LaunchTick + 1), 12, 1);
        });
    AddCase(
        TEXT("diagonal"), 2,
        [this]
        {
            ResetPlayer({-1800, 100, 0});
            PressKey(EKeys::W, true);
            PressKey(EKeys::D, true);
        },
        [this]
        { Measure(TEXT("diagonal blocks/s"), Pawn->Simulation.LastDisplacement.Size2D() / 5., 4.40528634, .0440529); });
    AddCase(
        TEXT("opposing"), .3,
        [this]
        {
            ResetPlayer();
            PressKey(EKeys::W, true);
            PressKey(EKeys::S, true);
        },
        [this] { Measure(TEXT("opposing keys displacement"), Pawn->Simulation.Feet.Size(), 0, .001); });
    AddCase(
        TEXT("rebind old"), .3,
        [this]
        {
            ResetPlayer();
            InputBridge->ApplyBinding(TEXT("key.forward"), TEXT("K"));
            PressKey(EKeys::W, true);
        },
        [this] { Measure(TEXT("removed W binding"), Pawn->Simulation.Feet.Size(), 0, .001); });
    AddCase(
        TEXT("rebind new"), .15,
        [this]
        {
            PressKey(EKeys::W, false);
            PressKey(EKeys::K, true);
        },
        [this]
        { Record(TEXT("new K binding moves"), Pawn->Simulation.Feet.X > 10, TEXT("native profile forward=K")); });
    AddCase(TEXT("forward tap gap"), .1, [this] { PressKey(EKeys::K, false); }, [] {});
    AddCase(
        TEXT("forward double tap"), .15, [this] { PressKey(EKeys::K, true); },
        [this]
        {
            Record(TEXT("double tap follows remapped forward"), Pawn->Simulation.bSprinting,
                TEXT("K tap-release-K inside7 reference ticks"));
        });
    AddCase(
        TEXT("blocked stand"), .3,
        [this]
        {
            ResetPlayer({400, 400, 0});
            PressKey(EKeys::C, true);
        },
        [this]
        {
            Record(TEXT("low ceiling crouch"), Pawn->Simulation.bCrouched && !Pawn->Simulation.bHeadroom,
                TEXT("155cm ceiling"));
        });
    AddCase(
        TEXT("blocked uncrouch"), .2, [this] { PressKey(EKeys::C, false); },
        [this]
        {
            Record(TEXT("release cannot stand through ceiling"),
                Pawn->Simulation.bCrouched && !Pawn->Simulation.bHeadroom,
                TEXT("resolved pose survives released intent"));
        });
    AddCase(
        TEXT("mouse"), .15,
        [this]
        {
            ResetPlayer();
            Controller->InputKey(FInputKeyEventArgs(nullptr, FInputDeviceId::CreateFromInternalId(0), EKeys::MouseX,
                100.f, 1.f / RequestedFPS, 1, FPlatformTime::Cycles64()));
        },
        [this] { Measure(TEXT("100 mouse counts yaw degrees"), Controller->GetControlRotation().Yaw, 15, .05); });
    AddCase(
        TEXT("pause held"), .25,
        [this]
        {
            ResetPlayer();
            PressKey(EKeys::K, true);
            PressKey(EKeys::Escape, true);
            PressKey(EKeys::Escape, false);
        },
        [this]
        {
            Record(TEXT("pause removes contexts"),
                !Controller->GetInputSubsystem()->IsGameplayEnabled() && Controller->GetMenuHost()->IsOpen(),
                TEXT("existing CEF menu opened; no sim movement"));
        });
    AddCase(
        TEXT("release while paused"), .15,
        [this]
        {
            PressKey(EKeys::K, false);
            Controller->ResumeFoundation();
        },
        [this]
        {
            Record(TEXT("resume clears stale movement"), Pawn->LastIntent.Move.IsNearlyZero(),
                TEXT("release happened in UIOnly context"));
        });
    AddCase(
        TEXT("text"), .15,
        [this]
        {
            Controller->GetPanels()->OpenText(false);
            PressKey(EKeys::K, true);
        },
        [this]
        {
            Record(TEXT("text does not move"), !Controller->IsGameplay() && Pawn->LastIntent.Move.IsNearlyZero(),
                TEXT("native editable text focus; no chat backend"));
            PressKey(EKeys::K, false);
            Controller->GetPanels()->Close();
        });
    AddCase(
        TEXT("debug chord"), .15,
        [this]
        {
            InputBridge->ApplyBinding(TEXT("key.togglePerspective"), TEXT("F6"));
            Controller->GetHud()->bOverlay = false;
            PressKey(EKeys::F3, true);
            PressKey(EKeys::F6, true);
            PressKey(EKeys::F6, false);
            PressKey(EKeys::F3, false);
        },
        [this]
        {
            Record(TEXT("F3+F6 options and no perspective"),
                Controller->GetPanels()->IsOpen() && Controller->GetHud()->bOverlay && Pawn->GetPerspective() == 0,
                TEXT("perspective rebound to F6; chord consumes it"));
            Controller->GetPanels()->Close();
        });
    AddCase(
        TEXT("F3 alone"), .15,
        [this]
        {
            PressKey(EKeys::F3, true);
            PressKey(EKeys::F3, false);
        },
        [this] { Record(TEXT("F3 alone toggles"), !Controller->GetHud()->bOverlay, TEXT("toggle on release")); });
    AddCase(
        TEXT("F3 C sharing"), .15,
        [this]
        {
            ResetPlayer();
            PressKey(EKeys::F3, true);
            PressKey(EKeys::C, true);
        },
        [this]
        {
            Record(TEXT("F3+C does not also crouch"), !Pawn->LastIntent.bCrouch && !Pawn->Simulation.bCrouched,
                TEXT("debug chord consumes shared C binding"));
            PressKey(EKeys::C, false);
            PressKey(EKeys::F3, false);
        });
    AddCase(
        TEXT("HUD"), .15, [this] { PressKey(EKeys::F1, true); },
        [this]
        {
            Record(TEXT("HUD hides via action"), Controller->GetHud()->bHideHud, TEXT("Enhanced Input F1 action"));
            PressKey(EKeys::F1, false);
            Controller->GetHud()->bHideHud = false;
        });
    AddCase(
        TEXT("capture"), .15,
        [this]
        {
            Controller->GetInputRouter()->SetCapture(true);
            PressKey(EKeys::K, true);
        },
        [this]
        {
            Record(TEXT("capture consumes gameplay intent"), Pawn->LastIntent.Move.IsNearlyZero(),
                TEXT("capture input never reaches Enhanced Input"));
            Controller->GetInputRouter()->SetCapture(false);
            PressKey(EKeys::K, false);
        });
    AddCase(
        TEXT("capture F3"), .15,
        [this]
        {
            Controller->GetHud()->bOverlay = false;
            Controller->GetInputRouter()->SetCapture(true);
            PressKey(EKeys::F3, true);
            Controller->GetInputRouter()->SetCapture(false);
            PressKey(EKeys::F3, false);
        },
        [this]
        {
            Record(TEXT("capture F3 release cannot toggle overlay"), !Controller->GetHud()->bOverlay,
                TEXT("captured press has no standalone release action"));
        });
    AddCase(
        TEXT("inventory close"), .15,
        [this]
        {
            Controller->GetPanels()->OpenInventory();
            PressKey(EKeys::E, true);
            PressKey(EKeys::E, false);
        },
        [this]
        {
            Record(TEXT("logical inventory binding closes fixture"),
                !Controller->GetPanels()->IsOpen() && Controller->IsGameplay(),
                TEXT("same configurable E binding in inventory context"));
        });
    AddCase(
        TEXT("focus"), .15,
        [this]
        {
            PressKey(EKeys::K, true);
            Controller->HandleFocusChanged(false);
            PressKey(EKeys::K, false);
        },
        [this]
        {
            Record(TEXT("focus loss cancels"),
                Controller->GetMenuHost()->IsOpen() && Pawn->LastIntent.Move.IsNearlyZero(),
                TEXT("application activation handler invoked, not physical OS focus test"));
            Controller->HandleFocusChanged(true);
            Controller->ResumeFoundation();
        });
    AddCase(
        TEXT("wheel binding"), .25,
        [this]
        {
            ResetPlayer();
            InputBridge->ApplyBinding(TEXT("key.jump"), TEXT("MouseScrollUp"));
            Controller->GetInputRouter()->RouteWheel(1);
        },
        [this]
        {
            Record(TEXT("wheel pulse reaches rebound jump"), Pawn->Simulation.Feet.Z > 30,
                TEXT("Enhanced Input pulse survives one reference step"));
        });
    AddCase(
        TEXT("action target"), .15,
        [this]
        {
            ResetPlayer({250, 1050, 0});
            PressKey(EKeys::LeftMouseButton, true);
        },
        [this]
        {
            Record(TEXT("native attack reaches fixture"), Controller->Actions.bBreaking,
                TEXT("eye ray at block fixture2.5m"));
        });
    AddCase(
        TEXT("action cancel"), .15,
        [this]
        {
            PressKey(EKeys::Escape, true);
            PressKey(EKeys::Escape, false);
            PressKey(EKeys::LeftMouseButton, false);
        },
        [this]
        {
            Record(TEXT("pause cancels attack/use"), !Controller->Actions.bBreaking && !Controller->Actions.bUsing,
                TEXT("context removal"));
            Controller->ResumeFoundation();
        });
    AddCase(
        TEXT("menu cycles"), .15,
        [this]
        {
            for (int32 I = 0; I < 4; ++I)
            {
                Controller->PauseFoundation();
                Controller->ResumeFoundation();
            }
            InputBridge->ResetBindings(true);
        },
        [this]
        {
            Record(TEXT("repeated pause/resume stays playable"),
                Controller->IsGameplay() && Controller->GetInputSubsystem()->IsGameplayEnabled(),
                TEXT("4 native browser/context trips"));
        });
    AddCase(
        TEXT("screenshot"), 1,
        [this]
        {
            ResetPlayer({-100, -850, 0});
            Controller->SetControlRotation(FRotator(-10, 35, 0));
            Controller->GetHud()->bOverlay = true;
            Controller->GetHud()->bShowCollision = true;
        },
        [this] { FScreenshotRequest::RequestScreenshot(FPaths::ChangeExtension(Output, TEXT("png")), true, false); });
    AddCase(TEXT("capture flush"), 1, [] {}, [] {});
    TotalStarted = Started = FPlatformTime::Seconds();
#else
    SetComponentTickEnabled(false);
#endif
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
            PressKey(Pair.Key, Pair.Value);
    }
    if (Phase >= 0 && Cases[Phase].Name == TEXT("jump"))
    {
        Apex = FMath::Max(Apex, Pawn->Simulation.Feet.Z);
        if (LaunchTick < 0 && Pawn->Simulation.Feet.Z > 0) LaunchTick = Pawn->Simulation.Steps;
        if (LaunchTick >= 0 && LandTick < 0 && Pawn->Simulation.bGrounded)
        {
            LandTick = Pawn->Simulation.Steps;
            PressKey(EKeys::SpaceBar, false);
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
        Controller->ReturnToMenu();
        return;
    }
    FPlatformMisc::RequestExitWithStatus(false, Failures ? 1 : 0);
}
