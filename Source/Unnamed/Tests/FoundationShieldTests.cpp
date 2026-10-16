#include "Misc/AutomationTest.h"
#include "Foundation/FoundationAuthority.h"
#include "Foundation/FoundationBlockRegistry.h"
#include "Foundation/FoundationInteraction.h"
#include "Foundation/FoundationInteractionSettings.h"
#include "Foundation/FoundationPawn.h"

#if WITH_DEV_AUTOMATION_TESTS
// Survival step 19b: the shield. Values are literals from the 26.3 component report (items.json's blocks_attacks), the
// run-2 findings (R18: survival2.r18.shield_delay), the value report ("Armor / shield": the raise delay, the axe
// disable) and minecraft.wiki ("Data component format/blocks_attacks", "Shield"), never the constants under test.
namespace
{
    constexpr EAutomationTestFlags ShieldTestFlags =
        EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter;

    // The project's rules (DA_Items), lab tool wear on, a shield in hotbar slot 1 (selected), the player at the origin
    // looking along +X.
    struct FShieldLab
    {
        FFoundationAuthority A;
        FFoundationVitals Vitals;
        FFoundationInteraction I;
        int32 Player = 0, Shield = INDEX_NONE;
        int64 StepIndex = 0;

        FShieldLab()
        {
            UFoundationInteractionSettings::Get().BuildRules(A.Rules);
            A.World.SetRegistry(FFoundationBlockRegistry::GetDefault());
            A.World.BoundsMin = FIntVector(-20, -20, 0);
            A.World.BoundsMax = FIntVector(19, 19, 15);
            A.Options.bToolWear = true;
            A.Options.bItemUse = true;
            Player = A.AddPlayer();
            A.Reset();
            Vitals.bNaturalRegeneration = false;
            I.PlayerId = Player;
            I.Resync(A);
            NextStep();
            Shield = A.Rules.FindItem(TEXT("shield"));
            if (Shield != INDEX_NONE) P().Inventory.SetSlot(0, P().Inventory.MakeStack(Shield, 1));
            P().Inventory.Select(0);
        }

        FFoundationPlayer& P() { return *A.FindPlayer(Player); }

        void NextStep()
        {
            FFoundationPlayerFacts Facts;
            Facts.FeetCm = FVector::ZeroVector;
            Facts.EyeCm = FVector(0, 0, 162);
            Facts.Aim = FVector::ForwardVector;
            Facts.Step = ++StepIndex;
            A.SetFacts(Player, Facts);
            A.Step();
        }

        FFoundationResult Exec(EFoundationCommandType Type, EFoundationHand Hand = EFoundationHand::Main)
        {
            FFoundationCommand Command;
            Command.SessionId = A.GetSessionId();
            Command.Sequence = I.NextSequence++;
            Command.Type = Type;
            Command.Hand = Hand;
            Command.Item = P().Inventory.GetHand(Hand).GetRef();
            return A.Execute(Player, Command, Vitals);
        }

        // Raise (a start), then Ticks continues, one per reference step.
        FFoundationResult Raise(int32 Ticks, EFoundationHand Hand = EFoundationHand::Main)
        {
            FFoundationResult Result = Exec(EFoundationCommandType::UseItemStart, Hand);
            for (int32 Index = 0; Index < Ticks; ++Index)
            {
                NextStep();
                Exec(EFoundationCommandType::UseItemContinue, Hand);
            }
            return Result;
        }

        // A mob hit of Amount from a source at Yaw degrees from the look (horizontal, 2 blocks away).
        bool Hit(
            float Amount, double Yaw, float DisableSeconds = 0, EFoundationDamage Type = EFoundationDamage::MobAttack)
        {
            FFoundationAttack Attack;
            Attack.Type = Type;
            Attack.Amount = Amount;
            Attack.SourceCm =
                FVector(FMath::Cos(FMath::DegreesToRadians(Yaw)), FMath::Sin(FMath::DegreesToRadians(Yaw)), 0) * 200;
            Attack.DisableBlockingSeconds = DisableSeconds;
            return A.HurtPlayer(Player, Vitals, Attack);
        }

        // A zombie's queued 3 from 2 blocks away at Yaw degrees from the look, through the pawn's hit step.
        void ZombieHit(FFoundationSimulation& Simulation, double Yaw)
        {
            const FVector From =
                FVector(FMath::Cos(FMath::DegreesToRadians(Yaw)), FMath::Sin(FMath::DegreesToRadians(Yaw)), 0) * 200;
            P().MobHits.Add({1, 3.f, TEXT("Zombie"), From});
            FVector2D Side(1, 0);
            AFoundationPawn::TakeHits(A, Player, Vitals, Simulation, Side);
        }

        int32 Count(EFoundationEventKind Kind) const
        {
            int32 Found = 0;
            for (const FFoundationEvent& Event : A.GetEvents())
                Found += Event.Kind == Kind;
            return Found;
        }

        int32 ShieldDamage() { return P().Inventory.Hotbar[0].Damage; }

        // Fresh vitals between hits, so the hurt window never hides a hit.
        void Heal()
        {
            Vitals.Health = 20;
            Vitals.DamageCooldown = 0;
            Vitals.LastHurt = 0;
        }
    };
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFoundationShieldDataTest, "Unnamed.Foundation.Shield.Data", ShieldTestFlags)

bool FFoundationShieldDataTest::RunTest(const FString&)
{
    // DA_Items' shield: items.json's blocks_attacks (block_delay_seconds 0.25 = 5 ticks, the value report's "Shield
    // raise delay"; the default reduction's 90 degrees, base 0, factor 1; item_damage threshold 3, base 1, factor 1),
    // durability 336, its GUI stand-in.
    FFoundationRules Rules;
    UFoundationInteractionSettings::Get().BuildRules(Rules);
    const int32 Shield = Rules.FindItem(TEXT("shield"));
    if (!TestTrue(TEXT("the shield is a DA_Items item"), Shield != INDEX_NONE)) return false;
    const FFoundationItemDefinition& S = Rules.Items[Shield];
    const FFoundationBlocksAttacks& B = S.BlocksAttacks;
    TestTrue(TEXT("blocks attacks: 5 ticks, 90 degrees, all of the damage"),
        B.bValid && B.DelayTicks == 5 && B.AngleDegrees == 90.f && B.Base == 0.f && B.Factor == 1.f &&
            B.DisableCooldownScale == 1.f);
    TestTrue(TEXT("item damage: from 3, 1 + 1 x damage"),
        B.ItemDamageThreshold == 3.f && B.ItemDamageBase == 1.f && B.ItemDamageFactor == 1.f);
    TestTrue(TEXT("sounds: item.shield.block, item.shield.break"),
        B.BlockSound == TEXT("item.shield.block") && B.DisabledSound == TEXT("item.shield.break"));
    TestTrue(TEXT("336 durability, one per stack, the stand-in sprite"),
        S.MaxDurability == 336 && S.MaxStack == 1 && S.Sprite == TEXT("item/shield"));
    const int32 Pickaxe = Rules.FindItem(TEXT("iron_pickaxe"));
    TestTrue(TEXT("a pickaxe does not block"), Pickaxe != INDEX_NONE && !Rules.Items[Pickaxe].BlocksAttacks.bValid);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFoundationShieldDelayTest, "Unnamed.Foundation.Shield.Delay", ShieldTestFlags)

bool FFoundationShieldDelayTest::RunTest(const FString&)
{
    // R18 (survival2.r18.shield_delay): uninterrupted use, blocking no at ticks 0-4, yes at 5-12, no once released.
    FShieldLab L;
    if (!TestTrue(TEXT("a shield in hand"), L.Shield != INDEX_NONE)) return false;
    const FFoundationResult Start = L.Exec(EFoundationCommandType::UseItemStart);
    TestTrue(TEXT("the use starts, held for 72000 ticks"), Start.IsCommitted() && Start.UseDuration == 72000);
    for (int32 Tick = 0; Tick <= 12; ++Tick)
    {
        TestEqual(FString::Printf(TEXT("tick %d"), Tick), L.A.IsBlocking(L.Player), Tick >= 5);
        L.NextStep();
        L.Exec(EFoundationCommandType::UseItemContinue);
    }
    TestTrue(TEXT("no eating events"),
        L.Count(EFoundationEventKind::Eating) == 0 && L.Count(EFoundationEventKind::Ate) == 0);
    TestTrue(TEXT("released"), L.Exec(EFoundationCommandType::UseItemRelease).Outcome != EFoundationOutcome::Rejected);
    TestFalse(TEXT("released: not blocking"), L.A.IsBlocking(L.Player));

    // The offhand: an empty main hand passes, the offhand's shield raises.
    FShieldLab Off;
    FFoundationInventory& I = Off.P().Inventory;
    I.SetSlot(FFoundationInventory::OffhandSlot, I.Hotbar[0]);
    I.SetSlot(0, {});
    TestEqual(TEXT("offhand: the empty main hand passes"), Off.Exec(EFoundationCommandType::UseItemStart).Outcome,
        EFoundationOutcome::Pass);
    TestTrue(TEXT("offhand: raised"), Off.Raise(5, EFoundationHand::Off).IsCommitted() && Off.A.IsBlocking(Off.Player));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFoundationShieldBlockTest, "Unnamed.Foundation.Shield.Block", ShieldTestFlags)

bool FFoundationShieldBlockTest::RunTest(const FString&)
{
    FShieldLab L;
    if (!TestTrue(TEXT("a shield in hand"), L.Shield != INDEX_NONE)) return false;
    // Not yet raised (tick 4): the hit lands.
    L.Raise(4);
    TestTrue(TEXT("tick 4: hurt"), L.Hit(4, 0) && L.Vitals.Health == 16.f);
    L.Heal();
    L.NextStep();
    L.Exec(EFoundationCommandType::UseItemContinue);
    // Raised: a hit from the front is blocked whole, no damage and no hurt window (provisional, SM-Player 19b Q3),
    // its block sound, and 1 + floor(4) = 5 wear (minecraft.wiki "Shield", provisional).
    TestFalse(TEXT("front: nothing hurt"), L.Hit(4, 0));
    TestTrue(TEXT("front: health 20, no hurt window"), L.Vitals.Health == 20.f && !L.Vitals.IsInHurtWindow());
    const FFoundationEvent* Blocked = L.A.GetEvents().FindByPredicate(
        [](const FFoundationEvent& Event) { return Event.Kind == EFoundationEventKind::Blocked; });
    TestTrue(TEXT("a Blocked event with item.shield.block, 4 blocked"),
        Blocked && Blocked->Interaction == TEXT("item.shield.block") && Blocked->Amount == 4.f);
    TestEqual(TEXT("worn 5"), L.ShieldDamage(), 5);
    // item_damage's threshold 3: 2.9 wears nothing, 3 wears 4, 7.5 wears 8.
    L.Hit(2.9f, 0);
    TestEqual(TEXT("2.9: no wear"), L.ShieldDamage(), 5);
    L.Hit(3, 0);
    TestEqual(TEXT("3: 4 more"), L.ShieldDamage(), 9);
    L.Hit(7.5f, 0);
    TestEqual(TEXT("7.5: 8 more"), L.ShieldDamage(), 17);
    // The angle (the default reduction's horizontal_blocking_angle 90): 90 blocks, 91 and behind do not.
    TestFalse(TEXT("90 degrees: blocked"), L.Hit(4, 90));
    TestTrue(TEXT("91 degrees: hurt"), L.Hit(4, 91) && L.Vitals.Health == 16.f);
    L.Heal();
    TestTrue(TEXT("behind: hurt"), L.Hit(4, 180) && L.Vitals.Health == 16.f);
    L.Heal();
    // Damage without a source (lava, 26.3's #bypasses_shield) is never blocked.
    TestTrue(TEXT("lava: hurt"), L.Hit(4, 0, 0, EFoundationDamage::Lava) && L.Vitals.Health < 20.f);
    L.Heal();
    // Creative: blocked, never worn.
    const int32 Worn = L.ShieldDamage();
    L.Vitals.Mode = EFoundationPlayerMode::Creative;
    TestFalse(TEXT("creative: blocked"), L.Hit(4, 0));
    TestTrue(TEXT("creative: health 20, no wear"), L.Vitals.Health == 20.f && L.ShieldDamage() == Worn);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFoundationShieldDisableTest, "Unnamed.Foundation.Shield.Disable", ShieldTestFlags)

bool FFoundationShieldDisableTest::RunTest(const FString&)
{
    // An axe hit (disable_blocking_for_seconds 5) on a raised shield: blocked, then disabled for round(5 x 1 x 20) =
    // 100 ticks (the value report's "Axe disable"), the use ends with item.shield.break; raising passes until then.
    FShieldLab L;
    if (!TestTrue(TEXT("a shield in hand"), L.Shield != INDEX_NONE)) return false;
    L.Raise(5);
    TestFalse(TEXT("the axe hit is blocked"), L.Hit(4, 0, 5));
    const FFoundationEvent* Disabled = L.A.GetEvents().FindByPredicate(
        [](const FFoundationEvent& Event) { return Event.Kind == EFoundationEventKind::BlockingDisabled; });
    TestTrue(TEXT("disabled: 100 ticks, item.shield.break"),
        Disabled && Disabled->Amount == 100.f && Disabled->Interaction == TEXT("item.shield.break"));
    TestFalse(TEXT("the use ended"), L.P().Use.bActive || L.A.IsBlocking(L.Player));
    TestTrue(TEXT("the next hit lands"), L.Hit(4, 0) && L.Vitals.Health == 16.f);
    for (int32 Tick = 1; Tick < 100; ++Tick)
        L.NextStep();
    TestEqual(TEXT("step 99: a raise passes"), L.Exec(EFoundationCommandType::UseItemStart).Outcome,
        EFoundationOutcome::Pass);
    L.NextStep();
    TestTrue(TEXT("step 100: it raises again"), L.Exec(EFoundationCommandType::UseItemStart).IsCommitted());
    // A sword (disable 0) never disables.
    FShieldLab Sword;
    Sword.Raise(5);
    Sword.Hit(4, 0, 0);
    TestTrue(TEXT("no disable without the weapon's seconds"),
        Sword.Count(EFoundationEventKind::BlockingDisabled) == 0 && Sword.A.IsBlocking(Sword.Player));
    return true;
}

// Codex review 35, finding 1: a zombie's queued melee hit takes the pawn's hit step (AFoundationPawn::TakeHits) through
// HurtPlayer, so a raised shield facing it blocks it as it does an arrow: no damage, no knockback, the block sound, the
// wear. Mutation: mob hits straight to Vitals.Hurt (hurt 3, knocked back, nothing worn).
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFoundationShieldMobHitsTest, "Unnamed.Foundation.Shield.MobHits", ShieldTestFlags)

bool FFoundationShieldMobHitsTest::RunTest(const FString&)
{
    FShieldLab L;
    if (!TestTrue(TEXT("a shield in hand"), L.Shield != INDEX_NONE)) return false;
    L.Raise(5);
    FFoundationSimulation Simulation;
    L.ZombieHit(Simulation, 0);
    TestEqual(TEXT("in front: health 20"), L.Vitals.Health, 20.f);
    TestTrue(TEXT("no knockback"), Simulation.VelocityPerTick.IsNearlyZero());
    const FFoundationEvent* Blocked = L.A.GetEvents().FindByPredicate(
        [](const FFoundationEvent& Event) { return Event.Kind == EFoundationEventKind::Blocked; });
    TestTrue(TEXT("item.shield.block, 3 blocked"),
        Blocked && Blocked->Interaction == TEXT("item.shield.block") && Blocked->Amount == 3.f);
    TestEqual(TEXT("worn 1 + floor(3) = 4"), L.ShieldDamage(), 4);
    // Behind the shield's 90 degrees the hit lands and knocks back.
    L.ZombieHit(Simulation, 180);
    TestEqual(TEXT("behind: hurt 3"), L.Vitals.Health, 17.f);
    TestFalse(TEXT("behind: knocked back"), Simulation.VelocityPerTick.IsNearlyZero());
    TestEqual(TEXT("behind: no wear"), L.ShieldDamage(), 4);
    return true;
}

// Codex review 34, finding 2: the shield blocks a mob's hit after difficulty scaling (minecraft.wiki "Shield": it
// blocks the damage dealt and wears by it; "Difficulty": a mob's 3 is 2.5 on Easy, 4.5 on Hard, nothing on Peaceful).
// Mutation: the block and the wear by the unscaled 3 (4 on Hard, 4 on Easy, a block on Peaceful).
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FFoundationShieldMobHitsDifficultyTest, "Unnamed.Foundation.Shield.MobHitsDifficulty", ShieldTestFlags)

bool FFoundationShieldMobHitsDifficultyTest::RunTest(const FString&)
{
    // The first Blocked event's amount, or -1.
    const auto BlockedAmount = [](const FShieldLab& L)
    {
        const FFoundationEvent* Blocked = L.A.GetEvents().FindByPredicate(
            [](const FFoundationEvent& Event) { return Event.Kind == EFoundationEventKind::Blocked; });
        return Blocked ? Blocked->Amount : -1.f;
    };
    {
        FShieldLab L;
        if (!TestTrue(TEXT("a shield in hand"), L.Shield != INDEX_NONE)) return false;
        L.Vitals.Difficulty = EFoundationDifficulty::Hard;
        L.Raise(5);
        FFoundationSimulation Simulation;
        L.ZombieHit(Simulation, 0);
        TestEqual(TEXT("Hard: health 20"), L.Vitals.Health, 20.f);
        TestEqual(TEXT("Hard: 4.5 blocked"), BlockedAmount(L), 4.5f);
        TestEqual(TEXT("Hard: worn 1 + floor(4.5) = 5"), L.ShieldDamage(), 5);
    }
    {
        FShieldLab L;
        L.Vitals.Difficulty = EFoundationDifficulty::Easy;
        L.Raise(5);
        FFoundationSimulation Simulation;
        L.ZombieHit(Simulation, 0);
        TestEqual(TEXT("Easy: 2.5 blocked"), BlockedAmount(L), 2.5f);
        TestEqual(TEXT("Easy: 2.5 is under the threshold 3, no wear"), L.ShieldDamage(), 0);
    }
    {
        FShieldLab L;
        L.Vitals.Difficulty = EFoundationDifficulty::Peaceful;
        L.Raise(5);
        TestTrue(TEXT("Peaceful: the shield is raised"), L.A.IsBlocking(L.Player));
        FFoundationSimulation Simulation;
        L.ZombieHit(Simulation, 0);
        TestTrue(TEXT("Peaceful: no hit, nothing blocked or worn"),
            L.Vitals.Health == 20.f && L.Count(EFoundationEventKind::Blocked) == 0 && L.ShieldDamage() == 0);
    }
    return true;
}

#endif
