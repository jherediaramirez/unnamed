#include "Misc/AutomationTest.h"
#include "Foundation/FoundationAuthority.h"
#include "Foundation/FoundationBlockRegistry.h"
#include "Foundation/FoundationChat.h"
#include "Foundation/FoundationDeath.h"
#include "Foundation/FoundationPawn.h"
#include "Foundation/FoundationVitals.h"

#if WITH_DEV_AUTOMATION_TESTS
// Survival step 11b: the hurt tilt, the death roll and the chat fade. Expected numbers are literals worked from the
// survival findings' formulas (evidence ids in the comments), never the functions under test.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFoundationHurtTiltTest, "Unnamed.Foundation.Hurt.Tilt",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FFoundationHurtTiltTest::RunTest(const FString& Parameters)
{
    // [hurt.camera_tilt] -14 x sin((hurtTime / 10)^4 x pi) x strength.
    TestTrue(
        TEXT("hurtTime 10: sin(pi) is 0"), FMath::IsNearlyZero(FoundationHurt::GetHurtTiltDegrees(10, 10, 1), 1e-9));
    TestTrue(TEXT("hurtTime 9: -14 x sin(0.6561 pi)"),
        FMath::IsNearlyEqual(FoundationHurt::GetHurtTiltDegrees(9, 10, 1), -12.3504, .01));
    TestTrue(TEXT("hurtTime 5: -14 x sin(0.0625 pi)"),
        FMath::IsNearlyEqual(FoundationHurt::GetHurtTiltDegrees(5, 10, 1), -2.7313, .01));
    TestEqual(TEXT("hurtTime 0"), FoundationHurt::GetHurtTiltDegrees(0, 10, 1), 0.);
    TestEqual(TEXT("strength 0 gives 0"), FoundationHurt::GetHurtTiltDegrees(9, 10, 0), 0.);
    TestTrue(TEXT("half strength halves it"),
        FMath::IsNearlyEqual(FoundationHurt::GetHurtTiltDegrees(9, 10, .5), -6.1752, .01));
    for (int32 HurtTime = 1; HurtTime < 10; ++HurtTime)
        TestTrue(*FString::Printf(TEXT("negative at %d"), HurtTime),
            FoundationHurt::GetHurtTiltDegrees(HurtTime, 10, 1) < 0);

    // [hurt.cooldown] cooldown 20 at the hit: hurt time 10, then 0 from cooldown 10 down.
    FFoundationVitals Vitals;
    Vitals.DamageCooldown = 20;
    TestEqual(TEXT("fresh hit: 10"), FoundationHurt::GetHurtTime(Vitals), 10);
    Vitals.DamageCooldown = 15;
    TestEqual(TEXT("cooldown 15: 5"), FoundationHurt::GetHurtTime(Vitals), 5);
    Vitals.DamageCooldown = 10;
    TestEqual(TEXT("cooldown 10: 0"), FoundationHurt::GetHurtTime(Vitals), 0);
    Vitals.DamageCooldown = 0;
    TestEqual(TEXT("no cooldown: 0"), FoundationHurt::GetHurtTime(Vitals), 0);
    return true;
}

// Codex audit RA3-5: the tilt leans toward the entity that dealt the damage (minecraft.wiki "Damage"; hurt.camera_tilt:
// oriented by hurtDir), the native sweep's roll for a source on the left; damage without a source keeps the last
// side. The hits go through the pawn's hit step (AFoundationPawn::TakeHits). Mutations: the side never turned (a
// zombie on the right still rolls left); the roll's sign flipped.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFoundationHurtTiltSideTest, "Unnamed.Foundation.Hurt.TiltSide",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FFoundationHurtTiltSideTest::RunTest(const FString& Parameters)
{
    // The player at the origin looking along +X (its left is -Y); a zombie's 3 from 2 blocks away at From.
    FFoundationAuthority A;
    A.World.SetRegistry(FFoundationBlockRegistry::GetDefault());
    const int32 Player = A.AddPlayer();
    A.Reset();
    FFoundationPlayerFacts Facts;
    Facts.EyeCm = FVector(0, 0, 162);
    Facts.Aim = FVector::ForwardVector;
    Facts.Step = 1;
    A.SetFacts(Player, Facts);
    FFoundationVitals Vitals;
    FFoundationSimulation Simulation;
    FVector2D Side(1, 0);
    const auto ZombieHit = [&](const FVector& From)
    {
        Vitals.Health = 20;
        Vitals.DamageCooldown = 0;
        Vitals.LastHurt = 0;
        A.FindPlayer(Player)->MobHits.Add({1, 3.f, TEXT("Zombie"), From});
        AFoundationPawn::TakeHits(A, Player, Vitals, Simulation, Side);
    };
    // hurtTime 9's -12.3504 degrees on a level view.
    const auto Tilted = [&]()
    {
        FRotator View = FRotator::ZeroRotator;
        FoundationHurt::ApplyHurtTilt(View, FoundationHurt::GetHurtTiltDegrees(9, 10, 1), Side);
        return View;
    };
    ZombieHit(FVector(0, -200, 0));
    TestTrue(TEXT("on the left: the native roll, -12.35"),
        FMath::IsNearlyEqual(Tilted().Roll, -12.3504, .01) && FMath::IsNearlyZero(Tilted().Pitch, 1e-6));
    ZombieHit(FVector(0, 200, 0));
    TestTrue(TEXT("on the right: the other way, +12.35"),
        FMath::IsNearlyEqual(Tilted().Roll, 12.3504, .01) && FMath::IsNearlyZero(Tilted().Pitch, 1e-6));
    ZombieHit(FVector(200, 0, 0));
    TestTrue(TEXT("in front: a pitch down toward it, no roll"),
        FMath::IsNearlyEqual(Tilted().Pitch, -12.3504, .01) && FMath::IsNearlyZero(Tilted().Roll, 1e-6));
    ZombieHit(FVector(0, 200, 0));
    // A fall hurts without a source: the right side stays.
    Vitals.DamageCooldown = 0;
    Vitals.Hurt(EFoundationDamage::Fall, 2);
    TestTrue(TEXT("a fall keeps the last side"), FMath::IsNearlyEqual(Tilted().Roll, 12.3504, .01));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFoundationHurtDeathRollTest, "Unnamed.Foundation.Hurt.DeathRoll",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FFoundationHurtDeathRollTest::RunTest(const FString& Parameters)
{
    // [hurt.camera] 40 - 8000 / (min(deathTime, 20) + 200).
    TestTrue(TEXT("0 at death time 0"), FMath::IsNearlyZero(FoundationHurt::GetDeathRollDegrees(0), 1e-9));
    TestTrue(TEXT("40 - 8000 / 220 at 20"),
        FMath::IsNearlyEqual(FoundationHurt::GetDeathRollDegrees(20), 40. - 8000. / 220., 1e-9));
    TestTrue(TEXT("3.6364 at 20"), FMath::IsNearlyEqual(FoundationHurt::GetDeathRollDegrees(20), 3.636364, 1e-5));
    TestEqual(
        TEXT("past 20 clamps"), FoundationHurt::GetDeathRollDegrees(500), FoundationHurt::GetDeathRollDegrees(20));
    TestTrue(TEXT("rises with time"),
        FoundationHurt::GetDeathRollDegrees(10) > 0 &&
            FoundationHurt::GetDeathRollDegrees(10) < FoundationHurt::GetDeathRollDegrees(20));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFoundationChatFadeTest, "Unnamed.Foundation.Chat.Fade",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FFoundationChatFadeTest::RunTest(const FString& Parameters)
{
    // [survival.r4.fade] 1 until 180 ticks (9 s), then ((200 - t) / 20)^2, 0 at 200 ticks.
    TestEqual(TEXT("0 s"), FFoundationChat::GetAlpha(0), 1.f);
    TestEqual(TEXT("9 s"), FFoundationChat::GetAlpha(9), 1.f);
    TestEqual(TEXT("9.5 s"), FFoundationChat::GetAlpha(9.5), .25f);
    TestEqual(TEXT("9.75 s"), FFoundationChat::GetAlpha(9.75), .0625f);
    TestTrue(TEXT("9.95 s"), FMath::IsNearlyEqual(FFoundationChat::GetAlpha(9.95), .0025f, .0005f));
    TestEqual(TEXT("10 s"), FFoundationChat::GetAlpha(10), 0.f);
    TestEqual(TEXT("past 10 s"), FFoundationChat::GetAlpha(30), 0.f);
    return true;
}
#endif
