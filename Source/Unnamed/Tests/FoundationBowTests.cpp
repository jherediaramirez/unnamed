#include "Misc/AutomationTest.h"
#include "Engine/World.h"
#include "Foundation/FoundationArrowView.h"
#include "Foundation/FoundationArrows.h"
#include "Foundation/FoundationAuthority.h"
#include "Foundation/FoundationBlockRegistry.h"
#include "Foundation/FoundationInteraction.h"
#include "Foundation/FoundationInteractionSettings.h"
#include "Foundation/FoundationInteractionView.h"
#include "Foundation/FoundationMobAssets.h"
#include "GameFramework/Actor.h"
#include "HAL/IConsoleManager.h"
#include "UnnamedProjectSettings.h"

#if WITH_DEV_AUTOMATION_TESTS
// Survival step 19c: the bow and arrows. Values are literals from bow.draw_curve, bow.flight, arrows.flight,
// survival2.r18.arrow_rule and survival2.r18.fov_rule (the 26.3 reports and minecraft.wiki "Bow" / "Arrow"), never the
// constants under test.
namespace
{
    constexpr EAutomationTestFlags BowTestFlags =
        EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter;

    struct FBowTestSwitchScope
    {
        IConsoleVariable& Variable;
        int32 Saved;

        explicit FBowTestSwitchScope(IConsoleVariable& InVariable) : Variable(InVariable), Saved(Variable.GetInt()) {}
        ~FBowTestSwitchScope() { Set(Saved); }
        void Set(int32 Value) const { Variable.Set(Value, ECVF_SetByCode); }
    };

    // The project's rules, lab tool wear and item use on, finite supply, a floor, a bow in hotbar slot 1 (selected),
    // the player at the origin looking along +X. World origin is zero, so world cm equal local cm.
    struct FBowLab
    {
        FFoundationAuthority A;
        FFoundationVitals Vitals;
        FFoundationInteraction I;
        int32 Player = 0, BowItem = INDEX_NONE, ArrowItem = INDEX_NONE, ShieldItem = INDEX_NONE;
        int64 StepIndex = 0;
        EFoundationPlayerMode FactsMode = EFoundationPlayerMode::Survival;

        FBowLab(bool bMobs = false)
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
            A.Options.bToolWear = true;
            A.Options.bItemUse = true;
            A.Options.bInfiniteSupply = false;
            A.Options.bMobs = bMobs;
            Player = A.AddPlayer();
            A.Reset();
            P().Inventory.bInfiniteSupply = false;
            Vitals.bNaturalRegeneration = false;
            I.PlayerId = Player;
            I.Resync(A);
            NextStep();
            BowItem = A.Rules.FindItem(TEXT("bow"));
            ArrowItem = A.Rules.FindItem(TEXT("arrow"));
            ShieldItem = A.Rules.FindItem(TEXT("shield"));
            Hold(BowItem);
        }

        FFoundationPlayer& P() { return *A.FindPlayer(Player); }

        void Hold(int32 Item)
        {
            if (Item != INDEX_NONE) P().Inventory.SetSlot(0, P().Inventory.MakeStack(Item, 1));
            P().Inventory.Select(0);
        }

        void GiveArrows(int32 Count) { P().Inventory.SetSlot(1, P().Inventory.MakeStack(ArrowItem, Count)); }

        int32 CountArrows()
        {
            int32 Total = 0;
            for (int32 Slot = 0; Slot <= FFoundationInventory::OffhandSlot; ++Slot)
            {
                if (Slot >= FFoundationInventory::FirstArmor && Slot != FFoundationInventory::OffhandSlot) continue;
                const FFoundationItemInstance* Stack = P().Inventory.GetSlot(Slot);
                if (Stack && !Stack->IsEmpty() && Stack->Item == ArrowItem) Total += Stack->Count;
            }
            return Total;
        }

        void AddWall(const FBox& Box)
        {
            FFoundationStatic Wall;
            Wall.Box = Box;
            Wall.Label = TEXT("wall");
            Wall.bAnchor = true;
            A.Statics.Add(Wall);
        }

        void NextStep()
        {
            FFoundationPlayerFacts Facts;
            Facts.FeetCm = FVector::ZeroVector;
            Facts.EyeCm = FVector(0, 0, 162);
            Facts.Aim = FVector::ForwardVector;
            Facts.Mode = FactsMode;
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

        // Start a draw, then Ticks continues, one per reference step.
        FFoundationResult Draw(int32 Ticks)
        {
            FFoundationResult Result = Exec(EFoundationCommandType::UseItemStart);
            for (int32 Index = 0; Index < Ticks; ++Index)
            {
                NextStep();
                Exec(EFoundationCommandType::UseItemContinue);
            }
            return Result;
        }

        int32 CountSounds(const TCHAR* Sound) const
        {
            int32 Found = 0;
            for (const FFoundationEvent& Event : A.GetEvents())
                Found += Event.Kind == EFoundationEventKind::ArrowSound && Event.Interaction == Sound;
            return Found;
        }

        // A plain shot (no spread) from a world point.
        int32 Shoot(const FVector& From, const FVector& Direction, float Speed = 3.f, int32 Owner = INDEX_NONE,
            EFoundationArrowPickup Pickup = EFoundationArrowPickup::Allowed, bool bMobOwner = false,
            const TCHAR* Attacker = TEXT(""))
        {
            FFoundationArrowShot Shot;
            Shot.FromCm = From;
            Shot.Direction = Direction;
            Shot.SpeedBlocks = Speed;
            Shot.Inaccuracy = 0.f;
            Shot.OwnerId = Owner;
            Shot.bOwnerIsMob = bMobOwner;
            Shot.Pickup = Pickup;
            Shot.Attacker = Attacker;
            return A.ShootArrow(Shot);
        }
    };
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFoundationBowPowerTest, "Unnamed.Foundation.Bow.Power", BowTestFlags)

bool FFoundationBowPowerTest::RunTest(const FString&)
{
    // bow.draw_curve min((f^2 + 2f) / 3, 1), f = t / 20; catches a wrong curve (f^2 only, or no cap at 20 ticks).
    TestEqual(TEXT("power t0"), FoundationBow::GetPower(0), 0.f, 1e-6f);
    TestEqual(TEXT("power t5"), FoundationBow::GetPower(5), 0.1875f, 1e-6f);
    TestEqual(TEXT("power t10"), FoundationBow::GetPower(10), 0.4166667f, 1e-6f);
    TestEqual(TEXT("power t20"), FoundationBow::GetPower(20), 1.f, 1e-6f);
    TestEqual(TEXT("power t40"), FoundationBow::GetPower(40), 1.f, 1e-6f);
    // survival2.r18.fov_rule 1 - 0.15 x min(t / 20, 1)^2; catches a missing cap or a wrong coefficient.
    TestEqual(TEXT("fov t5"), FoundationBow::GetFovTarget(5), .990625f, 1e-6f);
    TestEqual(TEXT("fov t10"), FoundationBow::GetFovTarget(10), .9625f, 1e-6f);
    TestEqual(TEXT("fov t20"), FoundationBow::GetFovTarget(20), .85f, 1e-6f);
    TestEqual(TEXT("fov t30"), FoundationBow::GetFovTarget(30), .85f, 1e-6f);
    // The R18 table's derived smoothing from these targets, m += (target - m) x 0.5 from 1.0 (t1 .9998125, t10
    // .9688761): it pins the targets against the table; the camera's own halving is the simulation's.
    float M = 1.f;
    for (int32 Tick = 1; Tick <= 10; ++Tick)
    {
        M += (FoundationBow::GetFovTarget(Tick) - M) * .5f;
        if (Tick == 1) TestEqual(TEXT("fov smoothing t1"), M, .9998125f, 1e-6f);
    }
    TestEqual(TEXT("fov smoothing t10"), M, .9688761f, 1e-6f);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFoundationArrowFlightTest, "Unnamed.Foundation.Arrows.Flight", BowTestFlags)

bool FFoundationArrowFlightTest::RunTest(const FString&)
{
    // arrows.flight: drag 0.99 then gravity 0.05 blocks (5 cm); catches gravity before drag (-4.95 on the second step).
    FVector V(100, 0, 0);
    FoundationArrows::Integrate(V, false);
    TestTrue(TEXT("air step 1: (99, 0, -5)"), V.Equals(FVector(99, 0, -5), 1e-4));
    FoundationArrows::Integrate(V, false);
    TestTrue(TEXT("air step 2: (98.01, 0, -9.95)"), V.Equals(FVector(98.01, 0, -9.95), 1e-4));
    // Water drag 0.6; catches a swapped drag.
    FVector W(100, 0, 0);
    FoundationArrows::Integrate(W, true);
    TestTrue(TEXT("water step: (60, 0, -5)"), W.Equals(FVector(60, 0, -5), 1e-4));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFoundationArrowDamageTest, "Unnamed.Foundation.Arrows.Damage", BowTestFlags)

bool FFoundationArrowDamageTest::RunTest(const FString&)
{
    // survival2.r18.arrow_rule ceil(speed x 2); catches a floor or a wrong base damage.
    FRandomStream Random(1);
    const float Speeds[] = {1.f, 1.5f, 2.f, 3.f, 1.2f, .1f};
    const int32 Expected[] = {2, 3, 4, 6, 3, 1};
    for (int32 Index = 0; Index < 6; ++Index)
        TestEqual(FString::Printf(TEXT("damage at speed %g"), Speeds[Index]),
            FoundationArrows::GetDamage(Speeds[Index], false, Random), Expected[Index]);
    // A crit adds uniform 0..floor(6 / 2) + 1 = 0..4 to 6, so 6..10 with every value seen; catches a bound without + 1.
    FRandomStream Crits(20260810);
    int32 Min = MAX_int32, Max = MIN_int32;
    TSet<int32> Seen;
    for (int32 Draw = 0; Draw < 2000; ++Draw)
    {
        const int32 Damage = FoundationArrows::GetDamage(3.f, true, Crits);
        Min = FMath::Min(Min, Damage);
        Max = FMath::Max(Max, Damage);
        Seen.Add(Damage);
    }
    TestEqual(TEXT("crit min"), Min, 6);
    TestEqual(TEXT("crit max"), Max, 10);
    for (int32 Value = 6; Value <= 10; ++Value)
        TestTrue(FString::Printf(TEXT("crit %d seen"), Value), Seen.Contains(Value));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFoundationArrowSweepTest, "Unnamed.Foundation.Arrows.Sweep", BowTestFlags)

bool FFoundationArrowSweepTest::RunTest(const FString&)
{
    // Slab sweep of From..To: catches a wrong entry time, a false hit and a missed start-inside case.
    const FBox Box(FVector(100, -10, -10), FVector(200, 10, 10));
    double Time = -1;
    int32 Index = -1;
    TestTrue(TEXT("through the box hits"),
        FoundationArrows::Sweep(FVector(0), FVector(300, 0, 0), MakeArrayView(&Box, 1), Time, &Index));
    TestEqual(TEXT("enters at 100 / 300"), Time, 100. / 300., 1e-9);
    TestEqual(TEXT("index 0"), Index, 0);
    double MissTime = .5;
    TestFalse(TEXT("passing beside it misses"),
        FoundationArrows::Sweep(FVector(0, 50, 0), FVector(300, 50, 0), MakeArrayView(&Box, 1), MissTime));
    TestFalse(TEXT("ending short misses"),
        FoundationArrows::Sweep(FVector(0), FVector(50, 0, 0), MakeArrayView(&Box, 1), MissTime));
    double InsideTime = .5;
    TestTrue(TEXT("starting inside hits"),
        FoundationArrows::Sweep(FVector(150, 0, 0), FVector(400, 0, 0), MakeArrayView(&Box, 1), InsideTime));
    TestEqual(TEXT("starting inside: time 0"), InsideTime, 0., 1e-9);
    const FBox Boxes[] = {FBox(FVector(300, -10, -10), FVector(350, 10, 10)), Box};
    double NearTime = 5;
    int32 NearIndex = -1;
    TestTrue(TEXT("two boxes hit"),
        FoundationArrows::Sweep(FVector(0), FVector(400, 0, 0), MakeArrayView(Boxes, 2), NearTime, &NearIndex));
    TestEqual(TEXT("the nearer box (index 1)"), NearIndex, 1);
    TestEqual(TEXT("the nearer box's time"), NearTime, .25, 1e-9);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFoundationBowReleaseTest, "Unnamed.Foundation.Bow.Release", BowTestFlags)

bool FFoundationBowReleaseTest::RunTest(const FString&)
{
    // bow.flight: a full draw (20 ticks) shoots power 1 x 3 blocks/tick = 300 cm, critical, one arrow and one
    // durability; catches a wrong speed, a missing crit or an arrow not consumed.
    FBowLab L;
    if (!TestTrue(TEXT("bow and arrow items exist"), L.BowItem != INDEX_NONE && L.ArrowItem != INDEX_NONE))
        return false;
    L.GiveArrows(2);
    TestTrue(TEXT("the draw starts"), L.Draw(20).IsCommitted());
    L.Exec(EFoundationCommandType::UseItemRelease);
    if (!TestEqual(TEXT("one arrow in flight"), L.A.GetArrows().Num(), 1)) return false;
    const FFoundationArrow& Arrow = L.A.GetArrows()[0];
    TestTrue(TEXT("speed 300 cm/step +-10"), Arrow.VelocityCm.Size() >= 290. && Arrow.VelocityCm.Size() <= 310.);
    TestTrue(TEXT("critical at full power"), Arrow.bCrit);
    TestTrue(TEXT("pickup Allowed"), Arrow.Pickup == EFoundationArrowPickup::Allowed);
    TestEqual(TEXT("one arrow left"), L.CountArrows(), 1);
    TestEqual(TEXT("bow damage 1"), L.P().Inventory.Hotbar[0].Damage, 1);
    TestEqual(TEXT("one entity.arrow.shoot"), L.CountSounds(TEXT("entity.arrow.shoot")), 1);
    // From the eye less 0.1 block, along the look (spread 1 bends it slightly); the shoot sound at volume 1, pitch
    // 1 / (U x 0.4 + 1.2) + 0.5 in [1.125, 1.3333] (survival.r5.rules).
    TestTrue(TEXT("leaves the eye less 10 cm"), (L.A.World.OriginCm + Arrow.LocalCm).Equals(FVector(0, 0, 152), .01));
    TestTrue(TEXT("along the look"), (Arrow.VelocityCm.GetSafeNormal() | FVector::ForwardVector) > .999);
    for (const FFoundationEvent& Event : L.A.GetEvents())
        if (Event.Kind == EFoundationEventKind::ArrowSound)
            TestTrue(TEXT("shoot volume 1, pitch in range"),
                Event.Amount == 1.f && Event.Pitch >= 1.125f - 1e-4f && Event.Pitch <= 4.f / 3.f + 1e-4f);

    // A 1-tick draw has power .034167 < 0.1: nothing shoots and nothing is spent.
    FBowLab Short;
    Short.GiveArrows(2);
    Short.Draw(1);
    Short.Exec(EFoundationCommandType::UseItemRelease);
    TestEqual(TEXT("short draw: no arrow"), Short.A.GetArrows().Num(), 0);
    TestEqual(TEXT("short draw: arrows unchanged"), Short.CountArrows(), 2);
    TestEqual(TEXT("short draw: bow unworn"), Short.P().Inventory.Hotbar[0].Damage, 0);
    // The 0.1 edge: 2 ticks (power .07) shoot nothing, 3 ticks (.1075) shoot.
    for (const int32 Ticks : {2, 3})
    {
        FBowLab Edge;
        Edge.GiveArrows(2);
        Edge.Draw(Ticks);
        Edge.Exec(EFoundationCommandType::UseItemRelease);
        TestEqual(FString::Printf(TEXT("a %d-tick draw"), Ticks), Edge.A.GetArrows().Num(), Ticks == 3 ? 1 : 0);
    }
    // Vanilla's ammo order: the offhand before the inventory's slots.
    FBowLab Order;
    Order.GiveArrows(5);
    Order.P().Inventory.SetSlot(FFoundationInventory::OffhandSlot, Order.P().Inventory.MakeStack(Order.ArrowItem, 3));
    Order.Draw(20);
    Order.Exec(EFoundationCommandType::UseItemRelease);
    TestEqual(TEXT("the offhand's arrow is spent first"), Order.P().Inventory.GetHand(EFoundationHand::Off).Count, 2);
    TestEqual(TEXT("slot 1 untouched"), Order.P().Inventory.GetSlot(1)->Count, 5);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFoundationBowSwitchMidDrawTest, "Unnamed.Foundation.Bow.SwitchMidDraw", BowTestFlags)

bool FFoundationBowSwitchMidDrawTest::RunTest(const FString&)
{
    // R40-4: the public foundation.Items.Bow contract disables drawing and shooting. A held use must stop drawing
    // on its next command when the switch goes off, without spending ammunition or durability.
    IConsoleVariable* Variable = IConsoleManager::Get().FindConsoleVariable(TEXT("foundation.Items.Bow"));
    if (!TestNotNull(TEXT("the bow switch exists"), Variable)) return false;
    FBowTestSwitchScope BowSwitch(*Variable);
    BowSwitch.Set(1);
    if (!TestTrue(TEXT("the bow switch was enabled"), FoundationBow::IsOn())) return false;
    FBowLab Disabled;
    if (!TestTrue(
            TEXT("bow and arrow items exist"), Disabled.BowItem != INDEX_NONE && Disabled.ArrowItem != INDEX_NONE))
        return false;
    Disabled.GiveArrows(2);
    const FFoundationResult Started = Disabled.Draw(10);
    if (!TestTrue(TEXT("the bow is actively drawn before disabling it"),
            Started.IsCommitted() && Disabled.P().Use.bActive && Disabled.P().Use.bDraws &&
                Disabled.P().Use.Ticks == 10))
        return false;
    BowSwitch.Set(0);
    if (!TestFalse(TEXT("the bow switch was disabled during the draw"), FoundationBow::IsOn())) return false;
    Disabled.NextStep();
    const FFoundationResult Continued = Disabled.Exec(EFoundationCommandType::UseItemContinue);
    TestTrue(TEXT("Bow 0 cancels the held draw"), Continued.Outcome == EFoundationOutcome::Canceled);
    TestFalse(TEXT("Bow 0 clears active use"), Disabled.P().Use.bActive);
    TestFalse(TEXT("Bow 0 clears the drawing flag"), Disabled.P().Use.bDraws);
    TestEqual(TEXT("canceling spends no arrows"), Disabled.CountArrows(), 2);
    TestEqual(TEXT("canceling does not wear the bow"), Disabled.P().Inventory.Hotbar[0].Damage, 0);
    Disabled.Exec(EFoundationCommandType::UseItemRelease);
    TestEqual(TEXT("releasing the canceled draw spawns no arrow"), Disabled.A.GetArrows().Num(), 0);
    TestEqual(TEXT("releasing the canceled draw spends no arrows"), Disabled.CountArrows(), 2);
    TestEqual(TEXT("releasing the canceled draw does not wear the bow"), Disabled.P().Inventory.Hotbar[0].Damage, 0);

    // Leaving the bow enabled preserves the held draw and its normal release.
    BowSwitch.Set(1);
    if (!TestTrue(TEXT("the bow switch was enabled for the control"), FoundationBow::IsOn())) return false;
    FBowLab Enabled;
    Enabled.GiveArrows(2);
    Enabled.Draw(10);
    Enabled.NextStep();
    const FFoundationResult Kept = Enabled.Exec(EFoundationCommandType::UseItemContinue);
    TestTrue(TEXT("Bow 1 keeps drawing on the next command"),
        Kept.IsCommitted() && Enabled.P().Use.bActive && Enabled.P().Use.bDraws && Enabled.P().Use.Ticks == 11);
    Enabled.Exec(EFoundationCommandType::UseItemRelease);
    TestEqual(TEXT("Bow 1 release spawns one arrow"), Enabled.A.GetArrows().Num(), 1);
    TestEqual(TEXT("Bow 1 release spends one arrow"), Enabled.CountArrows(), 1);
    TestEqual(TEXT("Bow 1 release wears the bow once"), Enabled.P().Inventory.Hotbar[0].Damage, 1);

    // The bow switch must not cancel another held item use: the existing fixture also supports a raised shield.
    FBowLab Shield;
    if (!TestTrue(TEXT("the shield item exists"), Shield.ShieldItem != INDEX_NONE)) return false;
    Shield.Hold(Shield.ShieldItem);
    if (!TestTrue(TEXT("the shield is raised before disabling bows"),
            Shield.Draw(6).IsCommitted() && Shield.A.IsBlocking(Shield.Player)))
        return false;
    BowSwitch.Set(0);
    if (!TestFalse(TEXT("the bow switch was disabled during shield use"), FoundationBow::IsOn())) return false;
    Shield.NextStep();
    const FFoundationResult Blocking = Shield.Exec(EFoundationCommandType::UseItemContinue);
    TestTrue(TEXT("Bow 0 preserves a held shield"),
        Blocking.IsCommitted() && Shield.P().Use.bActive && Shield.P().Use.bBlocks && !Shield.P().Use.bDraws &&
            Shield.A.IsBlocking(Shield.Player));
    TestEqual(TEXT("the shield's use keeps counting"), Shield.P().Use.Ticks, 7);
    TestEqual(TEXT("the shield remains unworn"), Shield.P().Inventory.Hotbar[0].Damage, 0);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFoundationBowNoAmmoTest, "Unnamed.Foundation.Bow.NoAmmo", BowTestFlags)

bool FFoundationBowNoAmmoTest::RunTest(const FString&)
{
    // UseItemStart: a survival bow without arrows passes (FindArrowSlot); catches a draw that ignores ammo.
    FBowLab L;
    if (!TestTrue(TEXT("bow item exists"), L.BowItem != INDEX_NONE)) return false;
    const FFoundationResult Start = L.Exec(EFoundationCommandType::UseItemStart);
    TestFalse(TEXT("survival, no arrows: not committed"), Start.IsCommitted());
    TestEqual(TEXT("survival, no arrows: passes"), Start.Outcome, EFoundationOutcome::Pass);

    // Creative needs no arrow: it draws, shoots a CreativeOnly arrow and spends nothing.
    FBowLab C;
    C.Vitals.Mode = C.FactsMode = EFoundationPlayerMode::Creative;
    TestTrue(TEXT("creative, no arrows: draws"), C.Draw(20).IsCommitted());
    C.Exec(EFoundationCommandType::UseItemRelease);
    if (!TestEqual(TEXT("creative: one arrow"), C.A.GetArrows().Num(), 1)) return false;
    TestTrue(TEXT("creative: CreativeOnly pickup"), C.A.GetArrows()[0].Pickup == EFoundationArrowPickup::CreativeOnly);
    TestEqual(TEXT("creative: no arrows in the inventory"), C.CountArrows(), 0);
    TestEqual(TEXT("creative: bow unworn"), C.P().Inventory.Hotbar[0].Damage, 0);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFoundationArrowStickTest, "Unnamed.Foundation.Arrows.Stick", BowTestFlags)

bool FFoundationArrowStickTest::RunTest(const FString&)
{
    // survival2.r18.arrow_rule: stuck arrows despawn 1,200 ticks after sticking; catches a wrong DespawnTicks or a
    // missing stick.
    FBowLab L;
    L.AddWall(FBox(FVector(600, -500, 0), FVector(700, 500, 500)));
    L.Shoot(FVector(200, 0, 150), FVector::ForwardVector);
    for (int32 Step = 0; Step < 10 && L.A.GetArrows().Num() == 1 && !L.A.GetArrows()[0].bInGround; ++Step)
        L.NextStep();
    if (!TestEqual(TEXT("one arrow"), L.A.GetArrows().Num(), 1)) return false;
    const FFoundationArrow& Arrow = L.A.GetArrows()[0];
    if (!TestTrue(TEXT("stuck in the wall"), Arrow.bInGround)) return false;
    TestTrue(TEXT("zero velocity"), Arrow.VelocityCm.IsZero());
    TestTrue(TEXT("tip at the wall face"), FMath::Abs(Arrow.LocalCm.X - 600.) < 1.);
    TestEqual(TEXT("one entity.arrow.hit"), L.CountSounds(TEXT("entity.arrow.hit")), 1);
    for (int32 Step = 0; Step < 1199; ++Step)
        L.NextStep();
    if (!TestEqual(TEXT("still present at 1199"), L.A.GetArrows().Num(), 1)) return false;
    TestEqual(TEXT("InGroundTicks 1199"), L.A.GetArrows()[0].InGroundTicks, 1199);
    L.NextStep();
    TestEqual(TEXT("gone after the 1200th step"), L.A.GetArrows().Num(), 0);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFoundationArrowPickupTest, "Unnamed.Foundation.Arrows.Pickup", BowTestFlags)

bool FFoundationArrowPickupTest::RunTest(const FString&)
{
    // survival2.r18.arrow_rule: Allowed gives an arrow in any mode, CreativeOnly takes it in creative only (no item),
    // Disallowed never; catches a swapped mode check or an item from a CreativeOnly arrow.
    struct FCase
    {
        EFoundationArrowPickup Pickup;
        EFoundationPlayerMode Mode;
        bool bTaken;
        int32 ArrowsAfter;
    };
    const FCase Cases[] = {
        {EFoundationArrowPickup::Allowed, EFoundationPlayerMode::Survival, true, 1},
        {EFoundationArrowPickup::Allowed, EFoundationPlayerMode::Creative, true, 1},
        {EFoundationArrowPickup::CreativeOnly, EFoundationPlayerMode::Survival, false, 0},
        {EFoundationArrowPickup::CreativeOnly, EFoundationPlayerMode::Creative, true, 0},
        {EFoundationArrowPickup::Disallowed, EFoundationPlayerMode::Survival, false, 0},
        {EFoundationArrowPickup::Disallowed, EFoundationPlayerMode::Creative, false, 0},
    };
    for (int32 Index = 0; Index < 6; ++Index)
    {
        const FCase& Case = Cases[Index];
        const FString Label = FString::Printf(TEXT("case %d"), Index);
        FBowLab L;
        L.FactsMode = Case.Mode;
        // The player's own arrow from inside its body sticks in a wall just ahead of it.
        L.AddWall(FBox(FVector(60, -200, 0), FVector(160, 200, 300)));
        L.Shoot(FVector(40, 0, 100), FVector::ForwardVector, 3.f, L.Player, Case.Pickup);
        L.NextStep();
        if (!TestEqual(*(Label + TEXT(": one arrow stuck")), L.A.GetArrows().Num(), 1)) continue;
        TestTrue(*(Label + TEXT(": in the ground")), L.A.GetArrows()[0].bInGround);
        L.NextStep();
        L.NextStep();
        TestEqual(*(Label + TEXT(": arrow entity removed")), L.A.GetArrows().Num(), Case.bTaken ? 0 : 1);
        TestEqual(*(Label + TEXT(": inventory arrows")), L.CountArrows(), Case.ArrowsAfter);
    }
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFoundationArrowHitMobTest, "Unnamed.Foundation.Arrows.HitMob", BowTestFlags)

bool FFoundationArrowHitMobTest::RunTest(const FString&)
{
    // survival2.r18.arrow_rule: a hit does ceil(impact speed x 2); catches a wrong damage, an arrow left alive or no
    // ArrowHits count.
    FBowLab L(true);
    const int32 CowId = L.A.SpawnMob(EFoundationMobType::Cow, FVector(500, 0, 0));
    FFoundationMob* Cow = L.A.FindMob(CowId);
    if (!TestNotNull(TEXT("the cow"), Cow)) return false;
    const float Before = Cow->Health.Health;
    L.Shoot(FVector(0, 0, 70), FVector::ForwardVector, 3.f, L.Player);
    FVector LastVelocity = FVector::ZeroVector;
    for (int32 Step = 0; Step < 6 && L.A.GetArrows().Num() == 1; ++Step)
    {
        LastVelocity = L.A.GetArrows()[0].VelocityCm;
        L.NextStep();
    }
    TestEqual(TEXT("arrow removed"), L.A.GetArrows().Num(), 0);
    TestEqual(TEXT("ArrowHits counter"), L.A.ArrowHits, 1);
    const int32 Expected = FMath::CeilToInt32(LastVelocity.Size() / 100. * 2.);
    TestTrue(TEXT("impact damage is 6 (speed just under 3)"), Expected == 6);
    Cow = L.A.FindMob(CowId);
    if (!TestNotNull(TEXT("the cow is still there"), Cow)) return false;
    TestEqual(TEXT("cow health dropped by ceil(speed x 2)"), Before - Cow->Health.Health, float(Expected));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFoundationArrowHitPlayerTest, "Unnamed.Foundation.Arrows.HitPlayer", BowTestFlags)

bool FFoundationArrowHitPlayerTest::RunTest(const FString&)
{
    // arrow hit on a player: an Arrow attack of ceil(300 / 100 x 2) = 6 named after the mob shooter; a raised shield
    // facing it blocks it (HurtPlayer false); catches a wrong attack type/amount/attacker or arrows bypassing the
    // shield.
    FBowLab L;
    L.Shoot(FVector(300, 0, 100), -FVector::ForwardVector, 3.f, 999, EFoundationArrowPickup::Disallowed, true,
        TEXT("Skeleton"));
    L.NextStep();
    TestEqual(TEXT("arrow consumed"), L.A.GetArrows().Num(), 0);
    const TArray<FFoundationAttack> Hits = L.A.TakeArrowHits(L.Player);
    if (!TestEqual(TEXT("one arrow attack"), Hits.Num(), 1)) return false;
    TestTrue(TEXT("type Arrow"), Hits[0].Type == EFoundationDamage::Arrow);
    TestEqual(TEXT("amount 6"), Hits[0].Amount, 6.f);
    TestEqual(TEXT("attacker Skeleton"), Hits[0].Attacker, FString(TEXT("Skeleton")));
    TestEqual(TEXT("taken once"), L.A.TakeArrowHits(L.Player).Num(), 0);
    TestTrue(TEXT("hurts the unshielded player"), L.A.HurtPlayer(L.Player, L.Vitals, Hits[0]));
    TestEqual(TEXT("health 20 - 6"), L.Vitals.Health, 14.f);

    // A shield raised past its 5-tick delay, facing the arrow.
    if (!TestTrue(TEXT("a shield item"), L.ShieldItem != INDEX_NONE)) return false;
    L.Hold(L.ShieldItem);
    L.Vitals.Health = 20;
    L.Vitals.DamageCooldown = 0;
    L.Vitals.LastHurt = 0;
    TestTrue(TEXT("shield raised"), L.Draw(6).IsCommitted());
    TestTrue(TEXT("blocking"), L.A.IsBlocking(L.Player));
    L.Shoot(FVector(300, 0, 100), -FVector::ForwardVector, 3.f, 999, EFoundationArrowPickup::Disallowed, true,
        TEXT("Skeleton"));
    L.NextStep();
    const TArray<FFoundationAttack> Blocked = L.A.TakeArrowHits(L.Player);
    if (!TestEqual(TEXT("one arrow attack at the shield"), Blocked.Num(), 1)) return false;
    TestFalse(TEXT("the shield blocks it"), L.A.HurtPlayer(L.Player, L.Vitals, Blocked[0]));
    TestEqual(TEXT("health unchanged"), L.Vitals.Health, 20.f);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFoundationArrowOwnerGraceTest, "Unnamed.Foundation.Arrows.OwnerGrace", BowTestFlags)

bool FFoundationArrowOwnerGraceTest::RunTest(const FString&)
{
    // OwnerGraceTicks 5: the player's own slow arrow (0.05 blocks per tick, inside its grown box for several steps)
    // does not hit it while Age < 5 and hits it at age 5; catches a removed or shorter grace.
    FBowLab L;
    L.Shoot(FVector(0, 0, 100), FVector::ForwardVector, .05f, L.Player);
    for (int32 Step = 0; Step < 4; ++Step)
        L.NextStep();
    TestEqual(TEXT("still flying after 4 steps"), L.A.GetArrows().Num(), 1);
    TestEqual(TEXT("no arrow hits"), L.A.ArrowHits, 0);
    TestEqual(TEXT("no attack on the player"), L.A.TakeArrowHits(L.Player).Num(), 0);
    L.NextStep();
    TestEqual(TEXT("at age 5 it hits its shooter"), L.A.ArrowHits, 1);
    TestEqual(TEXT("one attack on the player"), L.A.TakeArrowHits(L.Player).Num(), 1);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFoundationArrowModelTest, "Unnamed.Foundation.Arrows.Model", BowTestFlags)

bool FFoundationArrowModelTest::RunTest(const FString&)
{
    // DA_Projectiles' arrow#main through FFoundationArrowView::GetModelQuads: 18 faces; the length along +X from the
    // tail (-12 px x 0.9 x 6.25 = -67.5 cm) to the nose (4 px: 22.5 cm), the back's fletching plane at -11 x 0.9 px
    // (-61.875 cm, its own 0.8 scale baked); catches the nose turned the wrong way (min and max swapped) or a pivot
    // or scale lost.
    const FString Path = UUnnamedProjectSettings::GetEntityAssetPath(TEXT("DA_Projectiles")).ToString();
    const UFoundationMobAssets* Assets = LoadObject<UFoundationMobAssets>(nullptr, *Path);
    const FFoundationMobModelAsset* Model = Assets
        ? Assets->Models.FindByPredicate(
              [](const FFoundationMobModelAsset& M) { return M.Layer == TEXT("arrow#main"); })
        : nullptr;
    if (!TestNotNull(TEXT("DA_Projectiles' arrow#main"), Model)) return false;
    TArray<FVector> Corners;
    TArray<FVector2D> UVs;
    FFoundationArrowView::GetModelQuads(*Model, Corners, UVs);
    TestEqual(TEXT("18 faces"), Corners.Num(), 72);
    FBox Bounds(Corners);
    TestTrue(TEXT("tail at -67.5 cm, nose at +22.5 cm"),
        FMath::IsNearlyEqual(Bounds.Min.X, -67.5, .01) && FMath::IsNearlyEqual(Bounds.Max.X, 22.5, .01));
    int32 Back = 0;
    for (const FVector& Corner : Corners)
        Back += FMath::IsNearlyEqual(Corner.X, -61.875, .01);
    // The back is the tallest part: half size 2.5 px x 0.8 x 0.9 = 11.25 cm, turned 45 degrees about the length (its
    AddInfo(FString::Printf(TEXT("arrow model: %d corners on the back plane, bounds %s"), Back, *Bounds.ToString()));
    TestTrue(TEXT("the back's plane at -61.875 cm, its corners 15.91 cm out"),
        Back >= 8 && FMath::IsNearlyEqual(Bounds.Max.Z, 11.25 * UE_DOUBLE_SQRT_2, .01));
    return true;
}

// K-PLAYER-7 (the smear fix, arrows): the arrows are instances of one component that move, not the component, so it
// keeps per-instance previous transforms (with them the engine writes the arrows' motion vectors; without them TSR took
// a flying arrow for still scenery and smeared it). An arrow drawn in its slot again moves from last frame's transform;
// one new to its slot starts still. Drawn by FFoundationArrowView in a bare world from DA_Projectiles' model.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FFoundationArrowMotionVectorsTest, "Unnamed.Foundation.Arrows.MotionVectors", BowTestFlags)
bool FFoundationArrowMotionVectorsTest::RunTest(const FString&)
{
    FBowLab Lab;
    Lab.Shoot(FVector(0, 0, 500), FVector::ForwardVector);
    // A bare world, never begun, rooted while the test holds it (no world context, so DestroyWorld(false)).
    UWorld* World = UWorld::CreateWorld(EWorldType::Game, false);
    if (World) World->AddToRoot();
    AActor* Owner = World ? World->SpawnActor<AActor>() : nullptr;
    if (TestNotNull(TEXT("an actor"), Owner))
    {
        USceneComponent* Root = NewObject<USceneComponent>(Owner);
        Owner->SetRootComponent(Root);
        Root->RegisterComponent();
        FFoundationArrowView View(*NewObject<UFoundationInteractionView>(Owner));
        // Slot 0 (the first instance): its transform and previous transform, in the world.
        FTransform Now, Previous;
        auto Slot0 = [Owner, &Now, &Previous]
        {
            const auto* Component = Owner->FindComponentByClass<UInstancedStaticMeshComponent>();
            return Component && Component->GetInstanceTransform(0, Now, true) &&
                Component->GetInstancePrevTransform(0, Previous, true);
        };
        View.Update(Lab.A, nullptr, 1);
        const auto* Arrows = Owner->FindComponentByClass<UInstancedStaticMeshComponent>();
        if (TestTrue(TEXT("the arrow is drawn"), View.GetDrawnCount() == 1 && Arrows && Slot0()))
        {
            TestEqual(TEXT("every instance has a previous transform"), Arrows->PerInstancePrevTransform.Num(),
                Arrows->GetInstanceCount());
            TestTrue(TEXT("drawn first: still"), Previous.Equals(Now, 1e-3));
            const FTransform First = Now;
            // A step on, it has flown on.
            Lab.NextStep();
            View.Update(Lab.A, nullptr, 1);
            Slot0();
            TestTrue(TEXT("flying: the previous transform is last frame's"), Previous.Equals(First, 1e-3));
            TestTrue(TEXT("flying: it moved since"), FVector::Dist(Now.GetLocation(), Previous.GetLocation()) > 100);
            // Another arrow takes the slot: it starts still.
            Lab.A.RemoveArrows();
            Lab.Shoot(FVector(0, 300, 500), FVector::ForwardVector);
            View.Update(Lab.A, nullptr, 1);
            Slot0();
            TestTrue(TEXT("a new arrow in the slot: still"), Previous.Equals(Now, 1e-3));
        }
    }
    if (World)
    {
        World->RemoveFromRoot();
        World->DestroyWorld(false);
    }
    return true;
}
#endif
