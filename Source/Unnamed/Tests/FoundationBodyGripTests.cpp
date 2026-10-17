#include "Misc/AutomationTest.h"
#include "Foundation/FoundationBodyGrip.h"
#include "Foundation/FoundationBodyPose.h"
#include "Foundation/FoundationItemMesh.h"

#if WITH_DEV_AUTOMATION_TESTS
namespace
{
    constexpr EAutomationTestFlags GripTestFlags =
        EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter;

    FFoundationBodyActionFrame MakeActionFrame(uint32 Serial, int64 Step, double Alpha, bool bHolding = true)
    {
        FFoundationBodyActionFrame Frame;
        Frame.bHolding = bHolding;
        Frame.PressSerial = Serial;
        Frame.Step = Step;
        Frame.Alpha = Alpha;
        return Frame;
    }

    // Holds the pickaxe until the hold is full (the first frame only learns the press counter).
    FFoundationBodyAction MakeHolding(int64 Step)
    {
        FFoundationBodyAction Action;
        for (int32 Frame = 0; Frame < 10; ++Frame)
            Action.Advance(1 / 60., MakeActionFrame(0, Step, 0));
        return Action;
    }
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFoundationBodyStrikeTest, "Unnamed.Foundation.Body.Strike", GripTestFlags)

bool FFoundationBodyStrikeTest::RunTest(const FString& Parameters)
{
    using namespace FoundationBodyClip;
    TestTrue(TEXT("pickaxes are body-held"),
        FoundationBodyGrip::IsBodyHeld(TEXT("iron_pickaxe")) && FoundationBodyGrip::IsBodyHeld(TEXT("wooden_pickaxe")));
    TestFalse(TEXT("a sword is not (yet)"), FoundationBodyGrip::IsBodyHeld(TEXT("iron_sword")));
    TestFalse(TEXT("nothing is not"), FoundationBodyGrip::IsBodyHeld(NAME_None));
    {
        FFoundationBodyAction Action = MakeHolding(100);
        TestEqual(TEXT("holding: the hold is full"), Action.HoldWeight, 1., 1e-9);
        TestFalse(TEXT("holding: no strike without a press"), Action.bStriking);
        FFoundationBodyPose Pose;
        Action.Write(Pose);
        TestEqual(TEXT("holding: the hold pose takes the upper body"), Pose.GetWeight(PickaxeHold), 1.f, 1e-6f);
        // A press seen at step 100: the strike starts there, at once from the full hold.
        Action.Advance(1 / 60., MakeActionFrame(1, 100, 0));
        TestTrue(TEXT("a press starts a strike"), Action.bStriking && Action.StartStep == 100);
        TestEqual(TEXT("at once from the hold"), Action.StrikeWeight, 1., 1e-9);
        TestEqual(TEXT("from frame 0"), Action.StrikeTime, 0., 1e-9);
        // The contact: frame 9 of 18 (0.15 s) as step start + 3 begins, the tick the hit lands.
        Action.Advance(1 / 60., MakeActionFrame(1, 103, 0));
        TestEqual(TEXT("step start + 3: frame 9"), Action.StrikeTime * 60, 9., 1e-9);
        Action.Advance(1 / 60., MakeActionFrame(1, 102, .99));
        TestTrue(TEXT("just before: before frame 9"), Action.StrikeTime * 60 < 9);
        Action.Advance(1 / 60., MakeActionFrame(1, 105, .5));
        TestEqual(TEXT("step 5.5 of 6"), Action.StrikeTime, 5.5 / 6 * .3, 1e-9);
        // A press mid-swing restarts at frame 0, the old strike crossfading out at its last time.
        Action.Advance(1 / 60., MakeActionFrame(2, 105, .5));
        TestTrue(TEXT("restart: a new swing from this step"),
            Action.StartStep == 105 && FMath::IsNearlyEqual(Action.StrikeTime, .5 / 6 * .3, 1e-9));
        TestEqual(TEXT("restart: the old strike fades from its time"), Action.FadingTime, 5.5 / 6 * .3, 1e-9);
        Action.Write(Pose);
        TestEqual(TEXT("restart: weights sum to 1"), Pose.GetWeight(StrikeA) + Pose.GetWeight(PickaxeHold), 1.f, 1e-5f);
        for (int32 Frame = 0; Frame < 4; ++Frame)
            Action.Advance(1 / 60., MakeActionFrame(2, 105, .6));
        TestEqual(TEXT("restart: the crossfade is over in 0.05 s"), Action.FadingWeight, 0., 1e-9);
        // The swing ends after 6 steps (no repeat) and fades back to the hold in 0.1 s.
        Action.Advance(1 / 60., MakeActionFrame(2, 111, 0));
        TestFalse(TEXT("ends after 6 steps"), Action.bStriking);
        TestTrue(TEXT("fading out"), Action.StrikeWeight > 0 && Action.StrikeWeight < 1);
        for (int32 Frame = 0; Frame < 6; ++Frame)
            Action.Advance(1 / 60., MakeActionFrame(2, 111, .5));
        TestEqual(TEXT("back to the hold"), Action.StrikeWeight, 0., 1e-9);
    }
    {
        // Held on a block, the swing repeats with no rest.
        FFoundationBodyAction Action = MakeHolding(10);
        FFoundationBodyActionFrame Frame = MakeActionFrame(1, 10, 0);
        Frame.bRepeating = true;
        Action.Advance(1 / 60., Frame);
        Frame.Step = 17;
        Action.Advance(1 / 60., Frame);
        TestTrue(TEXT("repeating: still striking"), Action.bStriking);
        TestEqual(TEXT("repeating: one step into the second swing"), Action.StrikeTime, .3 / 6, 1e-9);
        Frame.Step = 19;
        Action.Advance(1 / 60., Frame);
        TestEqual(TEXT("repeating: contact again at start + 9"), Action.StrikeTime * 60, 9., 1e-9);
    }
    {
        // Picking the pickaxe up mid-press: the hold fades in over 0.08 s and the strike over 0.05 s.
        FFoundationBodyAction Action;
        Action.Advance(1 / 60., MakeActionFrame(0, 0, 0, false));
        Action.Advance(1 / 60., MakeActionFrame(1, 0, 0, true));
        TestTrue(TEXT("not from a full hold: the strike fades in"),
            Action.StrikeWeight > 0 && Action.StrikeWeight < 1 && Action.HoldWeight < 1);
        // A press without a body-held item starts nothing.
        FFoundationBodyAction Empty;
        Empty.Advance(1 / 60., MakeActionFrame(0, 0, 0, false));
        Empty.Advance(1 / 60., MakeActionFrame(1, 0, 0, false));
        FFoundationBodyPose Pose;
        Empty.Write(Pose);
        TestTrue(TEXT("empty hand: no action"), !Empty.bStriking && Pose.ActionWeight == 0 && Pose.Action.IsEmpty());
    }
    {
        // The aim follows the view within PLAN's limits.
        FFoundationBodyAction Action;
        FFoundationBodyActionFrame Frame = MakeActionFrame(0, 0, 0);
        Frame.ViewPitchDeg = -80;
        Frame.ViewYawOffsetDeg = 45;
        Action.Advance(1 / 60., Frame);
        TestTrue(TEXT("aim clamped to -45 / 30"), Action.AimPitchDeg == -45 && Action.AimYawDeg == 30);
    }
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFoundationBodyItemGripTest, "Unnamed.Foundation.Body.ItemGrip", GripTestFlags)

bool FFoundationBodyItemGripTest::RunTest(const FString& Parameters)
{
    // The pickaxe sprite (model pixels) through the grip, at the view's 50 cm block: the point on the contact, the
    // handle along -Y with the fist near the butt, the sprite flat in the tool's YZ plane.
    const double BlockCm = 50;
    const FTransform Grip = FoundationBodyGrip::MakeItemGrip(BlockCm);
    auto Tool = [&Grip, BlockCm](double X, double Y)
    {
        return Grip.TransformPosition(
            FVector(FoundationItemMesh::ToUnreal(FVector3f(float(X), float(Y), 8.f), BlockCm)));
    };
    TestTrue(TEXT("the pick's point on the authored contact"),
        Tool(5, 12.5).Equals(FoundationBodyGrip::GetStrikeTipCm(), 1e-3));
    const FVector Butt = Tool(2, 1), Corner = Tool(13.5, 12.5), Other = Tool(13.5, 4);
    TestTrue(TEXT("the butt just behind the fist"), Butt.Y > 0 && Butt.Y < 5 && FMath::Abs(Butt.Z) < 1e-3);
    TestTrue(TEXT("the head's corner out along the handle"), Corner.Y < -60 && FMath::Abs(Corner.Z) < 1e-3);
    TestTrue(TEXT("the other point mirrors the first"),
        FMath::IsNearlyEqual(Other.Y, -42.75, 1e-3) && FMath::IsNearlyEqual(Other.Z, -26.5, 1e-3));
    TestTrue(TEXT("the sprite lies flat in the tool's YZ plane"),
        FMath::Abs(Butt.X) < 1e-3 && FMath::Abs(Corner.X) < 1e-3 && FMath::Abs(Other.X) < 1e-3);
    TestEqual(TEXT("4.41 cm a pixel"), Grip.GetScale3D().X * BlockCm / 16, 4.409, .001);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFoundationBodyUpperBodyTest, "Unnamed.Foundation.Body.UpperBody", GripTestFlags)

bool FFoundationBodyUpperBodyTest::RunTest(const FString& Parameters)
{
    // A chain root - pelvis - spine - arm, and a leg off the pelvis.
    const TArray<int32> Parents = {INDEX_NONE, 0, 1, 2, 1};
    TArray<FTransform> Base = {FTransform::Identity, FTransform(FVector(0, 0, 70)),
        FTransform(FRotator(10, 0, 0), FVector(0, 0, 30)), FTransform(FVector(20, 0, 30)),
        FTransform(FVector(10, 0, -3))};
    TArray<FTransform> Action = Base;
    Action[1] = FTransform(FRotator(0, 0, 20), FVector(0, 0, 70)); // the action leans the pelvis (masked out)
    Action[2] = FTransform(FRotator(-30, 0, 0), FVector(0, 0, 30));
    Action[3] = FTransform(FRotator(0, 40, 0), FVector(20, 0, 30));
    const TArray<float> Weights = {0, 0, 1, 1, 0};
    const TArray<bool> MeshSpace = {false, false, true, false, false};
    TArray<FTransform> ActionComponent, Out;
    FoundationBodyPose::ToComponentSpace(Action, Parents, ActionComponent);
    TArray<FTransform> Blended = Base;
    FoundationBodyPose::BlendUpperBody(Blended, Action, Parents, Weights, MeshSpace);
    FoundationBodyPose::ToComponentSpace(Blended, Parents, Out);
    TestTrue(TEXT("the spine takes the action's component-space rotation over the gait's pelvis"),
        Out[2].GetRotation().AngularDistance(ActionComponent[2].GetRotation()) < 1e-6);
    TestTrue(TEXT("the arm takes its parent-relative rotation"),
        Blended[3].GetRotation().AngularDistance(Action[3].GetRotation()) < 1e-6);
    TestTrue(TEXT("the pelvis and leg keep the gait"), Blended[1].Equals(Base[1]) && Blended[4].Equals(Base[4]));

    // The aim pitches the chest's forward (+Y) up and yaws it toward the body's right (-X).
    TArray<FTransform> Chain = {FTransform::Identity, FTransform(FVector(0, 0, 100))};
    const TArray<int32> ChainParents = {INDEX_NONE, 0};
    FoundationBodyPose::ApplyAim(Chain, ChainParents, 1, 30, 0);
    TestTrue(TEXT("aim up 30: forward tilts up"),
        Chain[1]
            .GetRotation()
            .RotateVector(FVector(0, 1, 0))
            .Equals(FVector(0, FMath::Cos(PI / 6), FMath::Sin(PI / 6)), 1e-6));
    Chain[1] = FTransform(FVector(0, 0, 100));
    FoundationBodyPose::ApplyAim(Chain, ChainParents, 1, 0, 90);
    TestTrue(TEXT("aim right 90: forward turns to the body's right"),
        Chain[1].GetRotation().RotateVector(FVector(0, 1, 0)).Equals(FVector(-1, 0, 0), 1e-6));
    return true;
}
#endif
