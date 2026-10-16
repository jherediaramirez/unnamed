// M1b, measured (-FoundationSuite=body, the Unnamed Player): the pickaxe in the real hand and StrikeA on the swing
// clock (PLAN.md section 7). The strike is drawn at exact frames by pausing the simulation at step start + k - 1: with
// no step due the pawn draws at alpha 1, so the swing's phase is k / 6 and the clip's frame 3k.
#include "Foundation/FoundationRuntimeProbe.h"
#include "Foundation/FoundationBodyAnimInstance.h"
#include "Foundation/FoundationBreakEffects.h"
#include "Foundation/FoundationController.h"
#include "Foundation/FoundationInteractionView.h"
#include "Foundation/FoundationPlayground.h"
#include "Input/UnnamedInputSubsystem.h"
#include "Foundation/FoundationPawn.h"
#include "Foundation/FoundationProfile.h"
#include "Animation/AnimSequence.h"
#include "Camera/CameraComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/StaticMesh.h"
#include "HAL/IConsoleManager.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "StaticMeshResources.h"
#include "UnrealClient.h"

#if WITH_DEV_AUTOMATION_TESTS
namespace
{
    int32 SetStrikeProbeCVar(const TCHAR* Name, int32 Value)
    {
        IConsoleVariable* Variable = IConsoleManager::Get().FindConsoleVariable(Name);
        if (!Variable) return 0;
        const int32 Previous = Variable->GetInt();
        Variable->Set(Value, ECVF_SetByCode);
        return Previous;
    }

    // The strike's tool tip in the tool's frame (UE axes: the grip socket maps the tool's +Y to UE -Y; M0's pickaxe
    // gate) and the contact target in the body's component space (the manifest's target, Blender m -> cm).
    const FVector StrikeTipCm(0, -42.75, 26.5);
    const FVector StrikeTargetCm(-5, 80, 125);

    // The arms' bones relative to spine_02: the body as drawn against StrikeA's own pose at Time. The worst rotation
    // difference (degrees).
    double ArmErrorAgainstClip(const USkeletalMeshComponent* Body, const UAnimSequence* Clip, double Time)
    {
        const FReferenceSkeleton& Reference = Body->GetSkeletalMeshAsset()->GetRefSkeleton();
        const TArray<FTransform>& Pose = Body->GetComponentSpaceTransforms();
        const int32 Spine = Reference.FindBoneIndex(TEXT("spine_02"));
        if (Spine == INDEX_NONE || !Clip) return -1;
        auto Sample = [Clip, Time](int32 Bone)
        {
            FTransform Local;
            Clip->GetBoneTransform(Local, FSkeletonPoseBoneIndex(Bone), FAnimExtractContext(Time), false);
            return Local;
        };
        double Worst = 0;
        for (const TCHAR* Side : {TEXT("l"), TEXT("r")})
        {
            FTransform Chain = FTransform::Identity;
            for (const TCHAR* Part : {TEXT("clavicle_"), TEXT("upperarm_"), TEXT("lowerarm_"), TEXT("hand_")})
            {
                const int32 Bone = Reference.FindBoneIndex(FName(FString(Part) + Side));
                if (Bone == INDEX_NONE) return -1;
                Chain = Sample(Bone) * Chain;
                const FTransform Drawn = Pose[Bone].GetRelativeTransform(Pose[Spine]);
                Worst = FMath::Max(
                    Worst, FMath::RadiansToDegrees(Drawn.GetRotation().AngularDistance(Chain.GetRotation())));
            }
        }
        return Worst;
    }

    // The nearest tool vertex to Point (cm), or -1 when the mesh keeps no CPU copy of its positions.
    double NearestToolVertex(const UStaticMeshComponent* Tool, const FVector& Point)
    {
        const UStaticMesh* Mesh = Tool ? Tool->GetStaticMesh() : nullptr;
        const FStaticMeshRenderData* Data = Mesh ? Mesh->GetRenderData() : nullptr;
        if (!Data || Data->LODResources.IsEmpty()) return -1;
        const FPositionVertexBuffer& Positions = Data->LODResources[0].VertexBuffers.PositionVertexBuffer;
        if (!Positions.GetVertexData() || Positions.GetNumVertices() == 0) return -1;
        const FTransform ToWorld = Tool->GetComponentTransform();
        double Best = TNumericLimits<double>::Max();
        for (uint32 Index = 0; Index < Positions.GetNumVertices(); ++Index)
            Best = FMath::Min(
                Best, FVector::Dist(Point, ToWorld.TransformPosition(FVector(Positions.VertexPosition(Index)))));
        return Best;
    }
}
#endif

void UFoundationRuntimeProbe::AddStrikeBodyCases()
{
#if WITH_DEV_AUTOMATION_TESTS
    const FVector Spot(20000, 24000, 0);
    auto Key = [this](const TCHAR* Id) { return Controller->GetInputSubsystem()->GetCurrentKey(Id); };
    auto Click = [this](FKey Pressed)
    {
        PressKey(Pressed, true);
        PressKey(Pressed, false);
    };
    auto Shot = [this](const FString& Name)
    {
        FScreenshotRequest::RequestScreenshot(
            FPaths::ChangeExtension(Output, TEXT("")) + TEXT("-body-") + Name + TEXT(".png"), true, false);
    };
    auto SaveText = [this](const TCHAR* Suffix, const FString& Text)
    { FFileHelper::SaveStringToFile(Text, *(FPaths::ChangeExtension(Output, TEXT("")) + Suffix)); };
    auto Stand = [this](FVector Where, int32 View, FRotator Look)
    {
        Pawn->SetSimulationEnabled(true);
        ResetPlayer(Where);
        Pawn->SetPerspective(View);
        Controller->SetControlRotation(Look);
        Pawn->ResetBody();
    };
    auto HeldItemShown = [this]
    {
        const auto* Lab = Cast<AFoundationPlayground>(Pawn->GetLab());
        const UFoundationInteractionView* View = Lab ? Lab->GetView() : nullptr;
        const UStaticMeshComponent* Held = View ? View->GetHeldItem() : nullptr;
        return Held && Held->IsVisible();
    };
    auto MainItem = [this]
    {
        const FFoundationAuthority* Authority = Pawn->GetAuthority();
        const FFoundationPlayer* Player = Authority ? Authority->FindPlayer(Pawn->Interaction.PlayerId) : nullptr;
        if (!Player) return FName();
        const FFoundationItemInstance& Main = Player->Inventory.GetHand(EFoundationHand::Main);
        return Main.IsEmpty() ? FName() : Authority->Rules.GetItem(Main.Item).Id;
    };
    // The pickaxe drawn for the owner: how many copies (the camera's, the copy's tool, the world tool unless it is the
    // owner's shadow only).
    auto ToolsDrawn = [this, HeldItemShown]
    {
        const UStaticMeshComponent* World = Pawn->GetBodyTool();
        const UStaticMeshComponent* Copy = Pawn->GetFirstPersonBodyTool();
        const bool bWorld = World && !World->bHiddenInGame && !Pawn->IsBodyShadowOnly() && Pawn->IsBodyShown();
        const bool bCopy = Copy && !Copy->bHiddenInGame && Pawn->IsFirstPersonBodyShown();
        return int32(bWorld) + int32(bCopy) + int32(HeldItemShown());
    };
    auto SocketError = [](const UStaticMeshComponent* Tool, const USkeletalMeshComponent* Body, double& OutDegrees)
    {
        const FTransform Socket = Body->GetSocketTransform(FoundationBodyGrip::GetGripSocket());
        const FTransform Drawn = Tool->GetComponentTransform();
        OutDegrees = FMath::RadiansToDegrees(Socket.GetRotation().AngularDistance(Drawn.GetRotation()));
        return FVector::Dist(Socket.GetLocation(), Drawn.GetLocation());
    };

    // The pickaxe in hand (the loadout's slot 2): the body holds it, the camera's copy hides.
    AddCase(
        TEXT("body strike hold"), .6,
        [this, Stand, Spot, Click]
        {
            Stand(Spot, 0, FRotator(0, 0, 0));
            Click(EKeys::Two);
        },
        [this, Shot, MainItem, HeldItemShown, ToolsDrawn, SocketError]
        {
            Shot(TEXT("strike-fp-hold"));
            Record(TEXT("body strike: a pickaxe in the main hand"), FoundationBodyGrip::IsBodyHeld(MainItem()),
                MainItem().ToString());
            Record(TEXT("body strike: the hold takes the upper body"),
                Pawn->BodyAnimator.Pose.GetWeight(FoundationBodyClip::PickaxeHold) > .99f,
                FString::Printf(TEXT("hold %.3f"), Pawn->BodyAction.HoldWeight));
            Record(TEXT("body strike fp: one pickaxe drawn, in the body's hand"),
                ToolsDrawn() == 1 && !HeldItemShown() && Pawn->IsMainItemInBody(),
                FString::Printf(TEXT("drawn %d, camera copy %d"), ToolsDrawn(), HeldItemShown()));
            double Degrees[2] = {0, 0};
            const double Cm[2] = {SocketError(Pawn->GetBodyTool(), Pawn->GetBodyMesh(), Degrees[0]),
                SocketError(Pawn->GetFirstPersonBodyTool(), Pawn->GetFirstPersonBodyMesh(), Degrees[1])};
            Record(TEXT("body strike: both tools on the grip socket (<= 0.05 cm)"),
                FMath::Max(Cm[0], Cm[1]) <= .05 && FMath::Max(Degrees[0], Degrees[1]) <= .05,
                FString::Printf(
                    TEXT("world %.5f cm %.4f deg, copy %.5f cm %.4f deg"), Cm[0], Degrees[0], Cm[1], Degrees[1]));
        });
    AddCase(
        TEXT("body strike back"), .5, [this] { Pawn->SetPerspective(1); },
        [this, Shot, HeldItemShown, ToolsDrawn]
        {
            Shot(TEXT("strike-back-hold"));
            Record(TEXT("body strike back: one pickaxe drawn, in the world body's hand"),
                ToolsDrawn() == 1 && !HeldItemShown() && !Pawn->GetBodyTool()->bHiddenInGame,
                FString::Printf(TEXT("drawn %d"), ToolsDrawn()));
            Pawn->SetPerspective(0);
        });

    // One press: the strike starts on the swing clock's start step (the view's own clock agrees) and draws frame 9 in
    // the frame its contact step starts.
    struct FTiming
    {
        int64 Start = MIN_int64, ViewStart = MIN_int64;
        int32 Frames = 0, StartFrame = -1, ContactStepFrame = -1, Frame9 = -1;
        double MinMs = 1e9, MaxMs = 0;
    };
    TSharedRef<FTiming> Timing = MakeShared<FTiming>();
    AddCase(
        TEXT("body strike timing"), .1,
        [this, Stand, Spot, Timing]
        {
            Stand(Spot, 0, FRotator(0, 0, 0));
            *Timing = FTiming();
            bHoldCase = true;
            FrameHook = [this, Timing]
            {
                const int32 Frame = Timing->Frames++;
                if (Frame == 20) PressKey(EKeys::LeftMouseButton, true);
                if (Frame == 21) PressKey(EKeys::LeftMouseButton, false);
                if (Frame < 20) return;
                const double Ms = GetWorld()->GetDeltaSeconds() * 1000.;
                Timing->MinMs = FMath::Min(Timing->MinMs, Ms);
                Timing->MaxMs = FMath::Max(Timing->MaxMs, Ms);
                const FFoundationBodyAction& Action = Pawn->BodyAction;
                if (Timing->StartFrame < 0 && Action.bStriking)
                {
                    Timing->StartFrame = Frame;
                    Timing->Start = Action.StartStep;
                }
                // The view updates its swing clock later in the frame (post-update work): read it a frame on.
                if (Timing->StartFrame >= 0 && Frame == Timing->StartFrame + 1)
                {
                    const auto* Lab = Cast<AFoundationPlayground>(Pawn->GetLab());
                    if (const FFoundationBreakEffects* Effects =
                            Lab && Lab->GetView() ? Lab->GetView()->GetBreakEffects() : nullptr)
                        Timing->ViewStart = Effects->GetSwingClock().StartStep;
                }
                if (Timing->StartFrame < 0) return;
                if (Timing->ContactStepFrame < 0 && Pawn->Interaction.Step >= Timing->Start + 3)
                    Timing->ContactStepFrame = Frame;
                if (Timing->Frame9 < 0 && Action.Phase >= 0 && Action.StrikeTime * 60 >= 9 - 1e-6)
                    Timing->Frame9 = Frame;
                if (Frame > Timing->StartFrame + 60) bHoldCase = false;
            };
        },
        [this, Timing]
        {
            Record(TEXT("body strike: starts on the swing clock's start step (the view's clock agrees)"),
                Timing->StartFrame >= 0 && Timing->Start == Timing->ViewStart,
                FString::Printf(TEXT("body start step %lld, view %lld"), Timing->Start, Timing->ViewStart));
            // PLAN 7: drawn in the frame that contains step start + 3 (+-1 frame at 30 fps).
            Record(TEXT("body strike: frame 9 drawn in the frame step start + 3 begins"),
                Timing->Frame9 >= 0 && FMath::Abs(Timing->Frame9 - Timing->ContactStepFrame) <= 1,
                FString::Printf(TEXT("frame 9 at frame %d, step start+3 at frame %d (%.1f-%.1f ms frames)"),
                    Timing->Frame9 - Timing->StartFrame, Timing->ContactStepFrame - Timing->StartFrame, Timing->MinMs,
                    Timing->MaxMs));
        });

    // Exact frames 0, 3, ... 15 (paused at start + k - 1): the arms against the clip, the tip at the contact, the
    // tool's distance from the eye, one pickaxe drawn, and stills in first person and the back view.
    struct FStills
    {
        int64 Start = 0;
        int32 K = 0, Wait = 0, Stage = 0;
        double WorstArm = 0, TipErrorCm = -1, ToolNearestCm = TNumericLimits<double>::Max();
        int32 Doubles = 0;
        FString Table =
            TEXT("frame,arm_error_deg_world,arm_error_deg_copy,tool_to_eye_cm,tools_drawn_fp,tools_drawn_back\n");
        FString Row;
    };
    TSharedRef<FStills> Stills = MakeShared<FStills>();
    AddCase(
        TEXT("body strike stills"), .1,
        [this, Stand, Spot, Stills, Shot, ToolsDrawn]
        {
            Stand(Spot, 0, FRotator(0, 0, 0));
            *Stills = FStills();
            bHoldCase = true;
            FrameHook = [this, Stills, Shot, ToolsDrawn]
            {
                FStills& S = *Stills;
                const UFoundationBodyAnimInstance* Anim =
                    Cast<UFoundationBodyAnimInstance>(Pawn->GetBodyMesh()->GetAnimInstance());
                const UAnimSequence* Clip = Anim ? Anim->GetClip(FoundationBodyClip::StrikeA) : nullptr;
                switch (S.Stage)
                {
                case 0: // settle, then press
                    if (++S.Wait < 20) return;
                    PressKey(EKeys::LeftMouseButton, true);
                    PressKey(EKeys::LeftMouseButton, false);
                    S.Wait = 0;
                    S.Stage = 1;
                    return;
                case 1: // wait for the strike, then for step start + k - 1 (k = 0: frame 0 at the start step)
                    if (!Pawn->BodyAction.bStriking && S.K == 0) return;
                    if (S.K == 0) S.Start = Pawn->BodyAction.StartStep;
                    if (S.K > 0 && Pawn->Interaction.Step < S.Start + S.K - 1) return;
                    // Frame 0 is measured as drawn on the strike's first frame; frames 3k paused at start + k - 1.
                    if (S.K > 0) Pawn->SetSimulationEnabled(false);
                    S.Wait = 0;
                    S.Stage = 2;
                    return;
                case 2: // let the paused frame pose and draw (first person), then measure and shoot
                {
                    if (S.K > 0 && ++S.Wait < 3) return;
                    // The strike time as drawn (frame 3k when the pause landed on its step).
                    const double Time = Pawn->BodyAction.StrikeTime;
                    const int32 Frame = FMath::RoundToInt32(Time * 60);
                    const double World = ArmErrorAgainstClip(Pawn->GetBodyMesh(), Clip, Time);
                    const double Copy = ArmErrorAgainstClip(Pawn->GetFirstPersonBodyMesh(), Clip, Time);
                    S.WorstArm = FMath::Max3(S.WorstArm, World, Copy);
                    const double Tool =
                        NearestToolVertex(Pawn->GetFirstPersonBodyTool(), Pawn->GetCamera()->GetComponentLocation());
                    if (Tool >= 0) S.ToolNearestCm = FMath::Min(S.ToolNearestCm, Tool);
                    const int32 DrawnFp = ToolsDrawn();
                    S.Doubles += DrawnFp != 1;
                    S.Row = FString::Printf(TEXT("%d,%.4f,%.4f,%.2f,%d"), Frame, World, Copy, Tool, DrawnFp);
                    if (S.K == 3)
                    {
                        // The tip at the contact, in the world body's component space, against the authored target.
                        const FVector Tip = Pawn->GetBodyTool()->GetComponentTransform().TransformPosition(StrikeTipCm);
                        S.TipErrorCm = FVector::Dist(
                            Pawn->GetBodyMesh()->GetComponentTransform().InverseTransformPosition(Tip), StrikeTargetCm);
                    }
                    if (S.K == 0)
                    {
                        S.Table += S.Row + TEXT(",-\n");
                        S.K = 1;
                        S.Stage = 1;
                        return;
                    }
                    Shot(FString::Printf(TEXT("strike-fp-frame%02d"), S.K * 3));
                    S.Wait = 0;
                    S.Stage = 3;
                    return;
                }
                case 3: // the back view of the same frame
                    if (++S.Wait == 2) Pawn->SetPerspective(1);
                    if (S.Wait < 5) return;
                    S.Doubles += ToolsDrawn() != 1;
                    S.Table += S.Row + FString::Printf(TEXT(",%d\n"), ToolsDrawn());
                    Shot(FString::Printf(TEXT("strike-back-frame%02d"), S.K * 3));
                    S.Wait = 0;
                    S.Stage = 4;
                    return;
                case 4: // back to first person, resume, next frame
                    if (++S.Wait == 2) Pawn->SetPerspective(0);
                    if (S.Wait < 3) return;
                    Pawn->SetSimulationEnabled(true);
                    if (++S.K > 5)
                    {
                        bHoldCase = false;
                        return;
                    }
                    S.Wait = 0;
                    S.Stage = 1;
                    return;
                default:
                    return;
                }
            };
        },
        [this, Stills, SaveText]
        {
            Pawn->SetSimulationEnabled(true);
            Pawn->SetPerspective(0);
            SaveText(TEXT("-body-strike.csv"), Stills->Table);
            Record(TEXT("body strike: the arms match StrikeA relative to the chest (<= 0.5 deg, both bodies)"),
                Stills->K > 5 && Stills->WorstArm >= 0 && Stills->WorstArm <= .5,
                FString::Printf(TEXT("worst %.4f deg over frames 0-15"), Stills->WorstArm));
            Record(TEXT("body strike: never two pickaxes (or none) drawn, either view"), Stills->Doubles == 0,
                FString::Printf(TEXT("%d bad frames"), Stills->Doubles));
            Record(TEXT("body strike: tip at the contact frame (vs the authored target)"), Stills->TipErrorCm >= 0,
                FString::Printf(
                    TEXT("%.2f cm from the target (gait and aim blended in; information)"), Stills->TipErrorCm));
            Record(TEXT("body strike fp: the tool's nearest point to the eye (information)"), true,
                Stills->ToolNearestCm < TNumericLimits<double>::Max()
                    ? FString::Printf(TEXT("%.2f cm"), Stills->ToolNearestCm)
                    : FString(TEXT("not measured (no CPU copy of the tool's vertices)")));
        });

    // Mining is the authority's: the same stone, pickaxe and hold give the same break steps and result with Better
    // Steve and with the Unnamed Player.
    TSharedRef<TArray<int32>> BreakSteps = MakeShared<TArray<int32>>();
    for (int32 Set = 0; Set < 2; ++Set)
    {
        AddCase(
            FString::Printf(TEXT("body strike mine set %d"), Set), 1.2,
            [this, Stand, Spot, Set, Click]
            {
                SetStrikeProbeCVar(TEXT("foundation.Body.Set"), Set);
                Stand(Spot, 0, FRotator(0, 0, 0));
                FFoundationAuthority* Authority = Pawn->GetAuthority();
                if (!Authority) return;
                // A stone 3 blocks east of the player's cell, at the feet.
                const FIntVector Cell(int32(Spot.X / 100) + 3, int32(Spot.Y / 100), 0);
                Authority->World.Set(Cell, Authority->World.FindBlock(TEXT("stone"))->GetDefaultState());
                Controller->SetControlRotation(
                    (FVector(Cell) * 100 + FVector(50, 50, 50) - Pawn->GetEyePosition()).Rotation());
                Click(EKeys::Two);
                PressKey(EKeys::LeftMouseButton, true);
            },
            [this, Spot, Set, BreakSteps, Shot]
            {
                PressKey(EKeys::LeftMouseButton, false);
                const FFoundationAuthority* Authority = Pawn->GetAuthority();
                const FIntVector Cell(int32(Spot.X / 100) + 3, int32(Spot.Y / 100), 0);
                const bool bBroken = Authority && Authority->World.Get(Cell).State.IsAir();
                BreakSteps->Add(bBroken ? Pawn->Interaction.LastBreakSteps : -1);
                Record(FString::Printf(TEXT("body strike mine set %d: the stone breaks"), Set), bBroken,
                    FString::Printf(TEXT("%d steps"), Pawn->Interaction.LastBreakSteps));
            });
    }
    AddCase(
        TEXT("body strike mine compare"), .1, [] {},
        [this, BreakSteps]
        {
            SetStrikeProbeCVar(TEXT("foundation.Body.Set"), 1);
            Record(TEXT("body strike: mining identical with Better Steve and the Unnamed Player"),
                BreakSteps->Num() == 2 && (*BreakSteps)[0] > 0 && (*BreakSteps)[0] == (*BreakSteps)[1],
                FString::Printf(TEXT("break steps %d and %d"), BreakSteps->Num() > 0 ? (*BreakSteps)[0] : -1,
                    BreakSteps->Num() > 1 ? (*BreakSteps)[1] : -1));
        });

    // Mining with the body: the swing repeats on the block (a shot mid-swing).
    AddCase(
        TEXT("body strike mining shot"), .45,
        [this, Stand, Spot, Click]
        {
            Stand(Spot, 0, FRotator(0, 0, 0));
            FFoundationAuthority* Authority = Pawn->GetAuthority();
            if (!Authority) return;
            const FIntVector Cell(int32(Spot.X / 100) + 2, int32(Spot.Y / 100), 1);
            Authority->World.Set(Cell, Authority->World.FindBlock(TEXT("obsidian"))->GetDefaultState());
            Controller->SetControlRotation(
                (FVector(Cell) * 100 + FVector(50, 50, 50) - Pawn->GetEyePosition()).Rotation());
            Click(EKeys::Two);
            PressKey(EKeys::LeftMouseButton, true);
        },
        [this, Shot]
        {
            Shot(TEXT("strike-fp-mining"));
            Record(TEXT("body strike: mining repeats the swing"),
                Pawn->Interaction.bMining && Pawn->BodyAction.bStriking,
                FString::Printf(TEXT("mining %d striking %d phase %.2f"), Pawn->Interaction.bMining,
                    Pawn->BodyAction.bStriking, Pawn->BodyAction.Phase));
        });
    AddCase(TEXT("body strike mining release"), .3, [this] { PressKey(EKeys::LeftMouseButton, false); }, [] {});

    // Rollback: foundation.Body.Set 0 draws the camera's pickaxe exactly as before.
    AddCase(
        TEXT("body strike set 0"), .6,
        [this, Stand, Spot, Click]
        {
            SetStrikeProbeCVar(TEXT("foundation.Body.Set"), 0);
            Stand(Spot, 0, FRotator(0, 0, 0));
            Click(EKeys::Two);
        },
        [this, HeldItemShown, Shot]
        {
            Shot(TEXT("strike-set0-fp"));
            Record(TEXT("body strike set 0: the camera's pickaxe, no body tool"),
                HeldItemShown() && !Pawn->GetBodyTool() && !Pawn->IsMainItemInBody(), TEXT(""));
            SetStrikeProbeCVar(TEXT("foundation.Body.Set"), 1);
        });
    AddCase(
        TEXT("body strike restore"), .4,
        [this, Click]
        {
            Click(EKeys::One);
            Pawn->SetPerspective(0);
        },
        [] {});
#endif
}
