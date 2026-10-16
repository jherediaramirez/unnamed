#include "Misc/AutomationTest.h"
#include "Foundation/FoundationProfile.h"
#include "Foundation/FoundationSimulation.h"
#include "Foundation/FoundationVitals.h"

#if WITH_DEV_AUTOMATION_TESTS
namespace
{
    // Vitals with natural regeneration off, so a rule under test is not mixed with healing.
    FFoundationVitals Quiet()
    {
        FFoundationVitals Vitals;
        Vitals.bNaturalRegeneration = false;
        return Vitals;
    }

    FFoundationVitalsStep Standing()
    {
        FFoundationVitalsStep Facts;
        Facts.bGrounded = true;
        return Facts;
    }

    void StepTimes(FFoundationVitals& Vitals, const FFoundationVitalsStep& Facts, int32 Count)
    {
        for (int32 Index = 0; Index < Count; ++Index)
            Vitals.Step(Facts);
    }
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFoundationVitalsDamageTest, "Unnamed.Foundation.Vitals.Damage",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FFoundationVitalsDamageTest::RunTest(const FString&)
{
    // 26.3 CombatRules: damage * (1 - clamp(armor - damage / (2 + toughness / 4), armor / 5, 20) / 25).
    TestEqual(TEXT("no armor"), FFoundationVitals::GetDamageAfterArmor(10, 0, 0), 10.f);
    TestTrue(TEXT("20 armor stops 60% of a 10 hit"),
        FMath::IsNearlyEqual(FFoundationVitals::GetDamageAfterArmor(10, 20, 0), 4.f, 1e-5f));
    TestTrue(TEXT("a large hit keeps at least armor/5"),
        FMath::IsNearlyEqual(FFoundationVitals::GetDamageAfterArmor(40, 20, 0), 33.6f, 1e-4f));
    TestTrue(TEXT("toughness resists large hits"),
        FMath::IsNearlyEqual(FFoundationVitals::GetDamageAfterArmor(40, 20, 8), 24.f, 1e-4f));
    TestTrue(TEXT("protection caps at 80%"),
        FMath::IsNearlyEqual(FFoundationVitals::GetDamageAfterArmor(1, 30, 0), .2f, 1e-6f));

    FFoundationVitals V = Quiet();
    V.Armor = 20;
    TestTrue(TEXT("mob hit lands"), V.Hurt(EFoundationDamage::MobAttack, 10));
    TestTrue(TEXT("armor reduces mob hits"), FMath::IsNearlyEqual(V.Health, 16.f, 1e-5f));
    TestTrue(TEXT("damage reaching health costs 0.1 exhaustion"), FMath::IsNearlyEqual(V.Exhaustion, .1f, 1e-6f));
    TestEqual(TEXT("20-tick damage cooldown"), V.DamageCooldown, 20);
    TestFalse(TEXT("a weaker hit during the cooldown is ignored"), V.Hurt(EFoundationDamage::Generic, 5));
    TestTrue(TEXT("a stronger hit deals only its excess"), V.Hurt(EFoundationDamage::Generic, 13));
    TestTrue(TEXT("generic damage bypasses armor"), FMath::IsNearlyEqual(V.Health, 13.f, 1e-5f));
    StepTimes(V, Standing(), 10);
    TestEqual(TEXT("cooldown falls once per tick"), V.DamageCooldown, 10);
    TestTrue(TEXT("full damage again after ten ticks"), V.Hurt(EFoundationDamage::Generic, 2));
    TestTrue(TEXT("full second hit"), FMath::IsNearlyEqual(V.Health, 11.f, 1e-5f));

    FFoundationVitals Buffer = Quiet();
    Buffer.GrantAbsorption(4);
    Buffer.GrantAbsorption(2);
    TestEqual(TEXT("absorption grant keeps the larger buffer"), Buffer.Absorption, 4.f);
    Buffer.Hurt(EFoundationDamage::Generic, 3);
    TestEqual(TEXT("absorption takes damage first"), Buffer.Absorption, 1.f);
    TestEqual(TEXT("absorbed damage leaves health"), Buffer.Health, 20.f);
    StepTimes(Buffer, Standing(), 20);
    Buffer.Hurt(EFoundationDamage::MobAttack, 3);
    TestEqual(TEXT("absorption empties, remainder reaches health"), Buffer.Absorption, 0.f);
    TestEqual(TEXT("remainder"), Buffer.Health, 18.f);

    const TPair<EFoundationDifficulty, float> Scaled[] = {{EFoundationDifficulty::Peaceful, 20},
        {EFoundationDifficulty::Easy, 17}, {EFoundationDifficulty::Normal, 16}, {EFoundationDifficulty::Hard, 14}};
    for (const auto& [Difficulty, Health] : Scaled)
    {
        FFoundationVitals D = Quiet();
        D.Difficulty = Difficulty;
        D.Hurt(EFoundationDamage::MobAttack, 4);
        TestEqual(TEXT("mob damage scales with difficulty"), D.Health, Health);
        D = Quiet();
        D.Difficulty = Difficulty;
        D.Hurt(EFoundationDamage::Generic, 4);
        TestEqual(TEXT("sourceless damage does not scale"), D.Health, 16.f);
    }

    FFoundationVitals Creative = Quiet();
    Creative.Mode = EFoundationPlayerMode::Creative;
    TestFalse(TEXT("creative ignores ordinary damage"), Creative.Hurt(EFoundationDamage::Generic, 5));
    TestFalse(TEXT("creative ignores falls"), Creative.Hurt(EFoundationDamage::Fall, 5));
    TestTrue(
        TEXT("kill bypasses invulnerability"), Creative.Hurt(EFoundationDamage::Kill, TNumericLimits<float>::Max()));
    TestTrue(TEXT("killed"), Creative.IsDead());

    FFoundationVitals K = Quiet();
    K.Armor = 5;
    K.AddExperience(20);
    K.Hurt(EFoundationDamage::Kill, TNumericLimits<float>::Max());
    TestTrue(TEXT("dead at zero health"), K.IsDead() && K.Health == 0);
    TestEqual(TEXT("death message"), K.DeathMessage, FString(TEXT("Player was killed")));
    TestFalse(TEXT("the dead take no damage"), K.Hurt(EFoundationDamage::Generic, 1));
    K.Heal(5);
    TestEqual(TEXT("the dead do not heal"), K.Health, 0.f);
    StepTimes(K, Standing(), FFoundationVitals::RespawnTicks - 1);
    TestFalse(TEXT("death plays out for a second"), K.ShouldRespawn());
    K.Step(Standing());
    TestTrue(TEXT("respawn due after 20 ticks"), K.ShouldRespawn());
    K.Respawn();
    TestTrue(TEXT("respawn restores health"), !K.IsDead() && K.Health == 20);
    TestEqual(TEXT("experience is lost"), K.Level, 0);
    TestEqual(TEXT("equipment stand-in kept"), K.Armor, 5);
    TestEqual(TEXT("death counted"), K.Deaths, 1);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFoundationVitalsFoodTest, "Unnamed.Foundation.Vitals.Food",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FFoundationVitalsFoodTest::RunTest(const FString&)
{
    FFoundationVitals F = Quiet();
    F.Exhaustion = 4;
    F.Step(Standing());
    TestEqual(TEXT("exactly four exhaustion waits"), F.Saturation, 5.f);
    F.Exhaustion = 4.5f;
    F.Step(Standing());
    TestEqual(TEXT("saturation pays first"), F.Saturation, 4.f);
    TestTrue(TEXT("four exhaustion spent"), FMath::IsNearlyEqual(F.Exhaustion, .5f, 1e-6f));
    F.Saturation = 0;
    F.Exhaustion = 4.5f;
    F.Step(Standing());
    TestEqual(TEXT("then food"), F.Food, 19);
    F.AddExhaustion(100);
    TestEqual(TEXT("exhaustion caps at 40"), F.Exhaustion, 40.f);

    FFoundationVitals Fast;
    Fast.Health = 10;
    StepTimes(Fast, Standing(), 9);
    TestEqual(TEXT("saturated regeneration waits ten ticks"), Fast.Health, 10.f);
    Fast.Step(Standing());
    TestTrue(TEXT("heals saturation/6"), FMath::IsNearlyEqual(Fast.Health, 10.f + 5.f / 6.f, 1e-5f));
    TestEqual(TEXT("and spends that much saturation as exhaustion"), Fast.Exhaustion, 5.f);

    FFoundationVitals Slow;
    Slow.Health = 10;
    Slow.Food = 18;
    Slow.Saturation = 0;
    StepTimes(Slow, Standing(), 79);
    TestEqual(TEXT("food 18 waits 80 ticks"), Slow.Health, 10.f);
    Slow.Step(Standing());
    TestEqual(TEXT("then heals one point"), Slow.Health, 11.f);
    TestEqual(TEXT("for six exhaustion"), Slow.Exhaustion, 6.f);

    FFoundationVitals Off = Quiet();
    Off.Health = 10;
    StepTimes(Off, Standing(), 200);
    TestEqual(TEXT("game rule stops regeneration"), Off.Health, 10.f);

    const TPair<EFoundationDifficulty, float> Floors[] = {
        {EFoundationDifficulty::Easy, 10}, {EFoundationDifficulty::Normal, 1}, {EFoundationDifficulty::Hard, 0}};
    for (const auto& [Difficulty, Floor] : Floors)
    {
        FFoundationVitals S;
        S.Difficulty = Difficulty;
        S.Food = 0;
        S.Saturation = 0;
        StepTimes(S, Standing(), 79);
        TestEqual(TEXT("starvation waits 80 ticks"), S.Health, 20.f);
        StepTimes(S, Standing(), 1700);
        TestEqual(TEXT("starvation floor by difficulty"), S.Health, Floor);
        TestEqual(TEXT("only Hard starves to death"), S.IsDead(), Difficulty == EFoundationDifficulty::Hard);
    }

    // Peaceful refills: a point of health every 20 ticks, of food every 10, so nobody starves.
    FFoundationVitals Peace;
    Peace.Difficulty = EFoundationDifficulty::Peaceful;
    Peace.Health = 10;
    Peace.Food = 0;
    Peace.Saturation = 0;
    StepTimes(Peace, Standing(), 19);
    TestEqual(TEXT("Peaceful heals once a second"), Peace.Health, 10.f);
    Peace.Step(Standing());
    TestEqual(TEXT("...a point at a time"), Peace.Health, 11.f);
    TestEqual(TEXT("...and refills food every half second"), Peace.Food, 2);
    StepTimes(Peace, Standing(), 1700);
    TestTrue(TEXT("Peaceful never starves: full health and food"), Peace.Health == 20.f && Peace.Food == 20);
    FFoundationVitals PeaceOff = Quiet();
    PeaceOff.Difficulty = EFoundationDifficulty::Peaceful;
    PeaceOff.Health = 10;
    StepTimes(PeaceOff, Standing(), 200);
    TestEqual(TEXT("the regeneration game rule stops Peaceful healing too"), PeaceOff.Health, 10.f);

    FFoundationVitals Sprint;
    Sprint.Food = 7;
    TestTrue(TEXT("seven food can sprint"), Sprint.HasEnoughFood());
    Sprint.Food = 6;
    TestFalse(TEXT("six food cannot"), Sprint.HasEnoughFood());
    Sprint.Mode = EFoundationPlayerMode::Creative;
    TestTrue(TEXT("flying modes ignore food"), Sprint.HasEnoughFood());

    FFoundationVitals Eat;
    Eat.Food = 15;
    Eat.Eat(8, 12.8f);
    TestEqual(TEXT("food caps at 20"), Eat.Food, 20);
    TestTrue(TEXT("saturation added"), FMath::IsNearlyEqual(Eat.Saturation, 17.8f, 1e-5f));
    Eat.Food = 3;
    Eat.Saturation = 0;
    Eat.Eat(1, 12.8f);
    TestEqual(TEXT("saturation never exceeds food"), Eat.Saturation, 4.f);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFoundationVitalsAirTest, "Unnamed.Foundation.Vitals.Air",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FFoundationVitalsAirTest::RunTest(const FString&)
{
    FFoundationVitalsStep Under = Standing();
    Under.bBodyInWater = Under.bEyesInWater = true;
    FFoundationVitals A = Quiet();
    StepTimes(A, Under, 300);
    TestEqual(TEXT("15 seconds to zero air"), A.Air, 0);
    TestEqual(TEXT("no damage before -20"), A.Health, 20.f);
    StepTimes(A, Under, 19);
    TestEqual(TEXT("negative air"), A.Air, -19);
    A.Step(Under);
    TestEqual(TEXT("drowning resets air"), A.Air, 0);
    TestEqual(TEXT("drowning costs one heart"), A.Health, 18.f);
    TestTrue(TEXT("recorded as drowning"), A.LastDamage.StartsWith(TEXT("drown")));
    StepTimes(A, Under, 20);
    TestEqual(TEXT("then one heart per second"), A.Health, 16.f);

    A.Air = 0;
    StepTimes(A, Standing(), 74);
    TestEqual(TEXT("refills four per tick"), A.Air, 296);
    A.Step(Standing());
    TestEqual(TEXT("3.75 seconds from empty"), A.Air, 300);

    FFoundationVitals Creative = Quiet();
    Creative.Mode = EFoundationPlayerMode::Creative;
    Creative.Air = 200;
    StepTimes(Creative, Under, 100);
    TestEqual(TEXT("invulnerable air neither falls nor refills underwater"), Creative.Air, 200);
    Creative.Step(Standing());
    TestEqual(TEXT("but refills on the surface"), Creative.Air, 204);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFoundationVitalsMovementTest, "Unnamed.Foundation.Vitals.Movement",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FFoundationVitalsMovementTest::RunTest(const FString&)
{
    auto Exhaustion = [](FFoundationVitalsStep Facts)
    {
        FFoundationVitals V = Quiet();
        V.Step(Facts);
        return V.Exhaustion;
    };
    FFoundationVitalsStep Facts = Standing();
    Facts.DisplacementCm = {28.06, 0, 0};
    TestEqual(TEXT("walking is free"), Exhaustion(Facts), 0.f);
    Facts.bSprinting = true;
    TestTrue(TEXT("ground sprint 0.1 per block of whole cm"), FMath::IsNearlyEqual(Exhaustion(Facts), .028f, 1e-7f));
    Facts.bGrounded = false;
    TestEqual(TEXT("airborne sprint is free"), Exhaustion(Facts), 0.f);
    Facts.bJumped = true;
    TestTrue(TEXT("jump 0.05"), FMath::IsNearlyEqual(Exhaustion(Facts), .05f, 1e-7f));
    Facts.bSprintJumped = true;
    TestTrue(TEXT("sprint jump 0.2"), FMath::IsNearlyEqual(Exhaustion(Facts), .2f, 1e-7f));
    Facts = Standing();
    Facts.DisplacementCm = {6, 0, 8};
    Facts.bBodyInWater = Facts.bEyesInWater = true;
    TestTrue(TEXT("underwater 0.01 per block, 3D"), FMath::IsNearlyEqual(Exhaustion(Facts), .001f, 1e-8f));
    Facts.bEyesInWater = false;
    TestTrue(TEXT("wading 0.01 per block, horizontal"), FMath::IsNearlyEqual(Exhaustion(Facts), .0006f, 1e-8f));

    TestEqual(TEXT("three blocks are safe"), FFoundationVitals::GetFallDamage(300), 0);
    // fall.damage_thresholds: 3.5 and 3.9 blocks give nothing, 4.0 one point.
    TestEqual(TEXT("3.5 blocks are safe"), FFoundationVitals::GetFallDamage(350), 0);
    TestEqual(TEXT("3.9 blocks are safe"), FFoundationVitals::GetFallDamage(390), 0);
    TestEqual(TEXT("just under four"), FFoundationVitals::GetFallDamage(399.9), 0);
    TestEqual(TEXT("four blocks: one point"), FFoundationVitals::GetFallDamage(400), 1);
    TestEqual(TEXT("float shortfall keeps four"), FFoundationVitals::GetFallDamage(399.99999), 1);
    TestEqual(TEXT("five blocks"), FFoundationVitals::GetFallDamage(500), 2);
    TestEqual(TEXT("sprint-jump off five"), FFoundationVitals::GetFallDamage(625.2), 3);

    FFoundationVitalsStep Up, Down, Land = Standing();
    Up.DisplacementCm = {0, 0, 50};
    Down.DisplacementCm = {0, 0, -100};
    Land.DisplacementCm = {0, 0, -50};
    FFoundationVitals Fall = Quiet();
    Fall.Step(Up);
    StepTimes(Fall, Down, 4);
    TestEqual(TEXT("rising does not count, falling does"), Fall.FallDistanceCm, 400.);
    Fall.Step(Land);
    TestEqual(TEXT("4.5 blocks land for one point"), Fall.Health, 19.f);
    TestEqual(TEXT("landing resets the fall"), Fall.FallDistanceCm, 0.);
    TestTrue(TEXT("fall damage recorded"), Fall.LastDamage.StartsWith(TEXT("fall")));
    FFoundationVitalsStep Splash = Down;
    Splash.bBodyInWater = true;
    StepTimes(Fall, Down, 9);
    Fall.Step(Splash);
    Fall.Step(Land);
    TestEqual(TEXT("water cancels a fall"), Fall.Health, 19.f);
    FFoundationVitals Creative = Quiet();
    Creative.Mode = EFoundationPlayerMode::Creative;
    StepTimes(Creative, Down, 10);
    Creative.Step(Land);
    TestEqual(TEXT("creative takes no fall damage"), Creative.Health, 20.f);

    // The simulation supplies the jump facts and lets callers run each due step themselves.
    const auto& P = *GetDefault<UFoundationProfile>();
    const TArray<FBox> Ground = {FBox(FVector(-100000, -100000, -100), FVector(100000, 100000, 0))};
    FFoundationSimulation S;
    S.Reset({}, P);
    TestEqual(TEXT("two steps due in 0.12 s"), S.TakeDueSteps(.12, P), 2);
    TestTrue(TEXT("remainder banked"), FMath::IsNearlyEqual(S.Accumulator, .02, 1e-9));
    TestEqual(TEXT("catch-up stays bounded"), S.TakeDueSteps(2., P), P.MaxCatchupSteps);
    S.Reset({}, P);
    S.bGrounded = true;
    FFoundationIntent Intent;
    Intent.Move = {0, 1};
    Intent.bSprint = true;
    for (int32 T = 0; T < 40; ++T)
        S.Step(Intent, 0, P, Ground);
    TestFalse(TEXT("no jump yet"), S.bJumped);
    Intent.bJump = true;
    S.Step(Intent, 0, P, Ground);
    TestTrue(TEXT("sprint jump reported"), S.bJumped && S.bSprintJumped);
    S.Step(Intent, 0, P, Ground);
    TestFalse(TEXT("one report per jump"), S.bJumped);

    // Ten seconds of sprinting through the real simulation spends saturation, not food; walking is free.
    for (bool bSprint : {false, true})
    {
        FFoundationVitals V = Quiet();
        S.Reset({}, P);
        S.bGrounded = true;
        Intent = {};
        Intent.Move = {0, 1};
        Intent.bSprint = bSprint;
        for (int32 T = 0; T < 200; ++T)
        {
            S.Step(Intent, 0, P, Ground);
            FFoundationVitalsStep Step;
            Step.DisplacementCm = S.LastDisplacement;
            Step.bGrounded = S.bGrounded;
            Step.bSprinting = S.bSprinting;
            V.Step(Step);
        }
        TestEqual(bSprint ? TEXT("sprinting spends saturation") : TEXT("walking spends nothing"), V.Saturation,
            bSprint ? 4.f : 5.f);
        TestEqual(TEXT("food untouched"), V.Food, 20);
    }
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFoundationVitalsExperienceTest, "Unnamed.Foundation.Vitals.Experience",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FFoundationVitalsExperienceTest::RunTest(const FString&)
{
    const TPair<int32, int32> Needed[] = {{0, 7}, {14, 35}, {15, 37}, {29, 107}, {30, 112}, {31, 121}};
    for (const auto& [Level, Points] : Needed)
    {
        FFoundationVitals V;
        V.Level = Level;
        TestEqual(FString::Printf(TEXT("points for level %d"), Level + 1), V.GetExperienceForNextLevel(), Points);
    }
    FFoundationVitals X;
    X.AddExperience(7);
    TestTrue(TEXT("seven points make level 1"), X.Level == 1 && X.Progress == 0);
    X = {};
    X.AddExperience(30);
    TestEqual(TEXT("30 points cross three levels"), X.Level, 3);
    TestTrue(TEXT("with 3 of 13 left over"), FMath::IsNearlyEqual(X.Progress, 3.f / 13.f, 1e-5f));
    TestEqual(TEXT("total kept"), X.TotalExperience, 30);
    X = {};
    X.AddExperience(352);
    TestTrue(TEXT("352 points reach level 16"), FMath::IsNearlyEqual(X.Level + X.Progress, 16.f, 1e-3f));
    X.AddLevels(-20);
    TestTrue(TEXT("levels never go negative"), X.Level == 0 && X.Progress == 0 && X.TotalExperience == 0);
    X.AddExperience(-3);
    TestTrue(TEXT("negative points stop at zero"), X.Level == 0 && X.Progress == 0);
    X.AddExperience(10);
    X.AddExperience(-4);
    TestEqual(TEXT("negative points step back a level"), X.Level, 0);
    TestTrue(TEXT("to six of seven"), FMath::IsNearlyEqual(X.Progress, 6.f / 7.f, 1e-5f));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFoundationVitalsAttackTest, "Unnamed.Foundation.Vitals.Attack",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FFoundationVitalsAttackTest::RunTest(const FString&)
{
    FFoundationVitals V = Quiet();
    TestEqual(TEXT("empty hand recovers in 5 ticks"), V.GetAttackDelay(), 5.f);
    StepTimes(V, Standing(), 4);
    TestTrue(TEXT("recovering"), FMath::IsNearlyEqual(V.GetAttackStrength(), .8f, 1e-6f));
    V.Step(Standing());
    TestEqual(TEXT("recovered"), V.GetAttackStrength(), 1.f);
    V.AttackSpeed = 1.6f;
    V.ResetAttackStrength();
    TestEqual(TEXT("swing restarts recovery"), V.GetAttackStrength(), 0.f);
    StepTimes(V, Standing(), 12);
    TestTrue(TEXT("sword-like 12.5-tick recovery"), V.GetAttackStrength() < 1.f && V.GetAttackDelay() > 12.f);
    V.Step(Standing());
    TestEqual(TEXT("recovered after 13 ticks"), V.GetAttackStrength(), 1.f);
    V.UpdateMainHand(TEXT("token"));
    TestEqual(TEXT("a different main-hand item restarts recovery"), V.GetAttackStrength(), 0.f);
    StepTimes(V, Standing(), 13);
    V.UpdateMainHand(TEXT("token"));
    TestEqual(TEXT("the same item does not"), V.GetAttackStrength(), 1.f);
    V.ResetAttackStrength();
    V.Hurt(EFoundationDamage::Kill, TNumericLimits<float>::Max());
    StepTimes(V, Standing(), 13);
    TestEqual(TEXT("recovery keeps counting through the death animation"), V.GetAttackStrength(), 1.f);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFoundationVitalsFireTest, "Unnamed.Foundation.Vitals.Fire",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FFoundationVitalsFireTest::RunTest(const FString&)
{
    // hurt.lava_contact: 4 HP at ticks 1, 11, 21 (the damage cooldown rejects the equal hits between).
    FFoundationVitalsStep InLava = Standing();
    InLava.bBodyInLava = true;
    FFoundationVitals V = Quiet();
    TArray<int32> Hits;
    for (int32 Tick = 1; Tick <= 21; ++Tick)
    {
        const float Before = V.Health;
        V.Step(InLava);
        if (V.Health < Before) Hits.Add(Tick);
    }
    TestEqual(TEXT("lava hits at ticks 1, 11, 21"), Hits, TArray<int32>{1, 11, 21});
    TestEqual(TEXT("4 HP each"), V.Health, 8.f);
    TestEqual(TEXT("three lava hits counted"), V.LavaHurts, 3);
    TestEqual(TEXT("no burning hit inside lava"), V.BurnHurts, 0);
    // hurt.lava_ignite: 300 fire ticks, refreshed every lava tick.
    TestEqual(TEXT("lava ignites for 300 ticks"), V.FireTicks, FFoundationVitals::LavaFireTicks);
    TestTrue(TEXT("lava damage recorded"), V.LastDamage.StartsWith(TEXT("lava")));

    // hurt.burning: out of lava, 1 HP every 20 ticks until the fire ticks run out.
    FFoundationVitals B = Quiet();
    B.FireTicks = 300;
    Hits.Reset();
    for (int32 Tick = 1; Tick <= 320; ++Tick)
    {
        const float Before = B.Health;
        B.Step(Standing());
        if (B.Health < Before) Hits.Add(Tick);
    }
    TestEqual(TEXT("fifteen burning hits"), Hits.Num(), 15);
    TestTrue(TEXT("every 20 ticks"), Hits.Num() == 15 && Hits[0] == 1 && Hits[1] == 21 && Hits.Last() == 281);
    TestEqual(TEXT("1 HP each"), B.Health, 5.f);
    TestEqual(TEXT("burnt out"), B.FireTicks, 0);
    TestEqual(TEXT("burning hits counted"), B.BurnHurts, 15);
    TestTrue(TEXT("burning recorded"), B.LastDamage.StartsWith(TEXT("onFire")));

    // Armor: lava's damage goes through it, burning's bypasses it (26.3 tags/damage_type/bypasses_armor).
    FFoundationVitals Armored = Quiet();
    Armored.Armor = 20;
    Armored.Step(InLava);
    TestTrue(TEXT("armor softens lava"), Armored.Health > 16.f && Armored.Health < 20.f);
    FFoundationVitals ArmoredBurn = Quiet();
    ArmoredBurn.Armor = 20;
    ArmoredBurn.FireTicks = 20;
    ArmoredBurn.Step(Standing());
    TestEqual(TEXT("burning ignores armor"), ArmoredBurn.Health, 19.f);

    // Water puts the fire out, with no hit.
    FFoundationVitals W = Quiet();
    W.FireTicks = 100;
    FFoundationVitalsStep Wet = Standing();
    Wet.bBodyInWater = true;
    W.Step(Wet);
    TestEqual(TEXT("water puts the fire out"), W.FireTicks, 0);
    TestEqual(TEXT("one extinguish counted"), W.Extinguishes, 1);
    TestEqual(TEXT("no hit"), W.Health, 20.f);
    W.Step(Wet);
    TestEqual(TEXT("an unlit player in water counts no extinguish"), W.Extinguishes, 1);

    // The damage cooldown: a stronger hit inside the window applies only its excess, a weaker one nothing.
    FFoundationVitals C = Quiet();
    C.Hurt(EFoundationDamage::Generic, 2.f);
    C.Step(InLava);
    TestEqual(TEXT("lava inside a 2 HP window deals the 2 HP excess"), C.Health, 16.f);
    FFoundationVitals D = Quiet();
    D.Step(InLava);
    D.FireTicks = 20;
    D.Step(Standing());
    TestEqual(TEXT("a burning hit inside lava's window is rejected"), D.Health, 16.f);

    // Death messages (minecraft.wiki "Death messages"), shown in chat by T4 step 11.
    FFoundationVitals Dies = Quiet();
    Dies.Health = 3;
    Dies.Step(InLava);
    TestEqual(TEXT("lava's death message"), Dies.DeathMessage, FString(TEXT("Player tried to swim in lava")));
    FFoundationVitals Burns = Quiet();
    Burns.Health = 1;
    Burns.FireTicks = 20;
    Burns.Step(Standing());
    TestEqual(TEXT("burning's death message"), Burns.DeathMessage, FString(TEXT("Player burned to death")));

    // Creative and Spectator neither ignite nor take the damage; no lava fact (foundation.Fire 0) changes nothing.
    for (EFoundationPlayerMode Mode : {EFoundationPlayerMode::Creative, EFoundationPlayerMode::Spectator})
    {
        FFoundationVitals Immune = Quiet();
        Immune.Mode = Mode;
        StepTimes(Immune, InLava, 40);
        TestTrue(TEXT("immune modes stay whole and unlit"), Immune.Health == 20.f && Immune.FireTicks == 0);
    }
    FFoundationVitals Off = Quiet();
    StepTimes(Off, Standing(), 40);
    TestTrue(TEXT("no lava, no fire"), Off.Health == 20.f && Off.FireTicks == 0 && Off.LavaHurts == 0);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFoundationVitalsArmorTest, "Unnamed.Foundation.Vitals.Armor",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FFoundationVitalsArmorTest::RunTest(const FString&)
{
    // Survival step 19a. armor.formula / armor.reduction_grid (the value report, "Armor formula"): spot values of
    // divisor = 2 + toughness / 4, effective = min(max(armor - damage / divisor, armor x 0.2), 20),
    // taken = damage x (1 - effective / 25).
    TestTrue(TEXT("grid: armor 8, toughness 0, 10 damage -> 8.8"),
        FMath::IsNearlyEqual(FFoundationVitals::GetDamageAfterArmor(10, 8, 0), 8.8f, 1e-5f));
    TestTrue(TEXT("grid: armor 20, toughness 12, 20 damage -> 7.2"),
        FMath::IsNearlyEqual(FFoundationVitals::GetDamageAfterArmor(20, 20, 12), 7.2f, 1e-5f));
    TestTrue(TEXT("grid: armor 20, toughness 0, 1 damage -> 0.22"),
        FMath::IsNearlyEqual(FFoundationVitals::GetDamageAfterArmor(1, 20, 0), .22f, 1e-5f));
    // The worn pieces add to the lab's base (diamond chestplate: 8 and 2); the attributes stay within 0-30 and 0-20.
    FFoundationVitals V = Quiet();
    V.WornArmor = 8;
    V.WornToughness = 2;
    TestTrue(TEXT("worn: 8 and 2"), V.GetArmor() == 8 && V.GetArmorToughness() == 2.f);
    TestTrue(TEXT("a player attack lands"), V.Hurt(EFoundationDamage::PlayerAttack, 10));
    // divisor 2.5: max(8 - 4, 1.6) = 4, so 10 x (1 - 4 / 25) = 8.4.
    TestTrue(TEXT("the worn armor reduces it to 8.4"), FMath::IsNearlyEqual(V.Health, 20.f - 8.4f, 1e-5f));
    TestEqual(TEXT("10 damage owes 2 wear"), V.PendingArmorWear, 2);
    V.Armor = 25;
    V.ArmorToughness = 19;
    TestTrue(TEXT("base + worn capped: 30 and 20"), V.GetArmor() == 30 && V.GetArmorToughness() == 20.f);
    // A small hit still owes 1; damage armor does not apply to owes none.
    FFoundationVitals Small = Quiet();
    Small.Hurt(EFoundationDamage::PlayerAttack, 1);
    TestEqual(TEXT("1 damage owes 1 wear"), Small.PendingArmorWear, 1);
    FFoundationVitals Bypass = Quiet();
    Bypass.Hurt(EFoundationDamage::Generic, 10);
    TestEqual(TEXT("armor-bypassing damage owes none"), Bypass.PendingArmorWear, 0);
    // Lava is #minecraft:is_fire: its wear is owed as fire wear too, which fire-resistant pieces skip (Codex 31 #4).
    TestEqual(TEXT("a player attack owes no fire wear"), V.PendingFireArmorWear, 0);
    FFoundationVitals Lava = Quiet();
    Lava.Hurt(EFoundationDamage::Lava, 8);
    TestTrue(TEXT("lava 8 owes 2 wear, all of it fire"), Lava.PendingArmorWear == 2 && Lava.PendingFireArmorWear == 2);
    // Respawn keeps what is worn (the holder sets it again each step).
    V.Respawn();
    TestTrue(TEXT("respawn keeps the worn pieces"), V.WornArmor == 8 && V.WornToughness == 2.f);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFoundationVitalsFireBlockTest, "Unnamed.Foundation.Vitals.FireBlock",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FFoundationVitalsFireBlockTest::RunTest(const FString&)
{
    // Survival step 16c, fire blocks [survival2.r14.player_fire, SURVIVAL-RUN2-FINDINGS-26.3.md lines 107-118 and the
    // trace at 39351-39471].
    FFoundationVitalsStep InFire = Standing();
    InFire.FireBlockDamage = 1;
    {
        // F1-F3: 120 ticks in fire at 1024 health: 1 every 10 ticks from tick 1 (12 in all); not burning for ticks
        // 1-19, 160 fire ticks at tick 20, then a net 0 or +1 a tick.
        FFoundationVitals V = Quiet();
        V.MaxHealth = V.Health = 1024;
        bool bCadence = true, bGrace = true, bGrowth = true;
        int32 Grown = 0, Before = 0;
        for (int32 Tick = 1; Tick <= 120; ++Tick)
        {
            const float Health = V.Health;
            V.Step(InFire);
            const float Expected = Tick % 10 == 1 ? 1.f : 0.f;
            if (Health - V.Health != Expected)
            {
                bCadence = false;
                AddError(FString::Printf(TEXT("tick %d: lost %g, expected %g"), Tick, Health - V.Health, Expected));
            }
            if (Tick < 20 && V.FireTicks != 0) bGrace = false;
            if (Tick == 20 && V.FireTicks != 160) bGrace = false;
            if (Tick > 20)
            {
                const int32 Change = V.FireTicks - Before;
                if (Change != 0 && Change != 1) bGrowth = false;
                Grown += Change;
            }
            Before = V.FireTicks;
        }
        TestTrue(TEXT("F1: one hit every 10 ticks from tick 1"), bCadence);
        TestEqual(TEXT("F1: 12 health in 120 ticks"), V.Health, 1012.f);
        TestTrue(TEXT("F2: not burning for 19 ticks, 160 fire ticks at the 20th"), bGrace);
        TestTrue(TEXT("F3: the fire ticks change by 0 or +1 a tick in fire"), bGrowth);
        // Measured: 210 after 120 ticks (50 of 100 ticks grew); the hash's split must stay near 50/50.
        TestTrue(TEXT("F3: they grow about every other tick"), Grown >= 30 && Grown <= 70);
        TestEqual(TEXT("F3: the grace is used up"), V.FireGraceTicks, 0);
        TestTrue(TEXT("F1: the hits count as fire block or burning hits"), V.FireBlockHurts + V.BurnHurts == 12);
        // F4: out of the fire it burns as step 16a: 1 every 20 ticks, one fire tick less each tick.
        const int32 Fire = V.FireTicks, Burns = V.BurnHurts;
        StepTimes(V, Standing(), 41);
        TestEqual(TEXT("F4: burning counts down"), V.FireTicks, Fire - 41);
        TestTrue(TEXT("F4: burning hurts outside the fire"), V.BurnHurts - Burns >= 2 && V.BurnHurts - Burns <= 3);
        // The fire burns out without water: the grace stays spent and the next touch ignites at once (provisional).
        StepTimes(V, Standing(), V.FireTicks);
        TestEqual(TEXT("burned out"), V.FireTicks, 0);
        V.Step(InFire);
        TestEqual(TEXT("burned out: the next touch ignites at once"), V.FireTicks, 160);
    }
    {
        // F5: soul fire hurts 2 (minecraft.wiki "Soul Fire"), at ticks 1, 11 and 21.
        FFoundationVitals V = Quiet();
        FFoundationVitalsStep Soul = Standing();
        Soul.FireBlockDamage = 2;
        StepTimes(V, Soul, 21);
        TestEqual(TEXT("F5: soul fire takes 6 in 21 ticks"), V.Health, 14.f);
        // Tick 21 burns first (160 fire ticks), so its soul fire hit deals only the 1 above the burn.
        TestTrue(TEXT("F5: soul fire hits"), V.FireBlockHurts >= 2 && V.FireBlockHurts + V.BurnHurts >= 3);
    }
    {
        // F6: armor applies to in_fire (26.3 tags/damage_type/bypasses_armor.json lacks it); it is #is_fire wear.
        FFoundationVitals V = Quiet();
        V.Armor = 20;
        V.Step(InFire);
        TestTrue(TEXT("F6: armor softens the hit"), V.Health > 19.f && V.Health < 20.f);
        TestEqual(TEXT("F6: fire armor wear"), V.PendingFireArmorWear, 1);
        // F11: exhaustion 0.1 (26.3 damage_type/in_fire.json).
        FFoundationVitals E = Quiet();
        E.Step(InFire);
        TestTrue(TEXT("F11: a hit costs 0.1 exhaustion"), FMath::IsNearlyEqual(E.Exhaustion, .1f, 1e-6f));
    }
    {
        // F7: the death message (en_us.json death.attack.inFire).
        FFoundationVitals V = Quiet();
        V.Health = 1;
        V.Step(InFire);
        TestTrue(TEXT("F7: dead"), V.IsDead());
        TestEqual(TEXT("F7: key"), V.DeathKey, FString(TEXT("death.attack.inFire")));
        TestEqual(TEXT("F7: message"), V.DeathMessage, FString(TEXT("Player went up in flames")));
    }
    {
        // F8: water puts a burning player out and restores the grace (the water case's final -20 fire ticks).
        FFoundationVitals V = Quiet();
        V.FireTicks = 100;
        V.FireGraceTicks = 0;
        FFoundationVitalsStep Water = Standing();
        Water.bBodyInWater = true;
        V.Step(Water);
        TestTrue(TEXT("F8: out, the grace back"), V.FireTicks == 0 && V.FireGraceTicks == 20);
        StepTimes(V, InFire, 19);
        TestEqual(TEXT("F8: 19 ticks in fire, not burning"), V.FireTicks, 0);
        V.Step(InFire);
        TestEqual(TEXT("F8: the 20th ignites"), V.FireTicks, 160);
    }
    for (const EFoundationPlayerMode Mode : {EFoundationPlayerMode::Creative, EFoundationPlayerMode::Spectator})
    {
        // F9: Creative and Spectator neither take the damage nor ignite.
        FFoundationVitals V = Quiet();
        V.Mode = Mode;
        StepTimes(V, InFire, 40);
        TestTrue(TEXT("F9: untouched"), V.Health == 20.f && V.FireTicks == 0 && V.FireGraceTicks == 20);
    }
    {
        // F10: no fire fact (foundation.FireBlocks 0 passes none), nothing happens.
        FFoundationVitals V = Quiet();
        StepTimes(V, Standing(), 40);
        TestTrue(TEXT("F10: nothing"), V.Health == 20.f && V.FireTicks == 0 && V.FireBlockHurts == 0);
    }
    {
        // F12: a lava cauldron's lava hurts as lava: 4 at ticks 1, 11 and 21, 300 fire ticks at once.
        FFoundationVitals V = Quiet();
        V.MaxHealth = V.Health = 1024;
        FFoundationVitalsStep Cauldron = Standing();
        Cauldron.bLavaContact = true;
        V.Step(Cauldron);
        TestEqual(TEXT("F12: lava ignites at once"), V.FireTicks, 300);
        StepTimes(V, Cauldron, 20);
        TestEqual(TEXT("F12: 12 in 21 ticks"), V.Health, 1012.f);
        TestEqual(TEXT("F12: three lava hits"), V.LavaHurts, 3);
    }
    {
        // Respawn keeps the count and restores the grace.
        FFoundationVitals V = Quiet();
        StepTimes(V, InFire, 25);
        const int32 Hurts = V.FireBlockHurts;
        V.Respawn();
        TestTrue(
            TEXT("respawn: count kept, grace back"), Hurts >= 2 && V.FireBlockHurts == Hurts && V.FireGraceTicks == 20);
    }
    return true;
}
#endif
