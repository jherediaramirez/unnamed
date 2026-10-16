#include "Misc/AutomationTest.h"
#include "Foundation/FoundationBlockFamilies.h"
#include "Foundation/FoundationBlocks.h"
#include "Foundation/FoundationGrid.h"
#include "Foundation/FoundationProfile.h"
#include "Foundation/FoundationSimulation.h"
#include "Foundation/FoundationVitals.h"

#if WITH_DEV_AUTOMATION_TESTS
namespace
{
    // Survival B4b's climbing course: a stone floor (z 0), a stone wall at x 1 up to z 12, and ladders facing west on
    // it at x 0, z 1-10 (each hangs on the wall's west face). The player starts on the floor in the ladder column.
    struct FClimbCourse
    {
        FFoundationBlockWorld World;
        FFoundationSimulation S;
        const UFoundationProfile& P = *GetDefault<UFoundationProfile>();
        FVector Start{50, 50, 100};

        FClimbCourse()
        {
            World.SetRegistry(FFoundationBlockRegistry::GetDefault());
            World.BoundsMin = FIntVector(-8, -8, 0);
            World.BoundsMax = FIntVector(7, 7, 15);
            for (int32 X = -4; X <= 4; ++X)
                for (int32 Y = -4; Y <= 4; ++Y)
                    World.Set({X, Y, 0}, State(TEXT("stone")));
            for (int32 Z = 1; Z <= 12; ++Z)
                World.Set({1, 0, Z}, State(TEXT("stone")));
            for (int32 Z = 1; Z <= 10; ++Z)
                World.Set({0, 0, Z}, State(TEXT("ladder[facing=west,waterlogged=false]")));
            S.Reset(Start, P);
        }

        FFoundationBlockState State(const TCHAR* Text) const
        {
            FFoundationBlockState Out;
            ensureMsgf(World.GetRegistry().Parse(Text, Out), TEXT("parse %s"), Text);
            return Out;
        }

        // One step, the eligibility as the pawn builds it (bClimb: the switch).
        void Step(const FFoundationIntent& Intent, bool bClimb = true)
        {
            TArray<FBox> Boxes;
            World.GatherCollision(FBox(FVector(-800, -800, -100), FVector(800, 800, 1600)), Boxes);
            FFoundationEligibility Facts;
            Facts.bOnClimbable =
                bClimb && FoundationBlockFamilies::IsClimbable(World, FoundationGrid::CellAt(S.Feet, World.OriginCm));
            S.Step(Intent, 0, P, Boxes, Facts);
        }
    };

    FFoundationIntent MakeIntent(double Forward, double Right = 0, bool bCrouch = false, bool bJump = false)
    {
        FFoundationIntent Intent;
        Intent.Move = {Right, Forward};
        Intent.bCrouch = bCrouch;
        Intent.bJump = bJump;
        return Intent;
    }
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFoundationClimbMovementTest, "Unnamed.Foundation.Climb.Movement",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FFoundationClimbMovementTest::RunTest(const FString&)
{
    // VALUES "Ladder speed" (movement.ladder_clamp): each horizontal axis and the descent clamp to 0.15 blocks a tick,
    // sneaking holds; pushing into the wall sets 0.2 up before gravity and drag (movement.ladder; provisional).
    FClimbCourse C;
    const double Up = (20. - C.P.GravityCmPerTick) * C.P.VerticalDrag;
    // Pushing forward into the ladder climbs.
    for (int32 T = 0; T < 30; ++T)
        C.Step(MakeIntent(1));
    AddInfo(FString::Printf(TEXT("30 ticks pushing: feet %s, Vz %.4f"), *C.S.Feet.ToString(), C.S.VelocityPerTick.Z));
    TestTrue(TEXT("climbing"), C.S.bClimbing);
    TestTrue(TEXT("pushing into the ladder rises (0.2 - gravity) x drag a tick"),
        FMath::IsNearlyEqual(C.S.VelocityPerTick.Z, Up, 1e-9) &&
            FMath::IsNearlyEqual(C.S.LastDisplacement.Z, Up, 1e-9));
    TestTrue(TEXT("no fall distance while climbing"), C.S.FallDistanceCm == 0);
    // Jumping in the column without pushing climbs too.
    for (int32 T = 0; T < 5; ++T)
        C.Step(MakeIntent(0, 0, false, true));
    TestTrue(TEXT("jump holds the climb"), FMath::IsNearlyEqual(C.S.LastDisplacement.Z, Up, 1e-9));
    // Sneaking holds the height (the first steps still carry the climb's speed up, then gravity's pull is held at 0).
    for (int32 T = 0; T < 2; ++T)
        C.Step(MakeIntent(0, 0, true));
    const double Held = C.S.Feet.Z;
    for (int32 T = 0; T < 20; ++T)
        C.Step(MakeIntent(0, 0, true));
    TestTrue(TEXT("sneaking holds the height"), C.S.Feet.Z == Held && C.S.LastDisplacement.Z == 0);
    // Let go: the descent clamps to 0.15 blocks a tick.
    double Fastest = 0;
    for (int32 T = 0; T < 20; ++T)
    {
        C.Step(MakeIntent(0));
        Fastest = FMath::Min(Fastest, C.S.LastDisplacement.Z);
    }
    TestTrue(TEXT("descending at 0.15 blocks a tick"),
        FMath::IsNearlyEqual(C.S.LastDisplacement.Z, -15., 1e-9) && Fastest >= -15. - 1e-9);
    TestTrue(TEXT("descending: no fall distance"), C.S.FallDistanceCm == 0);
    // Arriving fast (0.4 blocks a tick along the wall), sprinting along it: each axis moves 0.15 blocks a tick.
    FClimbCourse Side;
    Side.S.VelocityPerTick = FVector(0, 40, 0);
    FFoundationIntent Run = MakeIntent(1, 1);
    Run.bSprint = true;
    Side.Step(Run);
    AddInfo(FString::Printf(TEXT("climbing step %s"), *Side.S.LastDisplacement.ToString()));
    TestTrue(TEXT("horizontal clamp 0.15 blocks a tick"),
        Side.S.bClimbing && Side.S.LastDisplacement.Y == 15. && FMath::Abs(Side.S.LastDisplacement.X) <= 15.);
    // foundation.Blocks.Climb 0: pushing into the ladder does not rise.
    FClimbCourse Off;
    for (int32 T = 0; T < 30; ++T)
        Off.Step(MakeIntent(1), false);
    TestTrue(TEXT("switch 0: no climbing"), !Off.S.bClimbing && FMath::IsNearlyEqual(Off.S.Feet.Z, 100., 1e-9));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFoundationClimbFallTest, "Unnamed.Foundation.Climb.Fall",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FFoundationClimbFallTest::RunTest(const FString&)
{
    // Both fall accumulators stay at 0 on a ladder: climbing up 9 blocks and sliding back down hurts nothing; dropping
    // off the same height hurts.
    FClimbCourse C;
    FFoundationVitals Vitals;
    Vitals.bNaturalRegeneration = false;
    auto StepBoth = [&](const FFoundationIntent& Intent, bool bClimb)
    {
        C.Step(Intent, bClimb);
        FFoundationVitalsStep Facts;
        Facts.DisplacementCm = C.S.LastDisplacement;
        Facts.bGrounded = C.S.bGrounded;
        Facts.bClimbing = C.S.bClimbing;
        Vitals.Step(Facts);
    };
    for (int32 T = 0; T < 400 && C.S.Feet.Z < 1000; ++T)
        StepBoth(MakeIntent(1), true);
    TestTrue(TEXT("climbed 9 blocks"), C.S.Feet.Z >= 1000);
    // Off the top: step back onto the ladder's last cell and slide down to the floor.
    for (int32 T = 0; T < 200 && !C.S.bGrounded; ++T)
        StepBoth(MakeIntent(0), true);
    AddInfo(FString::Printf(TEXT("slid down to %s, health %.1f"), *C.S.Feet.ToString(), Vitals.Health));
    TestTrue(TEXT("down the ladder: on the floor"), C.S.bGrounded && FMath::IsNearlyEqual(C.S.Feet.Z, 100., 1e-6));
    TestEqual(TEXT("down the ladder: no damage"), Vitals.Health, 20.f);
    TestEqual(TEXT("the vitals' fall stays 0"), Vitals.FallDistanceCm, 0.);
    // The control: the same height with climbing off falls and hurts.
    FClimbCourse Drop;
    Drop.S.Reset({50, 50, 1000}, Drop.P);
    FFoundationVitals Hurt;
    Hurt.bNaturalRegeneration = false;
    for (int32 T = 0; T < 200 && !Drop.S.bGrounded; ++T)
    {
        Drop.Step(MakeIntent(0), false);
        FFoundationVitalsStep Facts;
        Facts.DisplacementCm = Drop.S.LastDisplacement;
        Facts.bGrounded = Drop.S.bGrounded;
        Facts.bClimbing = Drop.S.bClimbing;
        Hurt.Step(Facts);
    }
    TestTrue(TEXT("9 blocks without climbing hurt"), Hurt.Health < 20.f);
    // A step that climbs zeroes the vitals' fall on its own.
    FFoundationVitals Mid;
    FFoundationVitalsStep Falling;
    Falling.DisplacementCm = {0, 0, -300};
    Mid.Step(Falling);
    FFoundationVitalsStep Climbing = Falling;
    Climbing.bClimbing = true;
    Mid.Step(Climbing);
    TestEqual(TEXT("a climbing step zeroes the vitals' fall"), Mid.FallDistanceCm, 0.);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFoundationClimbUnchangedTest, "Unnamed.Foundation.Climb.Unchanged",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FFoundationClimbUnchangedTest::RunTest(const FString&)
{
    // bOnClimbable false never climbs: a mixed course (sprint, jumps, sneaking, a step) run with the default facts and
    // with bOnClimbable written false gives the same trajectory, and nothing climbs. That the step without a climbable
    // is the one before B4b is held by the pinned movement suites (Unnamed.Foundation.Movement, .Vitals.Movement): they
    // fail when the climbing clamp is applied without the gate (B4b's mutation check M8).
    const UFoundationProfile& P = *GetDefault<UFoundationProfile>();
    TArray<FBox> Course = {FBox(FVector(-100000, -100000, -100), FVector(100000, 100000, 0)),
        FBox(FVector(300, -100, 0), FVector(400, 100, 50)), FBox(FVector(900, -500, 0), FVector(1000, 500, 300))};
    FFoundationSimulation A, B;
    A.Reset({}, P);
    B.Reset({}, P);
    FFoundationEligibility Off;
    Off.bOnClimbable = false;
    bool bSame = true, bClimbed = false;
    for (int32 T = 0; T < 400; ++T)
    {
        FFoundationIntent Mixed;
        Mixed.Move = {T % 50 < 10 ? .5 : 0., T % 70 < 60 ? 1. : -1.};
        Mixed.bSprint = T % 90 < 40;
        Mixed.bJump = T % 23 < 3;
        Mixed.bCrouch = T % 110 > 95;
        A.Step(Mixed, T * 1.7, P, Course);
        B.Step(Mixed, T * 1.7, P, Course, Off);
        bSame &= A.Feet == B.Feet && A.VelocityPerTick == B.VelocityPerTick && A.FallDistanceCm == B.FallDistanceCm &&
            A.bGrounded == B.bGrounded;
        bClimbed |= A.bClimbing || B.bClimbing;
    }
    TestTrue(TEXT("same trajectory"), bSame);
    TestFalse(TEXT("nothing climbs"), bClimbed);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFoundationClimbBlocksTest, "Unnamed.Foundation.Climb.Blocks",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FFoundationClimbBlocksTest::RunTest(const FString&)
{
    // Which cells climb: #minecraft:climbable (Block.Climbable), and an open trapdoor over a ladder facing the same way
    // (minecraft.wiki "Trapdoor"; provisional).
    FClimbCourse C;
    FFoundationBlockWorld& World = C.World;
    const FIntVector Cell(-2, -2, 3);
    auto Climbs = [&](const TCHAR* Text)
    {
        World.Set(Cell, C.State(Text));
        return FoundationBlockFamilies::IsClimbable(World, Cell);
    };
    for (const TCHAR* Text : {TEXT("ladder[facing=north,waterlogged=false]"), TEXT("vine[north=true]"),
             TEXT("twisting_vines_plant"), TEXT("weeping_vines[age=0]"), TEXT("cave_vines_plant[berries=false]")})
        TestTrue(FString(Text) + TEXT(" climbs"), Climbs(Text));
    for (const TCHAR* Text : {TEXT("stone"), TEXT("air"), TEXT("oak_fence"), TEXT("glow_lichen[north=true]")})
        TestFalse(FString(Text) + TEXT(" does not climb"), Climbs(Text));
    World.Set(Cell - FIntVector(0, 0, 1), C.State(TEXT("ladder[facing=north,waterlogged=false]")));
    TestTrue(TEXT("an open trapdoor over a same-facing ladder"),
        Climbs(TEXT("oak_trapdoor[facing=north,half=bottom,open=true,powered=false,waterlogged=false]")));
    TestFalse(TEXT("closed"),
        Climbs(TEXT("oak_trapdoor[facing=north,half=bottom,open=false,powered=false,waterlogged=false]")));
    TestFalse(TEXT("facing another way"),
        Climbs(TEXT("oak_trapdoor[facing=south,half=bottom,open=true,powered=false,waterlogged=false]")));
    World.Set(Cell - FIntVector(0, 0, 1), C.State(TEXT("stone")));
    TestFalse(TEXT("an open trapdoor over stone"),
        Climbs(TEXT("oak_trapdoor[facing=north,half=bottom,open=true,powered=false,waterlogged=false]")));
    TestFalse(TEXT("a cell outside the world"), FoundationBlockFamilies::IsClimbable(World, FIntVector(50, 0, 0)));
    return true;
}
#endif
