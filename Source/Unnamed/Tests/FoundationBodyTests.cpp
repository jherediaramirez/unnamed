#include "Misc/AutomationTest.h"
#include "Animation/AnimSequence.h"
#include "Animation/Skeleton.h"
#include "Engine/SkeletalMesh.h"
#include "Foundation/FoundationBody.h"
#include "UnnamedProjectSettings.h"

#if WITH_DEV_AUTOMATION_TESTS
namespace
{
    constexpr EAutomationTestFlags BodyTestFlags =
        EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter;

    // Holds a frame for Seconds at 60 fps (the smoothed velocity settles within a few frames).
    void HoldBodyFrame(FFoundationBodyAnimator& Animator, const FFoundationBodyFrame& Frame, double Seconds)
    {
        for (double Time = 0; Time < Seconds - 1e-9; Time += 1 / 60.)
            Animator.Advance(1 / 60., Frame);
    }

    FFoundationBodyFrame MakeBodyFrame(FVector2D Velocity, double BodyYaw = 0)
    {
        FFoundationBodyFrame Frame;
        Frame.Velocity = Velocity;
        Frame.BodyYaw = Frame.ViewYaw = BodyYaw;
        return Frame;
    }

    double SumBodyWeights(const FFoundationBodyPose& Pose, bool bLook)
    {
        double Sum = 0;
        for (const FFoundationBodyLayer& Layer : bLook ? TArrayView<const FFoundationBodyLayer>(Pose.Look)
                                                       : TArrayView<const FFoundationBodyLayer>(Pose.Base))
            Sum += Layer.Weight;
        return Sum;
    }
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFoundationBodyTurnTest, "Unnamed.Foundation.Body.Turn", BodyTestFlags)

bool FFoundationBodyTurnTest::RunTest(const FString& Parameters)
{
    const EFoundationBodyYaw Vanilla = EFoundationBodyYaw::Vanilla;
    FFoundationBodyTurn Turn;
    Turn.Reset(0);
    // Standing: the head turns alone up to 50 degrees, then drags the body.
    Turn.Step(FVector2D::ZeroVector, 30, Vanilla);
    TestEqual(TEXT("standing, head 30 off: the body stays"), Turn.Yaw, 0., 1e-9);
    Turn.Step(FVector2D::ZeroVector, 80, Vanilla);
    TestEqual(TEXT("standing, head 80 off: the body is dragged to 50 off"), Turn.Yaw, 30., 1e-9);
    // Walking forward: the body turns 0.3 of the way per step toward the travel direction.
    Turn.Reset(0);
    Turn.Step(FVector2D(0, 21.6), 0, Vanilla);
    TestEqual(TEXT("strafing right: 0.3 of the way to 90"), Turn.Yaw, 27., 1e-9);
    for (int32 Step = 0; Step < 40; ++Step)
        Turn.Step(FVector2D(0, 21.6), 0, Vanilla);
    TestEqual(TEXT("strafing right: the head limit holds the body at 50"), Turn.Yaw, 50., 1e-9);
    Turn.Reset(0);
    for (int32 Step = 0; Step < 40; ++Step)
        Turn.Step(FVector2D(-21.6, 0), 0, Vanilla);
    TestEqual(TEXT("walking backward: the body keeps facing forward"), Turn.Yaw, 0., 1e-9);
    Turn.Reset(0);
    Turn.Step(FVector2D(4, 0).GetRotated(120), 0, Vanilla);
    TestEqual(TEXT("below 5 cm a step the body keeps its yaw"), Turn.Yaw, 0., 1e-9);
    // Travel: backward travel turns the body around; View follows the view.
    Turn.Reset(0);
    for (int32 Step = 0; Step < 60; ++Step)
        Turn.Step(FVector2D(-21.6, 0), 0, EFoundationBodyYaw::Travel);
    TestEqual(TEXT("travel: walking backward faces the travel"), FMath::Abs(Turn.Yaw), 180., 1e-6);
    Turn.Step(FVector2D(21.6, 0), 135, EFoundationBodyYaw::View);
    TestEqual(TEXT("view: the body faces the view"), Turn.GetYaw(.5, 140, EFoundationBodyYaw::View), 140., 1e-9);
    // Rendering interpolates across the +-180 seam.
    Turn.PreviousYaw = 170;
    Turn.Yaw = -170;
    TestEqual(TEXT("interpolates across 180"), FMath::Abs(Turn.GetYaw(.5, 0, Vanilla)), 180., 1e-9);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFoundationBodyLocomotionTest, "Unnamed.Foundation.Body.Locomotion", BodyTestFlags)

bool FFoundationBodyLocomotionTest::RunTest(const FString& Parameters)
{
    using namespace FoundationBodyClip;
    {
        FFoundationBodyAnimator Animator;
        HoldBodyFrame(Animator, MakeBodyFrame(FVector2D::ZeroVector), .5);
        TestEqual(TEXT("standing still plays the idle"), Animator.Pose.GetWeight(Idle), 1.f);
    }
    // Keyboard input lands exactly on a clip: each at the game's straight and diagonal speeds.
    const struct
    {
        const TCHAR* Name;
        double Speed, TravelYaw, BodyYaw;
        int32 Clip;
    } Cases[] = {{TEXT("walk forward"), 431.72, 0, 0, Walk}, {TEXT("walk forward-right"), 440.53, 45, 0, Walk + 1},
        {TEXT("walk right of the body"), 431.72, 120, 30, Walk + 2},
        {TEXT("walk backward-left"), 440.53, -135, 0, Walk + 5}, {TEXT("sprint forward"), 561.23, 10, 10, Sprint},
        {TEXT("sprint forward-left"), 572.69, -45, 0, Sprint + 7}};
    for (const auto& Case : Cases)
    {
        FFoundationBodyAnimator Animator;
        const FVector2D Velocity = FVector2D(Case.Speed, 0).GetRotated(Case.TravelYaw);
        HoldBodyFrame(Animator, MakeBodyFrame(Velocity, Case.BodyYaw), .5);
        TestEqual(FString(Case.Name) + TEXT(": its clip alone"), Animator.Pose.GetWeight(Case.Clip), 1.f, 1e-4f);
        TestEqual(FString(Case.Name) + TEXT(": weights sum to 1"), SumBodyWeights(Animator.Pose, false), 1., 1e-5);
    }
    {
        // Between walk and sprint (the sprint's acceleration) both blend; directions between two clips share a phase.
        FFoundationBodyAnimator Animator;
        HoldBodyFrame(Animator, MakeBodyFrame(FVector2D((431.72 + 561.23) / 2, 0)), .5);
        TestEqual(TEXT("half way to sprint: walk"), Animator.Pose.GetWeight(Walk), .5f, 1e-3f);
        TestEqual(TEXT("half way to sprint: sprint"), Animator.Pose.GetWeight(Sprint), .5f, 1e-3f);
        HoldBodyFrame(Animator, MakeBodyFrame(FVector2D(436, 0).GetRotated(20)), .5);
        const FFoundationBodyLayer* F = Animator.Pose.Base.FindByPredicate([](auto& L) { return L.Clip == Walk; });
        const FFoundationBodyLayer* FR = Animator.Pose.Base.FindByPredicate([](auto& L) { return L.Clip == Walk + 1; });
        TestTrue(TEXT("20 degrees right: walk F and FR"), F && FR);
        if (F && FR)
            TestEqual(TEXT("one phase for both"), F->Time / Animator.Lengths[Walk],
                FR->Time / Animator.Lengths[Walk + 1], 1e-5);
    }
    {
        // A frame's pose is at the frame's own time, as the body's position is (no frame of lag).
        FFoundationBodyAnimator Animator;
        HoldBodyFrame(Animator, MakeBodyFrame(FVector2D::ZeroVector), .5);
        Animator.Advance(.1, MakeBodyFrame(FVector2D(431.72, 0)));
        const FFoundationBodyLayer* F = Animator.Pose.Base.FindByPredicate([](auto& L) { return L.Clip == Walk; });
        TestTrue(TEXT("the first walking frame's walk is 0.1 s in"), F && FMath::IsNearlyEqual(F->Time, .1f, 1e-5f));
    }
    {
        // At the walk speed the cycle runs at its authored rate, so a planted foot moves with the ground.
        FFoundationBodyAnimator Animator;
        HoldBodyFrame(Animator, MakeBodyFrame(FVector2D(431.72, 0)), .5);
        const double Before = Animator.StandPhase;
        Animator.Advance(.1, MakeBodyFrame(FVector2D(431.72, 0)));
        TestEqual(TEXT("walk phase rate = 1 / cycle"), FMath::Frac(Animator.StandPhase - Before + 1),
            .1 / Animator.Lengths[Walk], 1e-6);
        Animator.Advance(.1, MakeBodyFrame(FVector2D(800, 0)));
        const double Fast = Animator.StandPhase;
        Animator.Advance(.1, MakeBodyFrame(FVector2D(800, 0)));
        TestTrue(TEXT("past the sprint speed the sprint plays faster"),
            FMath::Frac(Animator.StandPhase - Fast + 1) > .1 / Animator.Lengths[Sprint] * 1.3);
    }
    {
        FFoundationBodyAnimator Animator;
        FFoundationBodyFrame Frame = MakeBodyFrame(FVector2D(129.52, 0));
        Frame.bCrouched = true;
        Frame.CrouchAmount = 1;
        HoldBodyFrame(Animator, Frame, .5);
        TestEqual(TEXT("crouch walk forward"), Animator.Pose.GetWeight(CrouchWalk), 1.f, 1e-4f);
        Frame.Velocity = FVector2D(183.16, 0).GetRotated(-45);
        HoldBodyFrame(Animator, Frame, .5);
        TestEqual(TEXT("crouch walk forward-left"), Animator.Pose.GetWeight(CrouchWalk + 7), 1.f, 1e-4f);
    }
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFoundationBodyCrouchTest, "Unnamed.Foundation.Body.Crouch", BodyTestFlags)

bool FFoundationBodyCrouchTest::RunTest(const FString& Parameters)
{
    using namespace FoundationBodyClip;
    FFoundationBodyAnimator Animator;
    FFoundationBodyFrame Frame = MakeBodyFrame(FVector2D::ZeroVector);
    HoldBodyFrame(Animator, Frame, .5);
    // The eyes go halfway down each reference step (FFoundationSimulation), as the transition clips are eased; on the
    // toggle's frame they have not moved yet.
    Frame.bCrouched = true;
    Animator.Advance(1 / 60., Frame);
    TestEqual(TEXT("crouching while still plays StandToCrouch"), Animator.Pose.GetWeight(StandToCrouch), 1.f);
    TestEqual(TEXT("from its start"), Animator.Pose.Base[0].Time, 0.f, 1e-6f);
    Frame.CrouchAmount = 1;
    HoldBodyFrame(Animator, Frame, .5);
    TestEqual(TEXT("then the crouch idle"), Animator.Pose.GetWeight(CrouchIdle), 1.f);
    Frame.bCrouched = false;
    Animator.Advance(1 / 60., Frame);
    TestEqual(TEXT("standing up plays CrouchToStand"), Animator.Pose.GetWeight(CrouchToStand), 1.f);
    // Toggled back with the eyes 3/4 down (1/4 of the way still to go, two half-lives): StandToCrouch from 0.1 s, not
    // from its start.
    Frame.bCrouched = true;
    Frame.CrouchAmount = .75;
    Animator.Advance(1 / 60., Frame);
    TestEqual(TEXT("toggled back mid-way: StandToCrouch"), Animator.Pose.GetWeight(StandToCrouch), 1.f);
    TestEqual(TEXT("toggled back mid-way: where the eyes are"), Animator.Pose.Base[0].Time, .1f, 1e-5f);
    Frame.bCrouched = false;
    Frame.CrouchAmount = .5;
    // Moving, the stances blend by the eye height instead.
    Frame.Velocity = FVector2D(300, 0);
    HoldBodyFrame(Animator, Frame, .5);
    TestEqual(TEXT("moving, half crouched: crouch walk half"), Animator.Pose.GetWeight(CrouchWalk), .5f, 1e-4f);
    TestEqual(TEXT("moving: no transition clip"), Animator.Pose.GetWeight(CrouchToStand), 0.f);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFoundationBodyJumpTest, "Unnamed.Foundation.Body.Jump", BodyTestFlags)

bool FFoundationBodyJumpTest::RunTest(const FString& Parameters)
{
    using namespace FoundationBodyClip;
    FFoundationBodyAnimator Animator;
    FFoundationBodyFrame Frame = MakeBodyFrame(FVector2D::ZeroVector);
    HoldBodyFrame(Animator, Frame, .5);
    Frame.bGrounded = false;
    Frame.Jumps = 1;
    Animator.Advance(1 / 60., Frame);
    Frame.Jumps = 0;
    HoldBodyFrame(Animator, Frame, .15);
    TestEqual(TEXT("a jump plays Start"), Animator.Pose.GetWeight(JumpStart), 1.f);
    HoldBodyFrame(Animator, Frame, .25);
    TestEqual(TEXT("then the air loop"), Animator.Pose.GetWeight(JumpLoop), 1.f);
    Frame.bGrounded = true;
    HoldBodyFrame(Animator, Frame, .15);
    TestEqual(TEXT("touchdown plays Land"), Animator.Pose.GetWeight(JumpLand), 1.f);
    // A jump straight out of the landing: Land fades out under the new jump instead of vanishing.
    Frame.bGrounded = false;
    Frame.Jumps = 1;
    Animator.Advance(1 / 60., Frame);
    Frame.Jumps = 0;
    TestTrue(TEXT("jumping from a landing: Land fades"), Animator.Pose.GetWeight(JumpLand) > .5f);
    TestEqual(TEXT("jumping from a landing: no idle pop"), Animator.Pose.GetWeight(Idle), 0.f);
    HoldBodyFrame(Animator, Frame, .2);
    TestEqual(TEXT("jumping from a landing: then Start"), Animator.Pose.GetWeight(JumpStart), 1.f);
    Frame.bGrounded = true;
    HoldBodyFrame(Animator, Frame, .15);
    HoldBodyFrame(Animator, Frame, .5);
    TestEqual(TEXT("then the idle"), Animator.Pose.GetWeight(Idle), 1.f);
    // A step down (a short fall) stays on the ground clips; a longer fall loops.
    Frame.bGrounded = false;
    HoldBodyFrame(Animator, Frame, .1);
    TestEqual(TEXT("a short fall keeps the idle"), Animator.Pose.GetWeight(Idle), 1.f);
    HoldBodyFrame(Animator, Frame, .3);
    TestEqual(TEXT("a fall loops"), Animator.Pose.GetWeight(JumpLoop), 1.f);
    // Landing while moving hands straight back to locomotion.
    Frame.bGrounded = true;
    Frame.Velocity = FVector2D(431.72, 0);
    HoldBodyFrame(Animator, Frame, .2);
    TestEqual(TEXT("landing while walking: no Land"), Animator.Pose.GetWeight(JumpLand), 0.f);
    HoldBodyFrame(Animator, Frame, .4);
    TestEqual(TEXT("landing while walking: the walk"), Animator.Pose.GetWeight(Walk), 1.f, 1e-4f);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFoundationBodyLookTest, "Unnamed.Foundation.Body.Look", BodyTestFlags)

bool FFoundationBodyLookTest::RunTest(const FString& Parameters)
{
    using namespace FoundationBodyClip;
    const int32 C = Look + 4, U = Look + 7, R = Look + 5, UR = Look + 8, DL = Look + 0;
    const struct
    {
        double Yaw, Pitch;
        TArray<TPair<int32, float>> Weights;
    } Cases[] = {{0, 0, {{C, 1.f}}}, {0, 90, {{U, 1.f}}}, {45, 45, {{C, .25f}, {R, .25f}, {U, .25f}, {UR, .25f}}},
        {-90, -120, {{DL, 1.f}}}, {0, 315, {{C, .5f}, {Look + 1, .5f}}},
        // A view behind the body turns the head back toward the front.
        {150, 0, {{C, 2 / 3.f}, {R, 1 / 3.f}}}, {180, 0, {{C, 1.f}}}};
    for (const auto& Case : Cases)
    {
        FFoundationBodyAnimator Animator;
        FFoundationBodyFrame Frame = MakeBodyFrame(FVector2D::ZeroVector, 20);
        Frame.ViewYaw = 20 + Case.Yaw;
        Frame.ViewPitch = Case.Pitch;
        Animator.Advance(1 / 60., Frame);
        const FString Name = FString::Printf(TEXT("look yaw %g pitch %g"), Case.Yaw, Case.Pitch);
        for (const auto& [Clip, Weight] : Case.Weights)
            TestEqual(Name + TEXT(": ") + GetName(Clip), Animator.Pose.GetWeight(Clip), Weight, 1e-5f);
        TestEqual(Name + TEXT(": weights sum to 1"), SumBodyWeights(Animator.Pose, true), 1., 1e-5);
    }
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFoundationBodyAssetsTest, "Unnamed.Foundation.Body.Assets", BodyTestFlags)

bool FFoundationBodyAssetsTest::RunTest(const FString& Parameters)
{
    using namespace FoundationBodyClip;
    const struct
    {
        EFoundationBodySet Set;
        const TCHAR* Script;
        double HeightCm;
    } Sets[] = {{EFoundationBodySet::BetterSteve, TEXT("ImportBetterSteve.py"), 187.5},
        {EFoundationBodySet::UnnamedPlayer, TEXT("ImportUnnamedPlayer.py"), 180}};
    for (const auto& Body : Sets)
    {
        const FString MeshName = GetMeshName(Body.Set);
        auto* Mesh = Cast<USkeletalMesh>(GetAssetPath(Body.Set, MeshName).TryLoad());
        if (!TestNotNull(MeshName + TEXT(" (Scripts/Editor/") + Body.Script + TEXT(")"), Mesh)) continue;
        TestEqual(MeshName + TEXT(" height"), Mesh->GetBounds().BoxExtent.Z * 2, Body.HeightCm, .5);
        for (int32 Clip = 0; Clip < Count; ++Clip)
        {
            const FString Name = GetName(Clip, Body.Set);
            if (Name.IsEmpty()) continue;
            auto* Sequence = Cast<UAnimSequence>(GetAssetPath(Body.Set, Name).TryLoad());
            if (!TestNotNull(Name, Sequence)) continue;
            TestTrue(Name + TEXT(" uses the body's skeleton"), Sequence->GetSkeleton() == Mesh->GetSkeleton());
            // Better Steve's look poses and idle have their own lengths; every Unnamed Player clip has the manifest's.
            const bool bOwnLength = Body.Set == EFoundationBodySet::BetterSteve && (Clip >= Look || Clip == Idle);
            TestEqual(Name + TEXT(" length"), double(Sequence->GetPlayLength()),
                bOwnLength ? double(Sequence->GetPlayLength()) : GetDefaultLength(Clip, Body.Set), .02);
            TestEqual(Name + TEXT(" additive"), int32(Sequence->AdditiveAnimType),
                int32(Clip >= Look && Clip < Low ? AAT_RotationOffsetMeshSpace : AAT_None));
        }
    }
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFoundationBodySetsTest, "Unnamed.Foundation.Body.Sets", BodyTestFlags)

bool FFoundationBodySetsTest::RunTest(const FString& Parameters)
{
    using namespace FoundationBodyClip;
    const EFoundationBodySet BS = EFoundationBodySet::BetterSteve, UP = EFoundationBodySet::UnnamedPlayer;
    TestEqual(TEXT("UP crouch walk"), GetName(CrouchWalk + 1, UP), FString(TEXT("UP_Crouch_FR")));
    TestEqual(TEXT("UP air loop: the fall"), GetName(JumpLoop, UP), FString(TEXT("UP_Jump_Fall")));
    TestEqual(TEXT("UP land: soft"), GetName(JumpLand, UP), FString(TEXT("UP_Land_Soft")));
    TestEqual(TEXT("UP crouch look"), GetName(CrouchLook + 7, UP), FString(TEXT("UP_CrouchLook_U")));
    TestEqual(TEXT("UP low pose"), GetName(Low, UP), FString(TEXT("UP_Low_Pose")));
    TestEqual(TEXT("BS crouch look: its one grid"), GetName(CrouchLook + 7, BS), FString(TEXT("BS_Look_U")));
    TestTrue(TEXT("BS has no low pose"), GetName(Low, BS).IsEmpty() && GetDefaultLength(Low, BS) == 0);
    TestEqual(TEXT("UP walk: 36 frames at 60"), GetDefaultLength(Walk, UP), .6, 1e-9);
    TestEqual(TEXT("UP crouch walk: 54 frames at 60"), GetDefaultLength(CrouchWalk, UP), .9, 1e-9);
    for (int32 Clip = 0; Clip < Count; ++Clip)
        TestFalse(FString::Printf(TEXT("UP clip %d has a name"), Clip), GetName(Clip, UP).IsEmpty());
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFoundationBodyLowTest, "Unnamed.Foundation.Body.Low", BodyTestFlags)

bool FFoundationBodyLowTest::RunTest(const FString& Parameters)
{
    using namespace FoundationBodyClip;
    {
        FFoundationBodyAnimator Animator(EFoundationBodySet::UnnamedPlayer);
        FFoundationBodyFrame Frame = MakeBodyFrame(FVector2D::ZeroVector);
        HoldBodyFrame(Animator, Frame, .5);
        Frame.bLowPose = true;
        Frame.bGrounded = false;
        Animator.Advance(1 / 60., Frame);
        TestTrue(TEXT("the low pose fades in"), Animator.Pose.GetWeight(Low) > 0 && Animator.Pose.GetWeight(Low) < 1);
        HoldBodyFrame(Animator, Frame, .2);
        TestEqual(TEXT("then the low pose alone"), Animator.Pose.GetWeight(Low), 1.f, 1e-5f);
        TestEqual(TEXT("its weight for the corrections"), Animator.Pose.LowWeight, 1.f, 1e-5f);
        TestEqual(TEXT("base weights sum to 1"), SumBodyWeights(Animator.Pose, false), 1., 1e-5);
        Frame.bLowPose = false;
        Frame.bGrounded = true;
        HoldBodyFrame(Animator, Frame, .5);
        TestEqual(TEXT("standing again: no low pose"), Animator.Pose.GetWeight(Low), 0.f);
    }
    {
        FFoundationBodyAnimator Animator(EFoundationBodySet::BetterSteve);
        FFoundationBodyFrame Frame = MakeBodyFrame(FVector2D::ZeroVector);
        Frame.bLowPose = true;
        Frame.bGrounded = false;
        HoldBodyFrame(Animator, Frame, .5);
        TestEqual(TEXT("Better Steve keeps its air loop"), Animator.Pose.GetWeight(JumpLoop), 1.f, 1e-5f);
        TestEqual(TEXT("Better Steve: no low pose"), Animator.Pose.GetWeight(Low), 0.f);
    }
    {
        // The look splits between the standing and crouching grids by the eyes' crouch.
        FFoundationBodyAnimator Animator(EFoundationBodySet::UnnamedPlayer);
        FFoundationBodyFrame Frame = MakeBodyFrame(FVector2D::ZeroVector);
        Frame.bCrouched = true;
        Frame.CrouchAmount = .25;
        Animator.Advance(1 / 60., Frame);
        TestEqual(TEXT("quarter crouched: standing look"), Animator.Pose.GetWeight(Look + 4), .75f, 1e-5f);
        TestEqual(TEXT("quarter crouched: crouching look"), Animator.Pose.GetWeight(CrouchLook + 4), .25f, 1e-5f);
        TestEqual(TEXT("look weights sum to 1"), SumBodyWeights(Animator.Pose, true), 1., 1e-5);
    }
    return true;
}
#endif
