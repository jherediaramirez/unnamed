#include "Misc/AutomationTest.h"
#include "Foundation/FoundationActions.h"
#include "Foundation/FoundationCoordinates.h"
#include "Foundation/FoundationLab.h"
#include "Foundation/FoundationProfile.h"
#include "Foundation/FoundationSimulation.h"
#include "Engine/Engine.h"
#include "Engine/World.h"

#if WITH_DEV_AUTOMATION_TESTS
namespace
{
    // A throwaway game world for actors that only need to exist (no game instance or player).
    struct FScopedTestWorld
    {
        UWorld* World = UWorld::CreateWorld(EWorldType::Game, false);

        FScopedTestWorld() { GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World); }

        ~FScopedTestWorld()
        {
            GEngine->DestroyWorldContext(World);
            World->DestroyWorld(false);
        }
    };
}

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
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFoundationFlightTest, "Unnamed.Foundation.Flight",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FFoundationFlightTest::RunTest(const FString&)
{
    const auto& P = *GetDefault<UFoundationProfile>();
    const TArray<FBox> Ground = {FBox(FVector(-100000, -100000, -100), FVector(100000, 100000, 0))};
    FFoundationEligibility Creative;
    Creative.bMayFly = true;
    auto Fresh = [&P](FFoundationSimulation& Sim)
    {
        Sim.Reset({}, P);
        Sim.bGrounded = true;
    };
    // One-tick jump taps Gap reference ticks apart; the second stays held when the caller holds jump on.
    auto DoubleTap = [&P, &Ground](FFoundationSimulation& Sim, int32 Gap, const FFoundationEligibility& Allowed)
    {
        FFoundationIntent Tap;
        for (int32 T = 0; T <= Gap; ++T)
        {
            Tap.bJump = T == 0 || T == Gap;
            Sim.Step(Tap, 0, P, Ground, Allowed);
        }
    };
    auto BlocksPerSecond = [&P](double CmPerTick) { return CmPerTick / 100. / P.TickSeconds; };
    const FFoundationIntent Idle;
    FFoundationSimulation S;

    // Toggling: Creative only, and the sprint double tap's window (7 ticks: taps up to 6 ticks apart).
    Fresh(S);
    bool bEverFlew = false;
    for (int32 T = 0; T < 40; ++T)
    {
        FFoundationIntent Tap;
        Tap.bJump = T % 3 == 0;
        S.Step(Tap, 0, P, Ground);
        bEverFlew |= S.bFlying;
    }
    TestFalse(TEXT("without bMayFly taps 3 ticks apart never fly"), bEverFlew);
    for (int32 Gap : {2, 3, 6, 7, 8})
    {
        Fresh(S);
        DoubleTap(S, Gap, Creative);
        TestEqual(FString::Printf(TEXT("taps %d ticks apart toggle flight"), Gap), S.bFlying, Gap <= 6);
    }
    Fresh(S);
    DoubleTap(S, 3, Creative);
    for (int32 T = 0; T < 20; ++T)
        S.Step(Idle, 0, P, Ground, Creative);
    TestTrue(TEXT("a double tap during a jump hovers, no gravity"),
        S.bFlying && S.LastDisplacement.Z == 0 && S.Feet.Z > 100);
    AddInfo(FString::Printf(TEXT("hover height after taps 3 ticks apart=%.4f blocks"), S.Feet.Z / 100.));
    DoubleTap(S, 3, Creative);
    TestFalse(TEXT("another double tap ends flight"), S.bFlying);
    for (int32 T = 0; T < 60 && !S.bGrounded; ++T)
        S.Step(Idle, 0, P, Ground, Creative);
    TestTrue(TEXT("gravity resumes and lands"), S.bGrounded && FMath::Abs(S.Feet.Z) < .001);
    Fresh(S);
    DoubleTap(S, 3, Creative);
    S.Step(Idle, 0, P, Ground);
    TestTrue(TEXT("losing bMayFly mid-air ends flight"), !S.bFlying && S.Feet.Z > 100);
    Fresh(S);
    DoubleTap(S, 3, Creative);
    FFoundationEligibility Immobile = Creative;
    Immobile.bMobile = false;
    S.Step(Idle, 0, P, Ground, Immobile);
    TestFalse(TEXT("losing mobility ends flight"), S.bFlying);

    // Speeds: wiki 10.92 flying, 21.6 sprint-flying (our drag model: 10.8889 and 21.7778).
    FFoundationIntent Forward;
    Forward.Move = {0, 1};
    for (int32 FPS : {30, 60, 120})
    {
        Fresh(S);
        DoubleTap(S, 3, Creative);
        const int64 StepsBefore = S.Steps;
        for (int32 F = 0; F < FPS * 10; ++F)
            S.Advance(1. / FPS, Forward, 0, P, Ground, Creative);
        TestEqual(FString::Printf(TEXT("%d FPS tick count"), FPS), S.Steps - StepsBefore, int64(200));
        TestTrue(FString::Printf(TEXT("%d FPS fly speed within 1%%"), FPS),
            FMath::IsNearlyEqual(BlocksPerSecond(S.LastDisplacement.Size2D()), 10.8888889, .108889));
        TestTrue(TEXT("level flight stays flying"), S.bFlying && S.LastDisplacement.Z == 0);
        TestTrue(TEXT("flying FOV 1.1"), FMath::IsNearlyEqual(S.FovMultiplier, 1.1, 1e-4));
    }
    const double DriftStart = S.Feet.X;
    for (int32 T = 0; T < 200 && (T == 0 || S.LastDisplacement.Size2D() > 0); ++T)
        S.Step(Idle, 0, P, Ground, Creative);
    const double DriftBlocks = (S.Feet.X - DriftStart) / 100.;
    TestTrue(TEXT("release drifts a few blocks"), DriftBlocks > 3 && S.LastDisplacement.Size2D() == 0);
    AddInfo(FString::Printf(TEXT("fly release drift=%.4f blocks"), DriftBlocks));
    FFoundationIntent SprintForward = Forward;
    SprintForward.bSprint = true;
    for (bool bFood : {true, false})
    {
        FFoundationEligibility Allowed = Creative;
        Allowed.bEnoughFood = bFood;
        Fresh(S);
        DoubleTap(S, 3, Allowed);
        for (int32 T = 0; T < 200; ++T)
            S.Step(SprintForward, 0, P, Ground, Allowed);
        TestTrue(FString::Printf(TEXT("sprint-fly speed within 1%% (food %d)"), bFood),
            S.bSprinting && FMath::IsNearlyEqual(BlocksPerSecond(S.LastDisplacement.Size2D()), 21.7777778, .217778));
        TestTrue(TEXT("sprint-flying FOV 1.265"), FMath::IsNearlyEqual(S.FovMultiplier, 1.265, 1e-4));
    }

    // Rise and sink: wiki 7.49 blocks/s (ours 7.50); sneak sinks without crouching; both held hover.
    Fresh(S);
    DoubleTap(S, 3, Creative);
    FFoundationIntent Rise;
    Rise.bJump = true;
    for (int32 T = 0; T < 100; ++T)
        S.Step(Rise, 0, P, Ground, Creative);
    TestTrue(
        TEXT("climb 7.50 blocks/s within 1%"), FMath::IsNearlyEqual(BlocksPerSecond(S.LastDisplacement.Z), 7.5, .075));
    FFoundationIntent Sink;
    Sink.bCrouch = true;
    for (int32 T = 0; T < 40; ++T)
        S.Step(Sink, 0, P, Ground, Creative);
    TestTrue(
        TEXT("sink 7.50 blocks/s within 1%"), FMath::IsNearlyEqual(BlocksPerSecond(-S.LastDisplacement.Z), 7.5, .075));
    TestTrue(TEXT("sinking keeps the standing pose"),
        S.bFlying && !S.bCrouched && S.HeightCm == 180. && FMath::Abs(S.EyeCm - 162) < .1);
    FFoundationIntent Both = Rise;
    Both.bCrouch = true;
    for (int32 T = 0; T < 30; ++T)
        S.Step(Both, 0, P, Ground, Creative);
    const double HeldZ = S.Feet.Z;
    for (int32 T = 0; T < 20; ++T)
        S.Step(Both, 0, P, Ground, Creative);
    TestTrue(TEXT("jump and sneak together hold height"), S.bFlying && !S.bCrouched && S.Feet.Z == HeldZ);

    // Landing: only moving down onto something ends flight; no fall distance builds while flying.
    Fresh(S);
    DoubleTap(S, 3, Creative);
    double MaxFallCm = 0;
    for (int32 T = 0; T < 100 && S.bFlying; ++T)
    {
        S.Step(Sink, 0, P, Ground, Creative);
        MaxFallCm = FMath::Max(MaxFallCm, S.FallDistanceCm);
    }
    TestTrue(TEXT("sinking onto the floor lands"), !S.bFlying && S.bGrounded && FMath::Abs(S.Feet.Z) < .001);
    TestEqual(TEXT("no fall distance while flying"), MaxFallCm, 0.);
    S.Reset({0, 0, .0005}, P);
    S.bFlying = true;
    for (int32 T = 0; T < 40; ++T)
        S.Step(Idle, 0, P, Ground, Creative);
    TestTrue(TEXT("hovering just above the floor stays flying"), S.bFlying && !S.bGrounded && S.Feet.Z == .0005);

    // Collision stays on: a wall stops flight and sprint; a 1.5-block ceiling forces the crouch pose.
    TArray<FBox> Walled = Ground;
    Walled.Add(FBox(FVector(500, -1000, -100), FVector(600, 1000, 1000)));
    S.Reset({0, 0, 200}, P);
    S.bFlying = true;
    for (int32 T = 0; T < 40; ++T)
        S.Step(SprintForward, 0, P, Walled, Creative);
    TestTrue(TEXT("a wall stops flight"), S.bFlying && FMath::IsNearlyEqual(S.Feet.X, 470., .001));
    TestFalse(TEXT("a head-on wall ends sprint-flying"), S.bSprinting);
    TArray<FBox> LowCeiling = Ground;
    LowCeiling.Add(FBox(FVector(-1000, -1000, 150), FVector(1000, 1000, 250)));
    S.Reset({}, P);
    S.bFlying = true;
    for (int32 T = 0; T < 10; ++T)
        S.Step(Idle, 0, P, LowCeiling, Creative);
    TestTrue(TEXT("a 1.5-block ceiling forces the crouch pose"), S.bFlying && S.bCrouched && S.HeightCm == 150.);
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
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFoundationActionsTest, "Unnamed.Foundation.ActionContracts",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FFoundationActionsTest::RunTest(const FString&)
{
    FScopedTestWorld TestWorld;
    auto* Lab = TestWorld.World->SpawnActor<AFoundationLab>();
    const auto& P = *GetDefault<UFoundationProfile>();
    Lab->Primitives = {
        {FBox(FVector(449, -50, 0), FVector(549, 50, 200)), TEXT("block"), EFoundationTargetKind::Block, true}};
    auto Target = Lab->TraceTarget({0, 0, 162}, {1, 0, 0}, P);
    TestTrue(TEXT("block 4.49m reachable"), Target.bInRange);
    Target = Lab->TraceTarget({-1, 0, 162}, {1, 0, 0}, P);
    TestFalse(TEXT("block exact4.5m rejected"), Target.bInRange);
    Lab->Primitives[0].Kind = EFoundationTargetKind::Entity;
    TestFalse(TEXT("entity same4.49m rejected"), Lab->TraceTarget({0, 0, 162}, {1, 0, 0}, P).bInRange);
    Target = Lab->TraceTarget({150, 0, 162}, {1, 0, 0}, P);
    TestTrue(TEXT("entity2.99m reachable"), Target.bInRange);
    Lab->Primitives.Add(
        {FBox(FVector(300, -50, 0), FVector(350, 50, 200)), TEXT("occluder"), EFoundationTargetKind::Block, false});
    TestEqual(TEXT("solid block occludes entity"), Lab->TraceTarget({150, 0, 162}, {1, 0, 0}, P).Name,
        FString(TEXT("occluder")));
    FFoundationActions A;
    FFoundationActionRequest R;
    R.Target = Target;
    TestTrue(TEXT("entity attack press accepted"), A.Dispatch(R).bAccepted);
    TestFalse(TEXT("entity attack does not mine"), A.bBreaking);
    R.Phase = EFoundationPhase::Hold;
    TestFalse(TEXT("held entity attack is not extra clicks"), A.Dispatch(R).bAccepted);
    R.Phase = EFoundationPhase::Press;
    R.Target.Kind = EFoundationTargetKind::Block;
    A.Dispatch(R);
    TestTrue(TEXT("block break fixture active"), A.bBreaking);
    R.Action = EFoundationAction::Use;
    TestFalse(TEXT("active break prevents use"), A.Dispatch(R).bAccepted);
    R.Action = EFoundationAction::Attack;
    R.Phase = EFoundationPhase::Release;
    A.Dispatch(R);
    TestFalse(TEXT("release cancels break"), A.bBreaking);
    R.Action = EFoundationAction::Use;
    R.Phase = EFoundationPhase::Press;
    TestTrue(TEXT("interactive target accepted before item"), A.Dispatch(R).bAccepted);
    TestFalse(TEXT("interaction is not ongoing item use"), A.bUsing);
    R.Target.bRejectInteraction = true;
    TestFalse(TEXT("block failure prevents item/offhand fallback"), A.Dispatch(R).bAccepted);
    TestFalse(TEXT("failed target did not begin item use"), A.bUsing);
    R.Target.bRejectInteraction = false;
    R.Target.Kind = EFoundationTargetKind::Entity;
    R.bCrouch = true;
    A.Dispatch(R);
    TestFalse(TEXT("crouching does not generically bypass entity interaction"), A.bUsing);
    R.Target.Kind = EFoundationTargetKind::Block;
    R.bCrouch = true;
    A.Dispatch(R);
    TestTrue(TEXT("crouch with a held item bypasses the target to the item"), A.bUsing);
    R.Action = EFoundationAction::Attack;
    TestFalse(TEXT("using item consumes attack"), A.Dispatch(R).bAccepted);
    R.Action = EFoundationAction::Pick;
    TestFalse(TEXT("using item consumes pick"), A.Dispatch(R).bAccepted);
    A.Cancel();
    A.Select(8);
    R.Action = EFoundationAction::Use;
    auto Used = A.Dispatch(R);
    TestEqual(TEXT("main empty tries offhand"), Used.Hand, EFoundationHand::Off);
    A.Cancel();
    A.Offhand = 0;
    R.Target.bInteractive = false;
    TestFalse(TEXT("empty hands pass"), A.Dispatch(R).bAccepted);
    A.Scroll(-1);
    TestEqual(TEXT("wheel wraps slot9 to1"), A.Slot, 0);
    R.Action = EFoundationAction::Drop;
    R.bControl = false;
    A.Dispatch(R);
    TestEqual(TEXT("drop one"), A.Slots[0], 63);
    R.bControl = true;
    A.Dispatch(R);
    TestEqual(TEXT("fixed control drops stack"), A.Slots[0], 0);
    A.Offhand = 7;
    R.Action = EFoundationAction::Swap;
    A.Dispatch(R);
    TestEqual(TEXT("offhand swaps tokens"), A.Slots[0], 7);
    R.Action = EFoundationAction::Inventory;
    A.Dispatch(R);
    TestFalse(TEXT("inventory cancels actions"), A.bUsing || A.bBreaking);
    return true;
}
#endif
