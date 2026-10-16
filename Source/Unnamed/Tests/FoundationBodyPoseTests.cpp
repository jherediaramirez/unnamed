#include "Misc/AutomationTest.h"
#include "Foundation/FoundationBodyPose.h"

#if WITH_DEV_AUTOMATION_TESTS
namespace
{
    // The Unnamed Player's rest skeleton (export manifest, Blender cm (x, y, z) -> component (x, -y, z)): bone heads,
    // parents before children, no rotations.
    struct FTestSkeleton
    {
        TArray<FTransform> Local;
        TArray<int32> Parents;
        FoundationBodyPose::FBones Bones;
        TArray<FTransform> Component() const
        {
            TArray<FTransform> Out;
            FoundationBodyPose::ToComponentSpace(Local, Parents, Out);
            return Out;
        }
    };

    FTestSkeleton MakeTestSkeleton()
    {
        const struct
        {
            const TCHAR* Name;
            int32 Parent;
            FVector Head;
        } Rest[] = {{TEXT("root"), -1, {0, 0, 0}}, {TEXT("pelvis"), 0, {0, 0, 70}}, {TEXT("spine_01"), 1, {0, 0, 79.9}},
            {TEXT("spine_02"), 2, {0, 0, 101.5}}, {TEXT("neck_01"), 3, {0, 0, 135.5}}, {TEXT("head"), 4, {0, 0, 140}},
            {TEXT("thigh_l"), 1, {10, 0, 67.1}}, {TEXT("calf_l"), 6, {10, 1.2, 36.5}},
            {TEXT("foot_l"), 7, {10, 0, 8.8}}, {TEXT("ball_l"), 8, {10, 5, 1.5}}, {TEXT("thigh_r"), 1, {-10, 0, 67.1}},
            {TEXT("calf_r"), 10, {-10, 1.2, 36.5}}, {TEXT("foot_r"), 11, {-10, 0, 8.8}},
            {TEXT("ball_r"), 12, {-10, 5, 1.5}}};
        FTestSkeleton Skeleton;
        for (const auto& Bone : Rest)
        {
            Skeleton.Parents.Add(Bone.Parent);
            Skeleton.Local.Add(FTransform(Bone.Parent < 0 ? Bone.Head : Bone.Head - Rest[Bone.Parent].Head));
        }
        Skeleton.Bones.Pelvis = 1;
        Skeleton.Bones.Spine[0] = 2;
        Skeleton.Bones.Spine[1] = 3;
        Skeleton.Bones.Neck = 4;
        Skeleton.Bones.Thigh[0] = 6;
        Skeleton.Bones.Calf[0] = 7;
        Skeleton.Bones.Foot[0] = 8;
        Skeleton.Bones.Thigh[1] = 10;
        Skeleton.Bones.Calf[1] = 11;
        Skeleton.Bones.Foot[1] = 12;
        return Skeleton;
    }
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFoundationBodyFirstPersonTest, "Unnamed.Foundation.Body.FirstPerson",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FFoundationBodyFirstPersonTest::RunTest(const FString& Parameters)
{
    const FFoundationFirstPersonTuning Tuning;
    TestEqual(TEXT("no blend looking ahead"), FoundationBodyPose::GetBlend(0, Tuning), 0., 1e-9);
    TestEqual(TEXT("no blend at -20"), FoundationBodyPose::GetBlend(-20, Tuning), 0., 1e-9);
    TestEqual(TEXT("half blend half way"), FoundationBodyPose::GetBlend(-47.5, Tuning), .5, 1e-9);
    TestEqual(TEXT("full blend at -75"), FoundationBodyPose::GetBlend(-75, Tuning), 1., 1e-9);
    TestEqual(TEXT("full blend below"), FoundationBodyPose::GetBlend(-90, Tuning), 1., 1e-9);

    FFoundationFirstPersonView View;
    View.EyeCm = FVector(0, 0, 162);
    View.Forward = FVector(0, 1, 0);
    for (const double Pitch : {-89.9, -60., -30., 0., 30., 60., 89.9})
    {
        FTestSkeleton Skeleton = MakeTestSkeleton();
        const TArray<FTransform> Before = Skeleton.Component();
        View.PitchDeg = Pitch;
        const FFoundationFirstPersonResult Result =
            FoundationBodyPose::Apply(Skeleton.Local, Skeleton.Parents, Skeleton.Bones, View, Tuning);
        const TArray<FTransform> After = Skeleton.Component();
        const FString Name = FString::Printf(TEXT("pitch %g: "), Pitch);
        const double Blend = FoundationBodyPose::GetBlend(Pitch, Tuning);
        // The feet stay where the base pose put them: ankles, balls and the feet's rotation.
        for (const int32 Bone : {8, 9, 12, 13})
        {
            TestTrue(Name + TEXT("foot bone planted"),
                FVector::Dist(Before[Bone].GetLocation(), After[Bone].GetLocation()) < .01);
            TestTrue(Name + TEXT("foot bone keeps its rotation"),
                Before[Bone].GetRotation().AngularDistance(After[Bone].GetRotation()) < 1e-4);
        }
        TestEqual(Name + TEXT("hips back"), Result.PelvisBackCm, 10 * Blend, 1e-6);
        TestEqual(
            Name + TEXT("pelvis moved back"), Before[1].GetLocation().Y - After[1].GetLocation().Y, 10 * Blend, 1e-3);
        TestEqual(Name + TEXT("neck at the anchor"), Result.NeckBehindEyeCm, FMath::Lerp(4., 16., Blend), .01);
        TestTrue(Name + TEXT("spine leans back only, within 35 degrees"),
            Result.SpineBendDeg[0] >= 0 && Result.SpineBendDeg[1] >= 0 && Result.SpineBendDeg[0] <= 35 &&
                Result.SpineBendDeg[1] <= 35);
        TestTrue(Name + TEXT("feet reached"), Result.FootErrorCm < .01);
    }
    {
        // A straight standing leg cannot reach 10 cm further back: the hips drop just enough.
        FTestSkeleton Skeleton = MakeTestSkeleton();
        View.PitchDeg = -90;
        const FFoundationFirstPersonResult Result =
            FoundationBodyPose::Apply(Skeleton.Local, Skeleton.Parents, Skeleton.Bones, View, Tuning);
        TestTrue(TEXT("the hips drop under 3 cm"), Result.PelvisDropCm > 0 && Result.PelvisDropCm < 3);
    }
    {
        // The view turned 60 degrees off the body: the hips and chest move back along the view, not the body.
        FTestSkeleton Skeleton = MakeTestSkeleton();
        View.PitchDeg = -90;
        View.Forward = FVector(0, 1, 0).RotateAngleAxis(60, FVector::UpVector);
        const FFoundationFirstPersonResult Result =
            FoundationBodyPose::Apply(Skeleton.Local, Skeleton.Parents, Skeleton.Bones, View, Tuning);
        TestEqual(TEXT("turned view: neck at the anchor"), Result.NeckBehindEyeCm, 16., .01);
        View.Forward = FVector(0, 1, 0);
    }
    {
        // The eye far behind the neck already: never leans forward.
        FTestSkeleton Skeleton = MakeTestSkeleton();
        View.PitchDeg = -90;
        View.EyeCm = FVector(0, 40, 162);
        const FFoundationFirstPersonResult Result =
            FoundationBodyPose::Apply(Skeleton.Local, Skeleton.Parents, Skeleton.Bones, View, Tuning);
        TestTrue(TEXT("no forward lean"), Result.SpineBendDeg[0] == 0 && Result.SpineBendDeg[1] == 0);
        View.EyeCm = FVector(0, 0, 162);
    }
    {
        // The low pose fades the corrections out: at full weight the pose is untouched.
        FTestSkeleton Skeleton = MakeTestSkeleton();
        const TArray<FTransform> Before = Skeleton.Component();
        View.PitchDeg = -90;
        View.LowWeight = 1;
        FoundationBodyPose::Apply(Skeleton.Local, Skeleton.Parents, Skeleton.Bones, View, Tuning);
        const TArray<FTransform> After = Skeleton.Component();
        double Moved = 0;
        for (int32 Bone = 0; Bone < Before.Num(); ++Bone)
            Moved = FMath::Max(Moved, FVector::Dist(Before[Bone].GetLocation(), After[Bone].GetLocation()));
        TestEqual(TEXT("low pose: nothing moves"), Moved, 0., 1e-9);
        View.LowWeight = .5;
        FTestSkeleton Half = MakeTestSkeleton();
        FoundationBodyPose::Apply(Half.Local, Half.Parents, Half.Bones, View, Tuning);
        TestEqual(TEXT("half low: the hips half way back"),
            Before[1].GetLocation().Y - Half.Component()[1].GetLocation().Y, 5., .5);
        View.LowWeight = 0;
    }
    {
        // Crouched (the eyes' crouch 1) the hips go further back.
        FTestSkeleton Skeleton = MakeTestSkeleton();
        View.PitchDeg = -90;
        View.CrouchAmount = 1;
        const FFoundationFirstPersonResult Result =
            FoundationBodyPose::Apply(Skeleton.Local, Skeleton.Parents, Skeleton.Bones, View, Tuning);
        TestEqual(TEXT("crouched: hips back 15"), Result.PelvisBackCm, 15., 1e-6);
        TestEqual(TEXT("crouched: neck at 22"), Result.NeckBehindEyeCm, 22., .01);
    }
    return true;
}
#endif
