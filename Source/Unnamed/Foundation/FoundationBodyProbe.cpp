#include "Foundation/FoundationRuntimeProbe.h"
#include "Foundation/FoundationController.h"
#include "Foundation/FoundationPawn.h"
#include "Foundation/FoundationProfile.h"
#include "Input/UnnamedInputSubsystem.h"
#include "Camera/CameraComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "HAL/IConsoleManager.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "UnrealClient.h"

#if WITH_DEV_AUTOMATION_TESTS
namespace
{
    // An open stretch of the Playground's dev grid, 200 m east and 200 m south of spawn: clear of the stations, the
    // galleries (the registry gallery grew over the old spot at (-30, 30) m) and the course. The player faces and
    // walks east (+X).
    const FVector BodyProbeSpot(20000, 20000, 0);

    // Sets an integer console variable; returns its previous value (0 when it does not exist).
    int32 SetBodyProbeCVar(const TCHAR* Name, int32 Value)
    {
        IConsoleVariable* Variable = IConsoleManager::Get().FindConsoleVariable(Name);
        if (!Variable) return 0;
        const int32 Previous = Variable->GetInt();
        Variable->Set(Value, ECVF_SetByCode);
        return Previous;
    }

    // One frame of the walking feet: the body's transform and the balls of the feet in component space.
    struct FBodyFootSample
    {
        FTransform Component;
        FVector Ball[2];
        double DeltaMs = 0;
    };

    // The largest drift (cm, horizontal) of a planted ball of the foot across frames, Lag frames between the body's
    // transform and its pose; Runs counts the plants seen.
    double MeasureBodyFootSlide(const TArray<FBodyFootSample>& Samples, int32 Lag, int32& Runs)
    {
        double Worst = 0;
        Runs = 0;
        for (int32 Foot = 0; Foot < 2; ++Foot)
        {
            TArray<FVector> World;
            for (int32 Index = Lag; Index < Samples.Num(); ++Index)
                World.Add(Samples[Index - Lag].Component.TransformPosition(Samples[Index].Ball[Foot]));
            double Lowest = TNumericLimits<double>::Max();
            for (const FVector& Point : World)
                Lowest = FMath::Min(Lowest, Point.Z);
            // A plant: consecutive frames with the ball within 0.5 cm of its lowest, the foot flat. (At toe-off the
            // ball joint lifts and rolls forward over the planted toes: 2.5 cm forward for its first 1 cm up.)
            int32 Start = INDEX_NONE;
            for (int32 Index = 0; Index <= World.Num(); ++Index)
            {
                const bool bPlanted = Index < World.Num() && World[Index].Z < Lowest + .5;
                if (bPlanted && Start == INDEX_NONE) Start = Index;
                if (!bPlanted && Start != INDEX_NONE)
                {
                    if (Index - Start >= 2)
                    {
                        ++Runs;
                        for (int32 Other = Start; Other < Index; ++Other)
                            Worst = FMath::Max(Worst, FVector::Dist2D(World[Start], World[Other]));
                    }
                    Start = INDEX_NONE;
                }
            }
        }
        return Worst;
    }
}
#endif

void UFoundationRuntimeProbe::AddBodyCases()
{
#if WITH_DEV_AUTOMATION_TESTS
    if (!Pawn->GetBodyMesh())
    {
        AddCase(
            TEXT("body present"), .1, [] {},
            [this]
            {
                Record(TEXT("body present"), false,
                    TEXT("no player body: run Scripts/Editor/ImportUnnamedPlayer.py and ImportBetterSteve.py (see the "
                         "log's Player body line)"));
            });
        return;
    }
    if (FParse::Param(FCommandLine::Get(), TEXT("FoundationBodySweep")))
    {
        AddFirstPersonBodySweep();
        return;
    }
    AddCase(
        TEXT("body set"), .1, [] {},
        [this]
        {
            Record(TEXT("body is the Unnamed Player (foundation.Body.Set 1)"),
                Pawn->GetBodySet() == EFoundationBodySet::UnnamedPlayer,
                FoundationBodyClip::GetMeshName(Pawn->GetBodySet()));
        });
    using namespace FoundationBodyClip;
    auto Key = [this](const TCHAR* Id) { return Controller->GetInputSubsystem()->GetCurrentKey(Id); };
    auto Shot = [this](const FString& Name)
    {
        FScreenshotRequest::RequestScreenshot(
            FPaths::ChangeExtension(Output, TEXT("")) + TEXT("-body-") + Name + TEXT(".png"), true, false);
    };
    // The view: 0 first person, 1 back, 2 front, 3 the spring arm (back view with foundation.View.SpringArm 1).
    auto Stand = [this](int32 View, int32 Yaw, FRotator Look)
    {
        SetBodyProbeCVar(TEXT("foundation.View.SpringArm"), View == 3);
        SetBodyProbeCVar(TEXT("foundation.Body.Yaw"), Yaw);
        ResetPlayer(BodyProbeSpot);
        Pawn->SetPerspective(View == 3 ? 1 : View);
        Controller->SetControlRotation(Look);
        Pawn->ResetBody();
    };
    auto Describe = [this]
    {
        FString Text;
        const EFoundationBodySet Set = Pawn->GetBodySet();
        for (const FFoundationBodyLayer& Layer : Pawn->BodyAnimator.Pose.Base)
            Text += FString::Printf(
                TEXT("%s %.2f @%.2f; "), *FoundationBodyClip::GetName(Layer.Clip, Set), Layer.Weight, Layer.Time);
        for (const FFoundationBodyLayer& Layer : Pawn->BodyAnimator.Pose.Look)
            Text += FString::Printf(TEXT("%s %.2f; "), *FoundationBodyClip::GetName(Layer.Clip, Set), Layer.Weight);
        return Text;
    };
    auto ReleaseAll = [this]
    {
        auto Keys = DownKeys;
        for (FKey Down : Keys)
            PressKey(Down, false);
    };

    AddCase(
        TEXT("body first person"), .5, [Stand] { Stand(0, 1, FRotator(-10, 0, 0)); },
        [this]
        {
            Record(TEXT("body: the world body is not drawn for the owner in first person"), !Pawn->IsBodyShown(),
                TEXT("perspective 0"));
        });

    // Each view, each pose: a shot and the clip the animator picked.
    struct FBodyPose
    {
        const TCHAR* Name;
        TArray<const TCHAR*> Keys;
        double Pitch, Seconds;
        int32 Clip;
        float MinWeight;
    };
    const TArray<FBodyPose> Poses = {{TEXT("idle"), {}, -10, 1, Idle, .99f},
        {TEXT("walk"), {TEXT("key.forward")}, -10, 1.5, Walk, .99f},
        {TEXT("sprint"), {TEXT("key.forward"), TEXT("key.sprint")}, -10, 1.5, Sprint, .99f},
        {TEXT("crouch"), {TEXT("key.sneak")}, -10, 1, CrouchIdle, .99f},
        {TEXT("crouch-walk"), {TEXT("key.sneak"), TEXT("key.forward")}, -10, 1.5, CrouchWalk, .99f},
        {TEXT("look-up"), {}, 60, 1, Look + 7, .6f}, {TEXT("look-down"), {}, -60, 1, Look + 1, .6f}};
    const TCHAR* const ViewNames[] = {TEXT("first"), TEXT("back"), TEXT("front"), TEXT("arm")};
    for (int32 View = 1; View <= 3; ++View)
    {
        const FString ViewName = ViewNames[View];
        for (const FBodyPose& Pose : Poses)
        {
            const FString Name = ViewName + TEXT("-") + Pose.Name;
            AddCase(
                TEXT("body ") + Name, Pose.Seconds,
                [this, Stand, Key, View, Pose]
                {
                    Stand(View, 1, FRotator(Pose.Pitch, 0, 0));
                    for (const TCHAR* Id : Pose.Keys)
                        PressKey(Key(Id), true);
                },
                [this, Shot, Describe, Name, Pose]
                {
                    Shot(Name);
                    Record(TEXT("body ") + Name + TEXT(" shown"), Pawn->IsBodyShown(), TEXT(""));
                    Record(TEXT("body ") + Name + TEXT(" plays ") + FoundationBodyClip::GetName(Pose.Clip),
                        Pawn->BodyAnimator.Pose.GetWeight(Pose.Clip) >= Pose.MinWeight, Describe());
                });
            AddCase(TEXT("body flush ") + Name, .3, [] {}, ReleaseAll);
        }
        // The jump: settle, press jump; the shot is 0.3 s into the rise.
        AddCase(
            TEXT("body ") + ViewName + TEXT("-jump settle"), .6, [Stand, View] { Stand(View, 1, FRotator(-10, 0, 0)); },
            [] {});
        AddCase(
            TEXT("body ") + ViewName + TEXT("-jump"), .3, [this, Key] { PressKey(Key(TEXT("key.jump")), true); },
            [this, Shot, Describe, ViewName]
            {
                Shot(ViewName + TEXT("-jump"));
                Record(TEXT("body ") + ViewName + TEXT("-jump plays the jump"),
                    !Pawn->Simulation.bGrounded &&
                        Pawn->BodyAnimator.Pose.GetWeight(JumpStart) + Pawn->BodyAnimator.Pose.GetWeight(JumpLoop) >
                            .9f,
                    Describe());
            });
        // Released at once, so the held key does not jump again on landing.
        AddCase(TEXT("body flush ") + ViewName + TEXT("-jump"), .8, ReleaseAll, [] {});
    }

    // The yaw taste call: strafing right in the back view, the body facing the view (0) or vanilla's (1); and the
    // spring arm walking toward the camera with the body facing its travel (2).
    struct FBodyYawShot
    {
        const TCHAR* Name;
        int32 View, Yaw;
        const TCHAR* Id;
    };
    const FBodyYawShot YawShots[] = {{TEXT("back-strafe-yaw0"), 1, 0, TEXT("key.right")},
        {TEXT("back-strafe-yaw1"), 1, 1, TEXT("key.right")}, {TEXT("arm-back-yaw1"), 3, 1, TEXT("key.back")},
        {TEXT("arm-back-yaw2"), 3, 2, TEXT("key.back")}};
    for (const FBodyYawShot& YawShot : YawShots)
    {
        const FString CaseName = YawShot.Name;
        const int32 View = YawShot.View, Yaw = YawShot.Yaw;
        const TCHAR* Id = YawShot.Id;
        AddCase(
            TEXT("body ") + CaseName, 1.5,
            [this, Stand, Key, View, Yaw, Id]
            {
                Stand(View, Yaw, FRotator(-10, 0, 0));
                PressKey(Key(Id), true);
            },
            [this, Shot, Describe, CaseName]
            {
                Shot(CaseName);
                Record(TEXT("body ") + CaseName, Pawn->IsBodyShown(), Describe());
            });
        AddCase(TEXT("body flush ") + CaseName, .3, [] {}, ReleaseAll);
    }

    // Planted feet: walking east in the back view, a planted ball of the foot stays put on the ground.
    TSharedRef<TArray<FBodyFootSample>> Feet = MakeShared<TArray<FBodyFootSample>>();
    // The parallel animation switches the foot case turns off, restored as they were.
    TSharedRef<TPair<int32, int32>> ParallelAnim = MakeShared<TPair<int32, int32>>(1, 1);
    AddCase(
        TEXT("body foot slide"), 2.5,
        [this, Stand, Key, Feet, ParallelAnim]
        {
            Stand(1, 1, FRotator(-10, 0, 0));
            PressKey(Key(TEXT("key.forward")), true);
            Feet->Reset();
            // Sampled after the body's tick, its pose evaluated in that tick (no worker thread finishing later), so
            // each sample pairs one frame's pose with its transform, as that frame is drawn.
            AddTickPrerequisiteComponent(Pawn->GetBodyMesh());
            ParallelAnim->Key = SetBodyProbeCVar(TEXT("a.ParallelAnimEvaluation"), 0);
            ParallelAnim->Value = SetBodyProbeCVar(TEXT("a.ParallelAnimUpdate"), 0);
            FrameHook = [this, Feet]
            {
                USkeletalMeshComponent* Mesh = Pawn->GetBodyMesh();
                if (FPlatformTime::Seconds() - PhaseStarted < 1) return;
                FBodyFootSample Sample;
                Sample.Component = Mesh->GetComponentTransform();
                Sample.DeltaMs = GetWorld()->GetDeltaSeconds() * 1000.;
                Sample.Ball[0] =
                    Mesh->GetBoneTransform(Mesh->GetBoneIndex(TEXT("ball_l")), FTransform::Identity).GetLocation();
                Sample.Ball[1] =
                    Mesh->GetBoneTransform(Mesh->GetBoneIndex(TEXT("ball_r")), FTransform::Identity).GetLocation();
                Feet->Add(Sample);
            };
        },
        [this, Feet, ReleaseAll, ParallelAnim]
        {
            ReleaseAll();
            RemoveTickPrerequisiteComponent(Pawn->GetBodyMesh());
            SetBodyProbeCVar(TEXT("a.ParallelAnimEvaluation"), ParallelAnim->Key);
            SetBodyProbeCVar(TEXT("a.ParallelAnimUpdate"), ParallelAnim->Value);
            int32 Runs = 0, LagRuns = 0;
            const double Slide = MeasureBodyFootSlide(*Feet, 0, Runs);
            // For scale: a pose paired with the previous frame's transform (what a one-frame lag would draw).
            const double LagSlide = MeasureBodyFootSlide(*Feet, 1, LagRuns);
            double MinMs = 1e9, MaxMs = 0;
            // <report>-body-feet.csv: each frame's time step, the body's position and both balls of the feet.
            FString Csv = TEXT("delta_ms,body_x,body_y,body_z,ball_l_x,ball_l_y,ball_l_z,ball_r_x,ball_r_y,ball_r_z\n");
            for (const FBodyFootSample& Sample : *Feet)
            {
                MinMs = FMath::Min(MinMs, Sample.DeltaMs);
                MaxMs = FMath::Max(MaxMs, Sample.DeltaMs);
                const FVector Body = Sample.Component.GetLocation();
                const FVector Left = Sample.Component.TransformPosition(Sample.Ball[0]);
                const FVector Right = Sample.Component.TransformPosition(Sample.Ball[1]);
                Csv += FString::Printf(TEXT("%.3f,%.3f,%.3f,%.3f,%.3f,%.3f,%.3f,%.3f,%.3f,%.3f\n"), Sample.DeltaMs,
                    Body.X, Body.Y, Body.Z, Left.X, Left.Y, Left.Z, Right.X, Right.Y, Right.Z);
            }
            FFileHelper::SaveStringToFile(Csv, *(FPaths::ChangeExtension(Output, TEXT("")) + TEXT("-body-feet.csv")));
            Record(TEXT("body planted feet stay put while walking"), Slide < 1 && Runs >= 3,
                FString::Printf(TEXT("%d frames (%.1f-%.1f ms); max planted drift %.2f cm over %d plants (limit 1 "
                                     "cm); paired with the previous frame's transform it would be %.2f cm"),
                    Feet->Num(), MinMs, MaxMs, Slide, Runs, LagSlide));
        });

    // The spring arm's length in the open, then pulled in by the ground when looking up.
    AddCase(
        TEXT("body spring arm open"), 1, [Stand] { Stand(3, 1, FRotator::ZeroRotator); },
        [this]
        {
            const double Distance = FVector::Dist(Pawn->GetCamera()->GetComponentLocation(), Pawn->GetEyePosition());
            Measure(TEXT("body spring arm length in the open (cm)"), Distance,
                FMath::Sqrt(325. * 325 + 55 * 55 + 30 * 30), 3);
        });
    AddCase(
        TEXT("body spring arm ground"), 1, [Stand] { Stand(3, 1, FRotator(80, 0, 0)); },
        [this, Shot]
        {
            Shot(TEXT("arm-ground"));
            const FVector Camera = Pawn->GetCamera()->GetComponentLocation();
            const double Distance = FVector::Dist(Camera, Pawn->GetEyePosition());
            Record(TEXT("body spring arm pulled in by the ground"), Camera.Z >= 4.9 && Distance < 250,
                FString::Printf(TEXT("camera z %.1f cm, %.1f cm from the eyes"), Camera.Z, Distance));
        });
    if (Pawn->GetBodySet() == EFoundationBodySet::UnnamedPlayer)
    {
        AddFirstPersonBodyCases();
        AddStrikeBodyCases();
    }
    AddCase(
        TEXT("body restore"), .3,
        [this]
        {
            SetBodyProbeCVar(TEXT("foundation.View.SpringArm"), 0);
            SetBodyProbeCVar(TEXT("foundation.Body.Yaw"), 1);
            ResetPlayer();
            Pawn->SetPerspective(0);
        },
        [] {});
#endif
}
