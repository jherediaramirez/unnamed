#include "Misc/AutomationTest.h"
#include "Foundation/FoundationCoordinates.h"
#include "Foundation/FoundationProfile.h"
#include "Foundation/FoundationSimulation.h"

#if WITH_DEV_AUTOMATION_TESTS
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFoundationCoordinatesTest, "Unnamed.Foundation.Coordinates",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FFoundationCoordinatesTest::RunTest(const FString&)
{
    for (FVector V : {FVector(-10, -110, -210), FVector(12345, -98765, 4321), FVector::ZeroVector})
        TestTrue(TEXT("round trip with origin rebase"),
            FoundationCoordinates::ToUnreal(FoundationCoordinates::ToMinecraft(V, {100, 200, 300}, {3000, -8000, 1000}),
                {100, 200, 300}, {3000, -8000, 1000})
                .Equals(V, 1e-7));
    TestEqual(TEXT("negative floor"), FoundationCoordinates::Blocks({-.1, -1.1, -2.01}), FInt64Vector3(-1, -2, -3));
    TestEqual(TEXT("east yaw"), FoundationCoordinates::Yaw(0), -90.);
    TestEqual(TEXT("south yaw"), FoundationCoordinates::Yaw(90), 0.);
    TestEqual(TEXT("west yaw"), FoundationCoordinates::Yaw(180), 90.);
    TestEqual(TEXT("north facing"), FoundationCoordinates::Facing(270), FString(TEXT("north (-Z)")));
    TestEqual(TEXT("angle reverse"), FoundationCoordinates::ToUnrealYaw(FoundationCoordinates::Yaw(47)), 47.);
    TestEqual(TEXT("up negative pitch"), FoundationCoordinates::Pitch(30), -30.);
    TestTrue(TEXT("vertical70 at16:9"),
        FMath::IsNearlyEqual(FoundationCoordinates::HorizontalFov(70, 16. / 9.), 102.4478578, .0001));
    return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFoundationMovementTest, "Unnamed.Foundation.Movement",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FFoundationMovementTest::RunTest(const FString&)
{
    const auto& P = *GetDefault<UFoundationProfile>();
    const TArray<FBox> Ground = {FBox(FVector(-100000, -100000, -100), FVector(100000, 100000, 0))};
    for (int32 FPS : {30, 60, 120})
    {
        FFoundationSimulation S;
        S.Reset({0, 0, 0}, P);
        S.bGrounded = true;
        FFoundationIntent I;
        I.Move = {0, 1};
        for (int32 F = 0; F < FPS * 10; ++F)
            S.Advance(1. / FPS, I, 0, P, Ground);
        TestEqual(FString::Printf(TEXT("%d FPS tick count"), FPS), S.Steps, int64(200));
        TestTrue(TEXT("walk speed within1%"),
            FMath::IsNearlyEqual(S.LastDisplacement.Size2D() / 100. / P.TickSeconds, 4.3171806, .04317));
        I.bSprint = true;
        for (int32 F = 0; F < FPS * 3; ++F)
            S.Advance(1. / FPS, I, 0, P, Ground);
        TestTrue(TEXT("sprint speed"), FMath::IsNearlyEqual(S.LastDisplacement.Size2D() / 5., 5.6123348, .05612));
        I.bSprint = false;
        S.Step(I, 0, P, Ground);
        TestTrue(TEXT("sprint release latches"), S.bSprinting);
        I.Move = {};
        for (int32 T = 0; T < 20; ++T)
            S.Step(I, 0, P, Ground);
        TestTrue(TEXT("stopped"), S.VelocityPerTick.SizeSquared2D() == 0);
        I.bCrouch = true;
        I.Move = {0, 1};
        for (int32 T = 0; T < 60; ++T)
            S.Step(I, 0, P, Ground);
        TestTrue(TEXT("crouch speed"), FMath::IsNearlyEqual(S.LastDisplacement.Size2D() / 5., 1.2951542, .01295));
        TestEqual(TEXT("crouch height"), S.HeightCm, 150.);
        TestTrue(TEXT("crouch eye"), FMath::Abs(S.EyeCm - 127) < .1);
        S.Reset({}, P);
        S.bGrounded = true;
        I = {};
        I.bJump = true;
        double Apex = 0;
        int32 AirTicks = 0;
        do
        {
            S.Step(I, 0, P, Ground);
            Apex = FMath::Max(Apex, S.Feet.Z);
            ++AirTicks;
        } while (!S.bGrounded && AirTicks < 50);
        TestTrue(TEXT("jump apex .02 block"), FMath::Abs(Apex / 100. - 1.2522) < .02);
        TestEqual(TEXT("airtime12 ticks"), AirTicks, 12);
        AddInfo(
            FString::Printf(TEXT("fps=%d apex=%.8f blocks airtime=%d reference ticks"), FPS, Apex / 100., AirTicks));
        S.Step(I, 0, P, Ground);
        TestTrue(TEXT("held jump repeats"), S.Feet.Z > 0);
        S.Reset({}, P);
        S.bGrounded = true;
        I = {};
        I.Move = {1, 1};
        for (int32 T = 0; T < 60; ++T)
            S.Step(I, 0, P, Ground);
        TestTrue(
            TEXT("26.3 diagonal speed"), FMath::IsNearlyEqual(S.LastDisplacement.Size2D() / 5., 4.40528634, .04405));
    }
    FFoundationSimulation S;
    S.Reset({}, P);
    S.bGrounded = true;
    FFoundationIntent I;
    I.Move = {0, 1};
    for (int32 T = 0; T < 200; ++T)
        S.Step(I, 0, P, Ground);
    const double StopStart = S.Feet.X;
    int32 FirstStoppedTick = 0;
    I = {};
    for (int32 T = 1; T <= 20; ++T)
    {
        S.Step(I, 0, P, Ground);
        if (!FirstStoppedTick && S.LastDisplacement.Size2D() == 0) FirstStoppedTick = T;
    }
    const double StopBlocks = (S.Feet.X - StopStart) / 100.;
    TestTrue(TEXT("walk release stopping distance within .001 block"),
        FMath::IsNearlyEqual(StopBlocks, .255845995478, .001));
    TestEqual(TEXT("walk release first zero displacement tick"), FirstStoppedTick, 8);
    AddInfo(FString::Printf(TEXT("walk release distance=%.10f blocks firstZeroTick=%d"), StopBlocks, FirstStoppedTick));
    S.Reset({}, P);
    S.bGrounded = true;
    I.Move = {1, 1};
    I.bCrouch = true;
    for (int32 T = 0; T < 60; ++T)
        S.Step(I, 0, P, Ground);
    TestTrue(TEXT("26.3 crouch diagonal speed"),
        FMath::IsNearlyEqual(S.LastDisplacement.Size2D() / 5., 1.8316246138, .0183163));
    AddInfo(FString::Printf(TEXT("crouch diagonal=%.10f blocks/s"), S.LastDisplacement.Size2D() / 5.));
    S.Reset({}, P);
    S.bGrounded = true;
    I = {};
    I.Move = {0, 1};
    I.bSprint = true;
    for (int32 T = 0; T < 60; ++T)
        S.Step(I, 90, P, Ground);
    const double BeforeJumpY = S.VelocityPerTick.Y;
    I.bJump = true;
    S.Step(I, 90, P, Ground);
    const double BoostCm = S.LastDisplacement.Y - BeforeJumpY - 12.74;
    TestTrue(TEXT("sprint jump adds20cm/tick along facing"), FMath::IsNearlyEqual(BoostCm, 20., .0001));
    TestTrue(TEXT("jump first displacement42cm"), FMath::IsNearlyEqual(S.LastDisplacement.Z, 42., .0001));
    const double BeforeAirY = S.VelocityPerTick.Y;
    S.Step(I, 90, P, Ground);
    const double AirAccelCm = S.LastDisplacement.Y - BeforeAirY;
    TestTrue(TEXT("sprint air intent acceleration2.548cm/tick"), FMath::IsNearlyEqual(AirAccelCm, 2.548, .0001));
    AddInfo(FString::Printf(
        TEXT("sprint jump boost=%.6f cm/ref.tick air acceleration=%.6f cm/ref.tick^2"), BoostCm, AirAccelCm));
    FFoundationEligibility Denied;
    Denied.bEnoughFood = false;
    S.Step(I, 90, P, Ground, Denied);
    TestFalse(TEXT("eligibility loss cancels resolved sprint"), S.bSprinting);
    S.Reset({}, P);
    S.Advance(2., {}, 0, P, Ground);
    TestEqual(TEXT("hitch bounded"), S.Steps, int64(5));
    TestTrue(TEXT("hitch counted"), S.DroppedSeconds > 1.7);
    return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFoundationCollisionTest, "Unnamed.Foundation.Collision",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FFoundationCollisionTest::RunTest(const FString&)
{
    const auto& P = *GetDefault<UFoundationProfile>();
    const FBox Cube(FVector(0, 0, 0), FVector(100, 100, 100));
    TestFalse(TEXT("box corner blocks where capsule circle clears"),
        FFoundationSimulation::Fits(FFoundationSimulation::Bounds({-22, -22, 0}, 180, 60), {Cube}));
    TestTrue(TEXT("capsule analytic corner clearance"), FMath::Sqrt(22. * 22. * 2.) > 30.);
    for (double Height : {50., 60., 61., 100.})
    {
        TArray<FBox> Boxes = {FBox(FVector(-1000, -1000, -100), FVector(1000, 1000, 0)),
            FBox(FVector(100, -100, 0), FVector(200, 100, Height))};
        FFoundationSimulation S;
        S.Reset({0, 0, 0}, P);
        S.bGrounded = true;
        FFoundationIntent I;
        I.Move = {0, 1};
        for (int32 T = 0; T < 18; ++T)
            S.Step(I, 0, P, Boxes);
        TestEqual(FString::Printf(TEXT("step%.0fcm"), Height), S.Feet.X > 100, Height <= 60);
    }
    TArray<FBox> Low = {FBox(FVector(-1000, -1000, -100), FVector(1000, 1000, 0)),
        FBox(FVector(-100, -100, 155), FVector(100, 100, 200))};
    FFoundationSimulation S;
    S.Reset({}, P);
    S.bGrounded = true;
    S.Step({}, 0, P, Low);
    TestTrue(TEXT("blocked standing retains crouch"), S.bCrouched);
    TestFalse(TEXT("headroom false"), S.bHeadroom);
    const TArray<FBox> Ledge = {FBox(FVector(-100, -100, -100), FVector(100, 100, 0))};
    S.Reset({}, P);
    S.bGrounded = true;
    FFoundationIntent I;
    I.Move = {0, 1};
    I.bCrouch = true;
    for (int32 T = 0; T < 300; ++T)
        S.Step(I, 0, P, Ledge);
    TestTrue(TEXT("ledge retains support"), S.Feet.X < 130 && S.Feet.Z >= 0);
    for (double Gap : {59., 61.})
    {
        TArray<FBox> Walls = {FBox(FVector(-100, -100, 0), FVector(-Gap / 2, 100, 200)),
            FBox(FVector(Gap / 2, -100, 0), FVector(100, 100, 200))};
        TestEqual(TEXT("gap clearance"), FFoundationSimulation::Fits(FFoundationSimulation::Bounds({}, 180, 60), Walls),
            Gap > 60);
    }
    TArray<FBox> JumpStep = {
        FBox(FVector(-1000, -1000, -100), FVector(1000, 1000, 0)), FBox(FVector(100, -100, 0), FVector(250, 100, 100))};
    for (bool Enabled : {false, true})
    {
        S.Reset({}, P);
        S.bGrounded = true;
        I = {};
        I.Move = {0, 1};
        I.bAutoJump = Enabled;
        double Peak = 0;
        for (int32 T = 0; T < 25; ++T)
        {
            S.Step(I, 0, P, JumpStep);
            Peak = FMath::Max(Peak, S.Feet.Z);
        }
        TestEqual(TEXT("auto jump 1m obstacle fixture"), Peak > 100, Enabled);
    }
    return true;
}
#endif
