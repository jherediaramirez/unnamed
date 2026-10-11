#include "Misc/AutomationTest.h"
#include "Dom/JsonObject.h"
#include "Foundation/FoundationAuthority.h"
#include "Foundation/FoundationBlockRegistry.h"
#include "Foundation/FoundationInteraction.h"
#include "Foundation/FoundationInteractionSettings.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"

#if WITH_DEV_AUTOMATION_TESTS
namespace
{
    constexpr EAutomationTestFlags CombatTestFlags =
        EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter;

    // One item's melee data from Reference/minecraft-items-26.3/items.json: the attack totals (player base 1 and 4
    // plus the modifiers; none: 1 and 4), the kind, the item tags and the weapon component's shield-disable seconds.
    struct FMeleeRow
    {
        FString Id, Kind;
        double Damage = 1, Speed = 4, DisableBlocking = 0;
        TArray<FString> Tags;
    };

    TArray<FMeleeRow> ReadMeleeRows()
    {
        TArray<FMeleeRow> Rows;
        FString Text;
        TSharedPtr<FJsonObject> Root;
        const FString Path = FPaths::ProjectDir() / TEXT("Reference/minecraft-items-26.3/items.json");
        if (!FFileHelper::LoadFileToString(Text, *Path) ||
            !FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Text), Root) || !Root)
            return Rows;
        for (const TSharedPtr<FJsonValue>& Value : Root->GetArrayField(TEXT("items")))
        {
            const TSharedPtr<FJsonObject> Item = Value->AsObject();
            FMeleeRow& Row = Rows.AddDefaulted_GetRef();
            Row.Id = Item->GetStringField(TEXT("id"));
            Item->TryGetStringField(TEXT("kind"), Row.Kind);
            Item->TryGetStringArrayField(TEXT("item_tags"), Row.Tags);
            const TSharedPtr<FJsonObject>* Attack = nullptr;
            if (Item->TryGetObjectField(TEXT("attack"), Attack))
            {
                (*Attack)->TryGetNumberField(TEXT("damage"), Row.Damage);
                (*Attack)->TryGetNumberField(TEXT("speed"), Row.Speed);
            }
            const TSharedPtr<FJsonObject>* Weapon = nullptr;
            if (Item->TryGetObjectField(TEXT("weapon"), Weapon))
                (*Weapon)->TryGetNumberField(TEXT("disable_blocking_for_seconds"), Row.DisableBlocking);
        }
        return Rows;
    }

    // The wiki's melee multiplier ("Damage"): 0.2 + 0.8 s^2, s = (t + 0.5) / (20 / speed) clamped to 1.
    double ExpectedStrength(int32 Ticks, double Speed)
    {
        return FMath::Min(1., (Ticks + .5) / (20. / Speed));
    }

    double ExpectedDamage(double Damage, int32 Ticks, double Speed)
    {
        const double S = ExpectedStrength(Ticks, Speed);
        return Damage * (.2 + .8 * S * S);
    }

    // The training targets' box: 60 x 60 x 180 cm standing on z 0, centred on (X, Y).
    FBox TargetBox(double X, double Y, double Z = 0)
    {
        return FBox(FVector(X - 30, Y - 30, Z), FVector(X + 30, Y + 30, Z + 180));
    }

    // A combat lab on the project's rules: a floor at z 0, the player's feet at the origin aiming at target 1 (the
    // primary, 2 m east), more targets (ids from 2) from Others, and the interaction registered for the player.
    struct FCombatLab
    {
        FFoundationAuthority A;
        FFoundationVitals Vitals;
        FFoundationInteraction I;
        int32 Player = 0;
        uint32 Sequence = 1;
        int64 StepIndex = 0;

        explicit FCombatLab(TArray<FBox> Others = {}, bool bWear = false)
        {
            UFoundationInteractionSettings::Get().BuildRules(A.Rules);
            A.World.SetRegistry(FFoundationBlockRegistry::GetDefault());
            A.World.BoundsMin = FIntVector(-20, -20, 0);
            A.World.BoundsMax = FIntVector(19, 19, 15);
            FFoundationStatic Floor;
            Floor.Box = FBox(FVector(-2000, -2000, -100), FVector(2000, 2000, 0));
            Floor.Label = TEXT("floor");
            Floor.bAnchor = true;
            A.Statics.Add(Floor);
            Others.Insert(TargetBox(200, 0), 0);
            for (int32 Index = 0; Index < Others.Num(); ++Index)
            {
                FFoundationTrainingTarget Target;
                Target.Id = Index + 1;
                Target.Label = FString::Printf(TEXT("target %d"), Index + 1);
                Target.Box = Others[Index];
                A.Targets.Add(Target);
            }
            A.Options.bInfiniteSupply = !bWear;
            A.Options.bToolWear = bWear;
            Player = A.AddPlayer();
            A.Reset();
            Vitals.bNaturalRegeneration = false;
            I.PlayerId = Player;
            I.SetPreset(*UFoundationInteractionSettings::Get().FindPreset(TEXT("VanillaReference")), .05);
            I.Resync(A);
            Stand();
        }

        FFoundationPlayer& P() { return *A.FindPlayer(Player); }
        FFoundationTrainingTarget& T(int32 Id) { return A.Targets[Id - 1]; }

        // The player's movement facts, feet at the origin, aiming at the primary's centre.
        void Stand(bool bGrounded = true, double FallCm = 0, bool bSprinting = false, double MovedCm = 0)
        {
            FFoundationPlayerFacts Facts;
            Facts.EyeCm = FVector(0, 0, 162);
            Facts.Aim = (FVector(200, 0, 100) - Facts.EyeCm).GetSafeNormal();
            Facts.bGrounded = bGrounded;
            Facts.FallDistanceCm = FallCm;
            Facts.bSprinting = bSprinting;
            Facts.StepMovedCm = MovedCm;
            Facts.Step = ++StepIndex;
            A.SetFacts(Player, Facts);
        }

        // None holds nothing: the bare hand. The interaction's equipment sync sets the attack speed, as in play.
        void Hold(FName Item)
        {
            const int32 Index = Item.IsNone() ? INDEX_NONE : A.Rules.FindItem(Item);
            P().Inventory.SetSlot(
                0, Index == INDEX_NONE ? FFoundationItemInstance() : P().Inventory.MakeStack(Index, 1));
            P().Inventory.Select(0);
            I.SyncEquipment(A, Vitals);
        }

        // An attack command after Ticks of recovery.
        FFoundationResult Attack(int32 Ticks = 100)
        {
            I.SyncEquipment(A, Vitals);
            Vitals.AttackTicker = Ticks;
            FFoundationCommand Command;
            Command.SessionId = A.GetSessionId();
            Command.Sequence = Sequence++;
            Command.Type = EFoundationCommandType::Attack;
            Command.Hand = EFoundationHand::Main;
            Command.Item = P().Inventory.GetHand(EFoundationHand::Main).GetRef();
            Command.Target = A.Resolve(P().Facts);
            return A.Execute(Player, Command, Vitals);
        }

        // Every target back to full health, outside its damage cooldown; the held item is kept.
        void Heal()
        {
            for (FFoundationTrainingTarget& Target : A.Targets)
            {
                Target.Health.Health = Target.Health.MaxHealth;
                Target.Health.DamageCooldown = 0;
                Target.Health.LastHurt = 0;
                Target.Health.DeathTicks = -1;
            }
        }

        // The events added since Serial.
        TArray<FFoundationEvent> EventsAfter(int64 Serial) const
        {
            return A.GetEvents().FilterByPredicate(
                [Serial](const FFoundationEvent& Event) { return Event.Serial > Serial; });
        }
    };
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFoundationCombatDamageTest, "Unnamed.Foundation.Combat.Damage", CombatTestFlags)

// The damage table: every DA_Items item and the bare hand, through Attack, at every recovery tick from 0 to full
// charge, against items.json's attack totals and the wiki's multiplier; full and half charge pinned, crits at full
// charge.
bool FFoundationCombatDamageTest::RunTest(const FString&)
{
    TArray<FMeleeRow> Rows = ReadMeleeRows();
    if (!TestEqual(TEXT("63 items in items.json"), Rows.Num(), 63)) return false;
    FMeleeRow& Hand = Rows.AddDefaulted_GetRef();
    Hand.Id = TEXT("hand");
    FCombatLab L;
    int32 Attacks = 0, Mismatches = 0, FullTicksChecked = 0;
    for (const FMeleeRow& Row : Rows)
    {
        const FName Id = Row.Id == TEXT("hand") ? NAME_None : FName(*Row.Id);
        if (!Id.IsNone() && !TestTrue(Row.Id + TEXT(" is defined"), L.A.Rules.FindItem(Id) != INDEX_NONE)) continue;
        L.Hold(Id);
        TestEqual(Row.Id + TEXT(": attack speed from the item"), double(L.Vitals.AttackSpeed), Row.Speed, 1e-6);
        // The first tick at full charge: t + 0.5 reaches 20 / speed.
        const int32 Full = FMath::CeilToInt32(20. / Row.Speed - .5 - 1e-9);
        for (int32 Ticks = 0; Ticks <= Full + 1; ++Ticks)
        {
            L.Heal();
            const FFoundationResult R = L.Attack(Ticks);
            ++Attacks;
            const double Expected = ExpectedDamage(Row.Damage, Ticks, Row.Speed);
            const bool bMatch = R.IsCommitted() && FMath::IsNearlyEqual(double(R.Damage), Expected, 1e-5 * Expected) &&
                FMath::IsNearlyEqual(double(R.Strength), ExpectedStrength(Ticks, Row.Speed), 1e-6) && !R.bCritical &&
                R.bChargeReset && L.Vitals.AttackTicker == 0;
            if (!bMatch && ++Mismatches <= 10)
                AddError(FString::Printf(TEXT("%s at %d ticks: damage %.6f, expected %.6f (strength %.6f)"), *Row.Id,
                    Ticks, R.Damage, Expected, R.Strength));
        }
        // Full charge deals the item's whole damage, one tick earlier does not.
        L.Heal();
        const FFoundationResult AtFull = L.Attack(Full);
        L.Heal();
        const FFoundationResult Before = L.Attack(Full - 1);
        if (TestTrue(Row.Id + TEXT(": full charge at its tick"),
                AtFull.Strength == 1 && double(AtFull.Damage) == double(float(Row.Damage)) && Before.Strength < 1))
            ++FullTicksChecked;
        // A full-charge critical: falling, off the ground.
        L.Stand(false, 50);
        L.Heal();
        const FFoundationResult Crit = L.Attack(Full);
        TestTrue(Row.Id + TEXT(": critical x1.5"),
            Crit.bCritical && FMath::IsNearlyEqual(double(Crit.Damage), Row.Damage * 1.5, 1e-5 * Row.Damage));
        L.Stand();
    }
    TestEqual(TEXT("mismatches"), Mismatches, 0);
    TestEqual(TEXT("full-charge ticks checked"), FullTicksChecked, 64);
    AddInfo(FString::Printf(TEXT("%d attacks against the table"), Attacks));

    // Pins, from items.json's totals and the formula: the iron sword (6 at 1.6, 12.5 ticks), the netherite axe (10 at
    // 1.0, 20 ticks), the bare hand (1 at 4, 5 ticks).
    struct FPin
    {
        const TCHAR* Item;
        int32 Ticks;
        double Damage;
    };
    const FPin Pins[] = {{TEXT("iron_sword"), 0, 1.20768}, {TEXT("iron_sword"), 6, 2.49792},
        {TEXT("iron_sword"), 12, 6}, {TEXT("netherite_axe"), 9, 3.805}, {TEXT("netherite_axe"), 19, 9.605},
        {TEXT("netherite_axe"), 20, 10}, {nullptr, 2, .4}, {nullptr, 4, .848}, {nullptr, 5, 1}};
    for (const FPin& Pin : Pins)
    {
        L.Hold(Pin.Item ? FName(Pin.Item) : NAME_None);
        L.Heal();
        const FFoundationResult R = L.Attack(Pin.Ticks);
        TestTrue(
            FString::Printf(TEXT("%s at %d ticks deals %g"), Pin.Item ? Pin.Item : TEXT("hand"), Pin.Ticks, Pin.Damage),
            FMath::IsNearlyEqual(double(R.Damage), Pin.Damage, 1e-5));
    }
    // The 84.8% gate is a charge above 0.9: the bare hand at exactly 0.9 (4 ticks) lands no critical, at 5 it does
    // (the project's rule; the wiki says "84.8% or above", known issue S7-a for probe P5).
    L.Hold(NAME_None);
    L.Stand(false, 50);
    L.Heal();
    const FFoundationResult AtGate = L.Attack(4);
    L.Heal();
    const FFoundationResult PastGate = L.Attack(5);
    TestTrue(TEXT("bare hand at exactly 0.9 charge: no critical"), AtGate.Strength == .9f && !AtGate.bCritical);
    TestTrue(TEXT("bare hand at full charge: critical 1.5"), PastGate.bCritical && PastGate.Damage == 1.5f);
    L.Stand();

    // Recovery restarts on another item, not on another stack of the same item (the cooldown's switch rule).
    L.Hold(TEXT("iron_sword"));
    L.Vitals.AttackTicker = 30;
    L.Hold(TEXT("iron_sword"));
    TestEqual(TEXT("same item kept recovery"), L.Vitals.AttackTicker, 30);
    L.Hold(TEXT("iron_axe"));
    TestEqual(TEXT("another item restarted recovery"), L.Vitals.AttackTicker, 0);
    TestEqual(TEXT("the axe's attack speed"), L.Vitals.AttackSpeed, .9f);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFoundationCombatSweepTest, "Unnamed.Foundation.Combat.Sweep", CombatTestFlags)

// The sweep truth table (wiki "Sweep attack"): the item, the charge, the ground, a critical, a sprint, the movement
// limit, a landed primary hit; then the geometry (the primary's box grown 1 block across and 0.25 up and down,
// positions within 3 blocks of the player), dead and recently hurt neighbours, the damage, knockback, events and wear.
bool FFoundationCombatSweepTest::RunTest(const FString&)
{
    // 2: beside the primary (inside the grown box, 2.19 m away): swept. 3: behind it (inside the box, 3.3 m away): not.
    // 4: further aside (outside the box, 2.62 m away): not. 5: beside and raised 2 m (its bottom 5 cm inside the grown
    // box's top, 2.97 m away): swept. 6: raised 2.1 m (just above the grown box, 2.93 m away): not.
    FCombatLab L(
        {TargetBox(200, 90), TargetBox(330, 0), TargetBox(200, 170), TargetBox(200, 90, 200), TargetBox(200, 40, 210)});
    auto Sweeps = [&L](FName Item, int32 Ticks = 100) -> FFoundationResult
    {
        L.Hold(Item);
        L.Heal();
        return L.Attack(Ticks);
    };

    // The base case: an iron sword at full charge, standing still on the ground.
    const int64 Serial = L.A.GetEventSerial();
    const FFoundationResult Base = Sweeps(TEXT("iron_sword"));
    TestTrue(TEXT("base: a sweep hurting two neighbours"), Base.bSweep && Base.SweptTargets == 2 && !Base.bCritical);
    TestTrue(TEXT("the primary takes the sword's 6"), L.T(1).Health.Health == 14);
    TestTrue(TEXT("swept: 2 and 5 take 1"), L.T(2).Health.Health == 19 && L.T(5).Health.Health == 19);
    TestTrue(TEXT("not swept: 3 (beyond 3 blocks), 4 and 6 (outside the box)"),
        L.T(3).Health.Health == 20 && L.T(4).Health.Health == 20 && L.T(6).Health.Health == 20);
    const TArray<FFoundationEvent> Events = L.EventsAfter(Serial);
    const TArray<FFoundationEvent> Swept =
        Events.FilterByPredicate([](const FFoundationEvent& Event) { return Event.bSweep; });
    TestEqual(TEXT("three hit events"), Events.Num(), 3);
    TestTrue(TEXT("the primary's event first, then the swept ones with knockback"),
        Events.Num() == 3 && Events[0].EntityId == 1 && !Events[0].bSweep && Events[0].bKnockback && Swept.Num() == 2 &&
            Swept[0].Kind == EFoundationEventKind::Hit && Swept[0].Amount == 1 && Swept[0].bKnockback &&
            Swept[1].Amount == 1);
    TestTrue(TEXT("swept targets count their hits"), L.T(2).Hits == 1 && L.T(2).DamageTaken == 1);

    // By item: exactly the items tagged #swords sweep (items.json), and the bare hand does not.
    int32 Sweepers = 0;
    for (const FMeleeRow& Row : ReadMeleeRows())
    {
        const bool bSword = Row.Tags.Contains(TEXT("swords"));
        const FFoundationResult R = Sweeps(FName(*Row.Id));
        Sweepers += R.bSweep;
        TestEqual(Row.Id + TEXT(" sweeps"), R.bSweep, bSword);
    }
    TestEqual(TEXT("seven swords sweep"), Sweepers, 7);
    TestFalse(TEXT("the bare hand does not sweep"), Sweeps(NAME_None).bSweep);

    // The charge: 0.84 at 10 ticks does not, 0.92 at 11 does.
    TestFalse(TEXT("10 ticks: no sweep"), Sweeps(TEXT("diamond_sword"), 10).bSweep);
    TestTrue(TEXT("11 ticks: a sweep"), Sweeps(TEXT("diamond_sword"), 11).bSweep);

    // Off the ground: rising is neither, falling is a critical.
    L.Stand(false, 0);
    const FFoundationResult Rising = Sweeps(TEXT("iron_sword"));
    TestTrue(TEXT("rising: no sweep, no critical"), !Rising.bSweep && !Rising.bCritical && L.T(2).Health.Health == 20);
    L.Stand(false, 50);
    const FFoundationResult Falling = Sweeps(TEXT("iron_sword"));
    TestTrue(TEXT("falling: a critical, no sweep"), !Falling.bSweep && Falling.bCritical && Falling.Damage == 9);
    // Sprinting: a sprint-knockback attack instead.
    L.Stand(true, 0, true);
    const FFoundationResult Sprint = Sweeps(TEXT("iron_sword"));
    TestTrue(TEXT("sprinting: sprint knockback, no sweep"), !Sprint.bSweep && Sprint.bSprintKnockback);

    // Movement over the last step: at most movement_speed's 0.1 blocks (10 cm); walking (21.6) never sweeps.
    const double Moves[] = {0, 6.5, 10, 10.01, 21.6};
    const bool Expected[] = {true, true, true, false, false};
    for (int32 Index = 0; Index < UE_ARRAY_COUNT(Moves); ++Index)
    {
        L.Stand(true, 0, false, Moves[Index]);
        TestEqual(FString::Printf(TEXT("moved %.2f cm: sweep"), Moves[Index]), Sweeps(TEXT("iron_sword")).bSweep,
            Expected[Index]);
    }
    L.Stand();

    // Only a landed primary hit sweeps: the primary absorbing it (a bigger hit just taken) sweeps nothing.
    L.Hold(TEXT("iron_sword"));
    L.Heal();
    L.T(1).Health.DamageCooldown = 20;
    L.T(1).Health.LastHurt = 100;
    const FFoundationResult Absorbed = L.Attack();
    TestTrue(TEXT("absorbed primary: no sweep"),
        Absorbed.Reason == EFoundationReason::TargetCooldown && !Absorbed.bSweep && L.T(2).Health.Health == 20);

    // Neighbours: a dead one is skipped; one inside its hurt window with a bigger last hit takes nothing; one whose
    // last hit was smaller takes the difference, without knockback; one at 1 health goes down.
    L.Heal();
    L.T(2).Health.Hurt(EFoundationDamage::Kill, 100);
    TestTrue(TEXT("a dead neighbour is skipped"), L.Attack().SweptTargets == 1 && L.T(5).Health.Health == 19);
    L.Heal();
    L.T(2).Health.DamageCooldown = 20;
    L.T(2).Health.LastHurt = 5;
    TestTrue(TEXT("a neighbour in its hurt window takes nothing"),
        L.Attack().SweptTargets == 1 && L.T(2).Health.Health == 20);
    L.Heal();
    L.T(2).Health.DamageCooldown = 20;
    L.T(2).Health.LastHurt = .5f;
    const int64 WindowSerial = L.A.GetEventSerial();
    TestTrue(TEXT("a smaller last hit: the difference lands"),
        L.Attack().SweptTargets == 2 && L.T(2).Health.Health == 19.5f);
    const TArray<FFoundationEvent> Window =
        L.EventsAfter(WindowSerial)
            .FilterByPredicate([](const FFoundationEvent& Event) { return Event.bSweep && Event.EntityId == 2; });
    TestTrue(TEXT("...without knockback"), Window.Num() == 1 && Window[0].Amount == .5f && !Window[0].bKnockback);
    L.Heal();
    L.T(2).Health.Health = 1;
    const int64 DownSerial = L.A.GetEventSerial();
    L.Attack();
    TestTrue(TEXT("a swept neighbour at 1 health goes down"),
        L.T(2).Health.IsDead() &&
            L.EventsAfter(DownSerial)
                .ContainsByPredicate([](const FFoundationEvent& Event)
                    { return Event.Kind == EFoundationEventKind::TargetDown && Event.EntityId == 2; }));

    // Wear: one point per attack, however many it sweeps (the wiki is silent; known issue S7-c).
    FCombatLab W({TargetBox(200, 90)}, true);
    W.Hold(TEXT("iron_sword"));
    const FFoundationResult Worn = W.Attack();
    TestTrue(TEXT("a sweeping sword wears once"),
        Worn.bSweep && Worn.SweptTargets == 1 && Worn.bToolWorn &&
            W.P().Inventory.GetHand(EFoundationHand::Main).Damage == 1);
    // The ratio is Sweeping Edge's extension point, on the cooldown-scaled base damage: at 0.5, full charge gives
    // 1 + 0.5 x 6; 11 ticks (0.92, multiplier 0.87712) gives 1 + 0.5 x 5.26272.
    W.A.Rules.SweepingDamageRatio = .5f;
    W.Heal();
    W.Attack();
    TestEqual(TEXT("the sweeping ratio at full charge"), W.T(2).Health.Health, 16.f);
    W.Heal();
    const FFoundationResult Partial = W.Attack(11);
    TestTrue(TEXT("the sweeping ratio at 0.92"),
        Partial.bSweep && FMath::IsNearlyEqual(double(W.T(2).Health.Health), 20 - 3.63136, 1e-5));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFoundationCombatKnockbackTest, "Unnamed.Foundation.Combat.Knockback", CombatTestFlags)

// Knockback as data (wiki "Knockback"): which hits knock back, the sprint-knockback attack, and the attacker's sprint
// ending through the interaction. The targets are anchored; their motion waits for a movable entity (known issue S7-b).
bool FFoundationCombatKnockbackTest::RunTest(const FString&)
{
    FCombatLab L;
    L.Hold(TEXT("iron_sword"));
    const FFoundationResult Fresh = L.Attack();
    TestTrue(TEXT("a fresh landed hit knocks back"), Fresh.bKnockback && !Fresh.bSprintKnockback);
    // Inside the hurt window a bigger hit lands its difference without knockback; a smaller one is absorbed.
    L.Heal();
    L.T(1).Health.DamageCooldown = 20;
    L.T(1).Health.LastHurt = 1;
    const FFoundationResult Window = L.Attack();
    TestTrue(TEXT("a landed hit in the hurt window: no knockback"),
        Window.DamageApplied == 5 && !Window.bKnockback && Window.Reason == EFoundationReason::None);
    L.Heal();
    L.T(1).Health.DamageCooldown = 20;
    L.T(1).Health.LastHurt = 100;
    const FFoundationResult Absorbed = L.Attack();
    TestTrue(TEXT("an absorbed hit: no knockback"),
        Absorbed.Reason == EFoundationReason::TargetCooldown && !Absorbed.bKnockback);

    // Sprinting at full charge: a sprint-knockback attack, never a critical, even falling.
    L.Stand(false, 50, true);
    L.Heal();
    const int64 Serial = L.A.GetEventSerial();
    const FFoundationResult Sprint = L.Attack();
    TestTrue(TEXT("sprinting and falling: sprint knockback, no critical"),
        Sprint.bSprintKnockback && Sprint.bKnockback && !Sprint.bCritical && Sprint.Damage == 6);
    const TArray<FFoundationEvent> Events = L.EventsAfter(Serial);
    TestTrue(TEXT("its hit event says so"), Events.Num() == 1 && Events[0].bSprintKnockback && Events[0].bKnockback);
    L.Stand(true, 0, true);
    L.Heal();
    TestFalse(TEXT("sprinting below the gate: a plain hit"), L.Attack(10).bSprintKnockback);
    // In the hurt window the sprint's extra knockback still lands with the hit; absorbed, nothing does.
    L.Heal();
    L.T(1).Health.DamageCooldown = 20;
    L.T(1).Health.LastHurt = 1;
    const FFoundationResult SprintWindow = L.Attack();
    TestTrue(TEXT("sprint knockback in the hurt window"), SprintWindow.bSprintKnockback && !SprintWindow.bKnockback);
    L.Heal();
    L.T(1).Health.DamageCooldown = 20;
    L.T(1).Health.LastHurt = 100;
    TestFalse(TEXT("absorbed: no sprint knockback"), L.Attack().bSprintKnockback);

    // Through the interaction: a landed sprint-knockback attack raises the stop-sprint flag; a plain one does not; a
    // resync clears it.
    auto Press = [&L](int32 Ticks)
    {
        L.I.NextSequence = L.Sequence;
        L.Vitals.AttackTicker = Ticks;
        L.Heal();
        L.I.Release(EFoundationInput::Attack);
        L.I.Press(EFoundationInput::Attack, 1);
        L.I.BeginUpdate();
        L.A.Step();
        L.I.StepOnce(L.A, L.Vitals, true, 0);
        L.Sequence = L.I.NextSequence;
        return L.I.LastResult;
    };
    const FFoundationResult Plain = Press(10);
    TestTrue(TEXT("interaction: a weak sprinting hit keeps the sprint"),
        Plain.Type == EFoundationCommandType::Attack && Plain.IsCommitted() && !L.I.bStopSprint);
    const FFoundationResult Stopped = Press(100);
    TestTrue(TEXT("interaction: a sprint-knockback hit stops the sprint"), Stopped.bSprintKnockback && L.I.bStopSprint);
    L.I.Resync(L.A);
    TestFalse(TEXT("a resync clears it"), L.I.bStopSprint);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FFoundationCombatShieldDisableTest, "Unnamed.Foundation.Combat.ShieldDisable", CombatTestFlags)

// The axe's shield-disable as data: weapon.disable_blocking_for_seconds reaches every definition (the 7 axes 5 s, every
// other item and the hand 0). Nothing blocks yet, so no hit applies it.
bool FFoundationCombatShieldDisableTest::RunTest(const FString&)
{
    FFoundationRules Rules;
    UFoundationInteractionSettings::Get().BuildRules(Rules);
    const TArray<FMeleeRow> Rows = ReadMeleeRows();
    if (!TestEqual(TEXT("63 items in items.json"), Rows.Num(), 63)) return false;
    int32 Disablers = 0;
    for (const FMeleeRow& Row : Rows)
    {
        const int32 Index = Rules.FindItem(FName(*Row.Id));
        if (!TestTrue(Row.Id + TEXT(" is defined"), Index != INDEX_NONE)) continue;
        const float Seconds = Rules.Items[Index].DisableBlockingSeconds;
        TestEqual(Row.Id + TEXT(": disable blocking"), double(Seconds), Row.DisableBlocking);
        if (Seconds > 0)
        {
            ++Disablers;
            TestTrue(Row.Id + TEXT(" is an axe at 5 s"), Row.Kind == TEXT("axe") && Seconds == 5);
        }
    }
    TestEqual(TEXT("seven items disable shields"), Disablers, 7);
    TestEqual(TEXT("the hand"), Rules.Hand.DisableBlockingSeconds, 0.f);
    const int32 Stone = Rules.FindItem(TEXT("stone"));
    TestTrue(TEXT("a block item"), Stone != INDEX_NONE && Rules.Items[Stone].DisableBlockingSeconds == 0);
    return true;
}
#endif
