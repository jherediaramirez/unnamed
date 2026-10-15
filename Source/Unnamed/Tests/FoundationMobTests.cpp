#include "Misc/AutomationTest.h"
#include "Dom/JsonObject.h"
#include "Foundation/FoundationAuthority.h"
#include "Foundation/FoundationBlockRegistry.h"
#include "Foundation/FoundationGrid.h"
#include "Foundation/FoundationInteractionSettings.h"
#include "Foundation/FoundationMobAssets.h"
#include "Foundation/FoundationMobView.h"
#include "Foundation/FoundationMobs.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"

#if WITH_DEV_AUTOMATION_TESTS
// Survival T6 20a: the mob framework and the cow (scout facts.md section 5's oracles, the Codex T6 corrections, the
// speed probe C:\dev\sandbox\mobs-20a\speed_probe). Expected values are literals from those sources, never the
// constants under test.
namespace MobTestsLocal
{
    // The project's rules and blocks, a floor at z 0, one survival player at (-300, 0) facing +X, mobs on.
    struct FMobLab
    {
        FFoundationAuthority A;
        FFoundationVitals Vitals;
        int32 Player = 0;
        uint32 Sequence = 1;
        int64 StepIndex = 0;

        FMobLab()
        {
            UFoundationInteractionSettings::Get().BuildRules(A.Rules);
            A.World.SetRegistry(FFoundationBlockRegistry::Build(
                FFoundationBlockRegistry::GetDefault()->GetAssets(), TArray<FFoundationBlockDefinition>()));
            A.World.BoundsMin = FIntVector(-20, -20, 0);
            A.World.BoundsMax = FIntVector(19, 19, 15);
            FFoundationStatic Floor;
            Floor.Box = FBox(FVector(-2000, -2000, -100), FVector(2000, 2000, 0));
            Floor.Label = TEXT("floor");
            Floor.bAnchor = true;
            A.Statics.Add(Floor);
            A.Options.bInfiniteSupply = false;
            A.Options.bMobs = true;
            A.Options.bItemEntities = true;
            A.Options.bExperienceOrbs = true;
            Player = A.AddPlayer();
            A.Reset();
            Vitals.bNaturalRegeneration = false;
            Stand(FVector(-200, 0, 0));
        }

        FFoundationPlayer& P() { return *A.FindPlayer(Player); }

        void Stand(FVector Feet, FVector Aim = FVector(1, 0, -.3))
        {
            FFoundationPlayerFacts Facts;
            Facts.FeetCm = Feet;
            Facts.HeightCm = 180;
            Facts.EyeCm = Feet + FVector(0, 0, 162);
            Facts.Aim = Aim.GetSafeNormal();
            Facts.bGrounded = true;
            Facts.Step = ++StepIndex;
            A.SetFacts(Player, Facts);
        }

        void Steps(int32 Count)
        {
            for (int32 Index = 0; Index < Count; ++Index)
            {
                FFoundationPlayerFacts Facts = P().Facts;
                Facts.Step = ++StepIndex;
                A.SetFacts(Player, Facts);
                A.Step();
            }
        }

        void Hold(FName Item)
        {
            P().Inventory.SetSlot(
                0, Item.IsNone() ? FFoundationItemInstance() : P().Inventory.MakeStack(A.Rules.FindItem(Item), 1));
            P().Inventory.Select(0);
        }

        // A fully charged attack on what the player aims at.
        FFoundationResult Attack()
        {
            Vitals.AttackTicker = 100;
            FFoundationCommand Command;
            Command.SessionId = A.GetSessionId();
            Command.Type = EFoundationCommandType::Attack;
            Command.Hand = EFoundationHand::Main;
            Command.Item = P().Inventory.GetHand(EFoundationHand::Main).GetRef();
            Command.Target = A.Resolve(P().Facts);
            Command.Sequence = Sequence++;
            return A.Execute(Player, Command, Vitals);
        }

        // A cow standing at the origin, settled on the floor, its AI held still (no stroll target).
        FFoundationMob& Cow()
        {
            const int32 Id = A.SpawnMob(EFoundationMobType::Cow, FVector::ZeroVector);
            FFoundationMob& Mob = *A.FindMob(Id);
            return Mob;
        }
    };

    // Steps a mob alone against the lab floor: no AI, InputDir at the mob's speed modifier.
    void MoveAlone(FFoundationMob& Mob, int32 Steps, FVector2D InputDir = FVector2D::ZeroVector)
    {
        const TArray<FBox> Boxes = {FBox(FVector(-20000, -20000, -100), FVector(20000, 20000, 0))};
        for (int32 Step = 0; Step < Steps; ++Step)
            FoundationMobs::Move(Mob, FVector::ZeroVector, Boxes, InputDir, false);
    }

    TSharedPtr<FJsonObject> ReadReferenceJson(const TCHAR* Relative)
    {
        FString Text;
        TSharedPtr<FJsonObject> Root;
        const FString Path = FPaths::Combine(FPaths::ProjectDir(), TEXT("Reference/minecraft-entities-26.3"), Relative);
        if (FFileHelper::LoadFileToString(Text, *Path))
            FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Text), Root);
        return Root;
    }

    // Block storage with columns that can be marked unloaded (as the item entities' streaming test).
    class FColumnStorage final : public IFoundationBlockStorage
    {
    public:
        TSet<FIntPoint> Unloaded;

        virtual FFoundationCell Get(const FIntVector& Cell) const override { return Inner.Get(Cell); }
        virtual void Set(const FIntVector& Cell, const FFoundationCell& Value) override { Inner.Set(Cell, Value); }
        virtual void Empty() override { Inner.Empty(); }
        virtual int32 GetSolidCount() const override { return Inner.GetSolidCount(); }
        virtual void ForEachSolid(const FIntVector& Min, const FIntVector& Max,
            TFunctionRef<void(const FIntVector&, const FFoundationCell&)> Visit) const override
        {
            Inner.ForEachSolid(Min, Max, Visit);
        }
        virtual SIZE_T GetAllocatedSize() const override { return Inner.GetAllocatedSize(); }
        virtual bool IsLoaded(const FIntVector& Cell) const override
        {
            return !Unloaded.Contains(
                FIntPoint(FoundationGrid::FloorDivide(Cell.X, 16), FoundationGrid::FloorDivide(Cell.Y, 16)));
        }

    private:
        FFoundationSectionStorage Inner;
    };
}

// O1 (cow) and the attribute facts: a summoned cow has 10 of 10 health; every kind matches mob_probe's health and
// movement attribute; the cow's box is 0.9 x 1.4 [catalog.entities]. Mutation: swap the cow's and sheep's health.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFoundationMobSpawnTest, "Unnamed.Foundation.Mobs.Spawn",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FFoundationMobSpawnTest::RunTest(const FString&)
{
    using namespace MobTestsLocal;
    FMobLab L;
    const int32 Id = L.A.SpawnMob(EFoundationMobType::Cow, FVector(0, 0, 0));
    const FFoundationMob* Cow = L.A.FindMob(Id);
    if (!TestNotNull(TEXT("summoned"), Cow)) return false;
    TestEqual(TEXT("cow health"), Cow->Health.Health, 10.f);
    TestEqual(TEXT("cow max health"), Cow->Health.MaxHealth, 10.f);
    const FBox Box = FoundationMobs::GetBox(EFoundationMobType::Cow, FVector::ZeroVector);
    TestEqual(TEXT("cow box"), Box.GetSize(), FVector(90, 90, 140));
    // mob_probe: health 10 / 10 / 8 / 4 / 20 / 20 / 20 / 16; movement 0.2 / 0.25 / 0.23 / 0.25 / 0.23 / 0.25 / 0.25 /
    // 0.3.
    const float Health[] = {10, 10, 8, 4, 20, 20, 20, 16};
    const float Speed[] = {.2f, .25f, .23f, .25f, .23f, .25f, .25f, .3f};
    const TSharedPtr<FJsonObject> Facts = ReadReferenceJson(TEXT("facts/mob_attributes.json"));
    const TSharedPtr<FJsonObject>* Mobs = nullptr;
    TestTrue(TEXT("mob_attributes.json read"), Facts.IsValid() && Facts->TryGetObjectField(TEXT("mobs"), Mobs));
    for (int32 Type = 0; Type < int32(EFoundationMobType::Count); ++Type)
    {
        const FFoundationMobKind& Kind = FoundationMobs::GetKind(EFoundationMobType(Type));
        TestEqual(*FString::Printf(TEXT("%s health"), *Kind.Id.ToString()), Kind.MaxHealth, Health[Type]);
        TestEqual(*FString::Printf(TEXT("%s speed"), *Kind.Id.ToString()), Kind.Speed, Speed[Type], 1e-6f);
        const TSharedPtr<FJsonObject>* Mob = nullptr;
        const TSharedPtr<FJsonObject>* Attributes = nullptr;
        double ProbeHealth = 0, ProbeSpeed = 0;
        if (Mobs && (*Mobs)->TryGetObjectField(Kind.Id.ToString(), Mob) &&
            (*Mob)->TryGetObjectField(TEXT("attributes"), Attributes))
        {
            (*Mob)->TryGetNumberField(TEXT("health"), ProbeHealth);
            (*Attributes)->TryGetNumberField(TEXT("movement_speed"), ProbeSpeed);
        }
        TestEqual(
            *FString::Printf(TEXT("%s health = probe"), *Kind.Id.ToString()), double(Kind.MaxHealth), ProbeHealth);
        TestEqual(
            *FString::Printf(TEXT("%s speed = probe"), *Kind.Id.ToString()), double(Kind.Speed), ProbeSpeed, 1e-6);
    }
    // 20b: the four farm animals spawn, the hostiles wait for 20c-20d. value-swaps.md T6: boxes pig 0.9 x 0.9, sheep
    // 0.9 x 1.3, chicken 0.4 x 0.7; Codex 02 section B: panic 2 / 1.25 / 1.25 / 1.4, tempt 1.25 / 1.2 / 1.1 / 1.0.
    const FVector Boxes[] = {FVector(90, 90, 140), FVector(90, 90, 90), FVector(90, 90, 130), FVector(40, 40, 70)};
    const float Panic[] = {2, 1.25f, 1.25f, 1.4f}, Tempt[] = {1.25f, 1.2f, 1.1f, 1};
    for (int32 Type = 0; Type < int32(EFoundationMobType::Count); ++Type)
    {
        const FFoundationMobKind& Kind = FoundationMobs::GetKind(EFoundationMobType(Type));
        TestEqual(*FString::Printf(TEXT("%s spawnable"), *Kind.Id.ToString()), Kind.bSpawnable, Type < 4);
        if (Type >= 4) continue;
        TestEqual(*FString::Printf(TEXT("%s box"), *Kind.Id.ToString()),
            FoundationMobs::GetBox(EFoundationMobType(Type), FVector::ZeroVector).GetSize(), Boxes[Type]);
        TestEqual(*FString::Printf(TEXT("%s panic"), *Kind.Id.ToString()), Kind.PanicSpeed, Panic[Type]);
        TestEqual(*FString::Printf(TEXT("%s tempt"), *Kind.Id.ToString()), Kind.TemptSpeed, Tempt[Type]);
        const int32 Summoned = L.A.SpawnMob(EFoundationMobType(Type), FVector(0, 300, 0));
        const FFoundationMob* Mob = L.A.FindMob(Summoned);
        TestTrue(*FString::Printf(TEXT("%s summoned with its health"), *Kind.Id.ToString()),
            Mob && Mob->Health.Health == Health[Type]);
    }
    return true;
}

// O5 and the speed probe: at rest on the ground the vertical velocity is -0.0784 blocks per tick; after a grounded
// knockback of 0.4 the x steps are 0.4, 0.2184, 0.19874, ... (ground 0.546 for the first step, air 0.91 after) and it
// lands on the tenth; a strolling cow (modifier 1) steps 0.04, 0.0618 from rest toward 0.0881, a panicking one
// (modifier 2) 0.16 toward 0.352. Mutation: air retention 0.98 (the items' drag).
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFoundationMobPhysicsTest, "Unnamed.Foundation.Mobs.Physics",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FFoundationMobPhysicsTest::RunTest(const FString&)
{
    using namespace MobTestsLocal;
    FFoundationMob Mob;
    MoveAlone(Mob, 3);
    TestTrue(TEXT("resting on the floor"), Mob.bOnGround && FMath::IsNearlyZero(Mob.LocalCm.Z));
    TestEqual(TEXT("resting vertical velocity"), Mob.VelocityCm.Z, -7.84, 1e-6);
    FoundationMobs::ApplyKnockback(Mob.VelocityCm, FVector2D(1, 0), true, 40);
    const double Expected[] = {.4, .2184, .19874, .18086, .16458, .14977, .13629, .12402, .11286, .10270};
    for (int32 Tick = 0; Tick < UE_ARRAY_COUNT(Expected); ++Tick)
    {
        const double Before = Mob.LocalCm.X;
        MoveAlone(Mob, 1);
        TestEqual(*FString::Printf(TEXT("knockback x step %d"), Tick + 1), (Mob.LocalCm.X - Before) / 100,
            Expected[Tick], 1e-4);
        if (Tick == 4) TestEqual(TEXT("height at tick 5 (probe 0.965)"), Mob.LocalCm.Z / 100, .965, .01);
        if (Tick == 8) TestTrue(TEXT("still airborne at tick 9"), Mob.LocalCm.Z > 0);
    }
    TestTrue(TEXT("landed at tick 10"), Mob.bOnGround && FMath::IsNearlyZero(Mob.LocalCm.Z));
    for (const float Modifier : {1.f, 2.f})
    {
        FFoundationMob Walker;
        Walker.SpeedModifier = Modifier;
        MoveAlone(Walker, 3);
        TArray<double> Steps;
        for (int32 Tick = 0; Tick < 40; ++Tick)
        {
            const double Before = Walker.LocalCm.X;
            MoveAlone(Walker, 1, FVector2D(1, 0));
            Steps.Add((Walker.LocalCm.X - Before) / 100);
        }
        const bool bPanic = Modifier == 2;
        TestEqual(TEXT("first step from rest"), Steps[0], bPanic ? .160 : .0400, 1e-4);
        TestEqual(TEXT("second step"), Steps[1], bPanic ? .247 : .0618, 1e-3);
        TestEqual(TEXT("plateau"), Steps.Last(), bPanic ? .352 : .0881, 1e-3);
    }
    return true;
}

// O3: a cow at rest on the ground, hit by the player from -X: velocity (0.4, 0.3608, 0) blocks per tick, away from
// the player. Mutation: drop the vy/2 term (0.4).
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFoundationMobKnockbackTest, "Unnamed.Foundation.Mobs.Knockback",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FFoundationMobKnockbackTest::RunTest(const FString&)
{
    using namespace MobTestsLocal;
    FMobLab L;
    FFoundationMob& Cow = L.Cow();
    const int32 Id = Cow.Id;
    L.Steps(3);
    FFoundationMob* Mob = L.A.FindMob(Id);
    Mob->bMoving = false;
    Mob->VelocityCm.X = Mob->VelocityCm.Y = 0;
    TestEqual(TEXT("resting vertical velocity"), Mob->VelocityCm.Z, -7.84, 1e-4);
    L.Hold(NAME_None);
    const FFoundationResult Result = L.Attack();
    TestEqual(TEXT("committed"), Result.Outcome, EFoundationOutcome::Committed);
    TestEqual(TEXT("hit the cow"), Result.EntityId, Id);
    TestTrue(TEXT("knocked back"), Result.bKnockback);
    TestEqual(TEXT("x 0.4"), Mob->VelocityCm.X, 40., 1e-3);
    TestEqual(TEXT("z 0.3608"), Mob->VelocityCm.Z, 36.08, 1e-3);
    TestEqual(TEXT("y 0"), Mob->VelocityCm.Y, 0., 1e-3);
    TestEqual(TEXT("fist damage 1"), Mob->Health.Health, 9.f);
    TestEqual(TEXT("hurt animation 10"), Mob->HurtTime, 10);
    return true;
}

// O4 [hurt.cooldown, hurt.repeated_hits]: a second hit inside the window (cooldown above 10) of equal damage is
// rejected and one stronger deals only the excess, neither knocking back; after 10 steps an equal hit lands again.
// Mutation: knock back on every hit.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFoundationMobHurtWindowTest, "Unnamed.Foundation.Mobs.HurtWindow",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FFoundationMobHurtWindowTest::RunTest(const FString&)
{
    using namespace MobTestsLocal;
    FMobLab L;
    const int32 Id = L.Cow().Id;
    FFoundationMob* Mob = L.A.FindMob(Id);
    Mob->Health.MaxHealth = Mob->Health.Health = 100;
    L.Hold(NAME_None);
    TestTrue(TEXT("first hit lands"), L.Attack().DamageApplied == 1);
    Mob->VelocityCm = FVector::ZeroVector;
    const FFoundationResult Equal = L.Attack();
    TestEqual(TEXT("equal hit rejected"), Equal.Reason, EFoundationReason::TargetCooldown);
    L.Hold(TEXT("iron_sword"));
    const float Sword = L.A.Rules.GetItem(L.A.Rules.FindItem(TEXT("iron_sword"))).AttackDamage;
    TestEqual(TEXT("iron sword damage 6"), Sword, 6.f);
    const FFoundationResult Stronger = L.Attack();
    TestEqual(TEXT("stronger hit deals the excess 5"), Stronger.DamageApplied, 5.f);
    TestFalse(TEXT("no knockback inside the window"), Stronger.bKnockback);
    TestTrue(TEXT("velocity untouched"), Mob->VelocityCm.IsNearlyZero());
    L.Steps(10);
    L.Hold(NAME_None);
    // Back in the aim (it panicked away), still.
    Mob = L.A.FindMob(Id);
    Mob->LocalCm = Mob->PreviousLocalCm = FVector::ZeroVector;
    Mob->VelocityCm = FVector::ZeroVector;
    const FFoundationResult Later = L.Attack();
    TestEqual(TEXT("after 10 steps an equal hit lands"), Later.DamageApplied, 1.f);
    TestTrue(TEXT("and knocks back"), Later.bKnockback);
    return true;
}

// [survival.r10.death_roll] the roll 0, 0, 25.455844, 36, 44.090818, 50.911694 degrees at death ticks 0-5, 90 from
// tick 14; [hurt.overlay] red while dying; the corpse stays to death tick 19 and goes at 20 (provisional). Mutation:
// the roll without its -1 (sqrt(deathTime x 1.6 / 20)).
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFoundationMobDeathTest, "Unnamed.Foundation.Mobs.Death",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FFoundationMobDeathTest::RunTest(const FString&)
{
    using namespace MobTestsLocal;
    const float Roll[] = {0, 0, 25.455844f, 35.999999f, 44.090818f, 50.911694f, 56.920995f, 62.353826f};
    for (int32 Tick = 0; Tick < UE_ARRAY_COUNT(Roll); ++Tick)
        TestEqual(
            *FString::Printf(TEXT("roll at tick %d"), Tick), FoundationMobs::GetDeathRoll(Tick, 90), Roll[Tick], 1e-3f);
    TestEqual(TEXT("full at tick 14"), FoundationMobs::GetDeathRoll(14, 90), 90.f, 1e-4f);
    TestTrue(TEXT("not full at tick 13"), FoundationMobs::GetDeathRoll(13, 90) < 90.f);
    TestEqual(TEXT("spider 180 at tick 14"), FoundationMobs::GetDeathRoll(14, 180), 180.f, 1e-4f);
    // The view draws the same curve: a cow at death tick 2 (no step fraction) lies 25.455844 degrees over.
    {
        FFoundationMob Dying;
        Dying.DeathTime = 2;
        const FRotator Drawn = FFoundationMobView::MakeRootTransform(Dying, FVector::ZeroVector, 0).Rotator();
        TestEqual(TEXT("the view's roll at death tick 2"), FMath::Abs(Drawn.Roll), 25.455844, 1e-3);
    }
    FMobLab L;
    const int32 Id = L.Cow().Id;
    L.A.FindMob(Id)->Health.Health = 1;
    L.Hold(NAME_None);
    const FFoundationResult Kill = L.Attack();
    TestTrue(TEXT("killed"), Kill.bTargetDied);
    const FFoundationMob* Mob = L.A.FindMob(Id);
    TestTrue(TEXT("killed by the player"), Mob && Mob->bKilledByPlayer);
    // A corpse still stops the aim until it is removed; hits on it are refused.
    TestEqual(TEXT("a dying cow is still aimed at"), L.A.Resolve(L.P().Facts).EntityId, Id);
    TestEqual(TEXT("and refuses hits"), L.Attack().Reason, EFoundationReason::TargetDead);
    L.Steps(19);
    Mob = L.A.FindMob(Id);
    TestTrue(TEXT("corpse at death tick 19"), Mob && Mob->DeathTime == 19);
    L.Steps(1);
    TestNull(TEXT("gone at death tick 20"), L.A.FindMob(Id));
    TestEqual(TEXT("one kill"), L.A.MobsKilled, 1);
    return true;
}

// O7 (cow): with our RNG over 30,000 player kills leather 0, 1, 2 and beef 1, 2, 3 each 1/3 +- 0.01; a kill's drops
// arrive as item entities with 1-3 experience; on fire the beef is cooked (furnace_smelt); not by a player, no
// experience. Mutation: set_count always its maximum.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFoundationMobLootTest, "Unnamed.Foundation.Mobs.Loot",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FFoundationMobLootTest::RunTest(const FString&)
{
    using namespace MobTestsLocal;
    FMobLab L;
    const FFoundationLootTable* Table = L.A.Rules.EntityLootTables.Find(TEXT("cow"));
    if (!TestNotNull(TEXT("entities/cow from DA_Mobs"), Table)) return false;
    const int32 Leather = L.A.Rules.FindItem(TEXT("leather")), Beef = L.A.Rules.FindItem(TEXT("beef")),
                Cooked = L.A.Rules.FindItem(TEXT("cooked_beef"));
    TestTrue(TEXT("beef, leather and cooked beef are items"), Leather >= 0 && Beef >= 0 && Cooked >= 0);
    int32 LeatherCounts[3] = {}, BeefCounts[4] = {};
    FRandomStream Random(263);
    constexpr int32 Rolls = 30000;
    for (int32 Roll = 0; Roll < Rolls; ++Roll)
    {
        FFoundationLootContext Context{L.A.Rules.Hand};
        Context.bKilledByPlayer = true;
        TArray<FFoundationLootDrop> Drops;
        FoundationLoot::Roll(*Table, L.A.Rules.LootPredicates, Context, Random, Drops);
        int32 LeatherCount = 0, BeefCount = 0;
        for (const FFoundationLootDrop& Drop : Drops)
            (Drop.Item == Leather ? LeatherCount : BeefCount) += Drop.Count;
        if (LeatherCount <= 2) ++LeatherCounts[LeatherCount];
        if (BeefCount >= 1 && BeefCount <= 3) ++BeefCounts[BeefCount];
    }
    for (int32 Count = 0; Count < 3; ++Count)
    {
        TestEqual(*FString::Printf(TEXT("leather %d"), Count), LeatherCounts[Count] / double(Rolls), 1 / 3., .01);
        TestEqual(*FString::Printf(TEXT("beef %d"), Count + 1), BeefCounts[Count + 1] / double(Rolls), 1 / 3., .01);
    }
    {
        FFoundationLootContext Context{L.A.Rules.Hand};
        Context.bOnFire = true;
        TArray<FFoundationLootDrop> Drops;
        FoundationLoot::Roll(*Table, L.A.Rules.LootPredicates, Context, Random, Drops);
        TestTrue(TEXT("on fire: cooked beef"),
            Drops.ContainsByPredicate([Cooked](const FFoundationLootDrop& Drop) { return Drop.Item == Cooked; }));
    }
    // A player's kill in the lab: items and orbs.
    const int32 Id = L.Cow().Id;
    L.A.FindMob(Id)->Health.Health = 1;
    L.Hold(NAME_None);
    L.Attack();
    L.Steps(20);
    int32 BeefDropped = 0;
    for (const FFoundationItemEntity& Entity : L.A.GetItemEntities())
        BeefDropped += Entity.Item == Beef ? Entity.Count : 0;
    TestTrue(TEXT("1-3 beef dropped"), BeefDropped >= 1 && BeefDropped <= 3);
    int32 Points = 0;
    for (const FFoundationExperienceOrb& Orb : L.A.GetExperienceOrbs())
        Points += Orb.Value * Orb.Count;
    TestTrue(TEXT("1-3 experience"), Points >= 1 && Points <= 3);
    return true;
}

// O16: a mob on an unloaded column neither moves, thinks nor counts its timers, and cannot be hit; once loaded it
// runs on. Mutation: the AI keeps running while frozen.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFoundationMobStreamingTest, "Unnamed.Foundation.Mobs.Streaming",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FFoundationMobStreamingTest::RunTest(const FString&)
{
    using namespace MobTestsLocal;
    FMobLab L;
    TUniquePtr<FColumnStorage> Storage = MakeUnique<FColumnStorage>();
    FColumnStorage* Columns = Storage.Get();
    L.A.World.SetStorage(MoveTemp(Storage));
    const int32 Id = L.A.SpawnMob(EFoundationMobType::Cow, FVector(50, 50, 300));
    L.Steps(2);
    FFoundationMob* Mob = L.A.FindMob(Id);
    Mob->HurtTime = 7;
    Mob->bHurtByEntity = true;
    Columns->Unloaded.Add(FIntPoint(0, 0));
    const FFoundationMob Before = *Mob;
    L.Steps(30);
    Mob = L.A.FindMob(Id);
    TestTrue(TEXT("frozen"), Mob->bFrozen);
    TestEqual(TEXT("no motion"), Mob->LocalCm, Before.LocalCm);
    TestEqual(TEXT("previous = current (drawn still)"), Mob->PreviousLocalCm, Mob->LocalCm);
    TestEqual(TEXT("no age"), Mob->Age, Before.Age);
    TestEqual(TEXT("hurt timer kept"), Mob->HurtTime, 7);
    TestTrue(TEXT("no AI: the panic trigger waits"), Mob->bHurtByEntity && !Mob->bPanicking);
    L.Stand(FVector(-150, 50, 200), FVector(1, 0, 0));
    TestTrue(TEXT("not a target"), L.A.Resolve(L.P().Facts).Kind != EFoundationHitKind::Entity);
    Columns->Unloaded.Reset();
    L.Steps(2);
    Mob = L.A.FindMob(Id);
    TestTrue(
        TEXT("thawed: it falls and panics"), !Mob->bFrozen && Mob->LocalCm.Z < Before.LocalCm.Z && Mob->bPanicking);
    return true;
}

// O17: with foundation.Mobs 0 (Options.bMobs off) nothing spawns, Resolve sees training targets only and mobs already
// there do not step. Mutation: Resolve adds mobs whatever the switch.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFoundationMobSwitchTest, "Unnamed.Foundation.Mobs.Switch",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FFoundationMobSwitchTest::RunTest(const FString&)
{
    using namespace MobTestsLocal;
    FMobLab L;
    const int32 Id = L.Cow().Id;
    TestEqual(TEXT("on: the cow is the target"), L.A.Resolve(L.P().Facts).EntityId, Id);
    L.A.Options.bMobs = false;
    TestEqual(TEXT("off: nothing spawns"), L.A.SpawnMob(EFoundationMobType::Cow, FVector(0, 0, 0)), 0);
    TestTrue(TEXT("off: Resolve ignores the cow"), L.A.Resolve(L.P().Facts).Kind != EFoundationHitKind::Entity);
    const FFoundationMob Before = *L.A.FindMob(Id);
    L.Steps(5);
    TestEqual(TEXT("off: the cow does not step"), L.A.FindMob(Id)->Age, Before.Age);
    TestTrue(TEXT("off: no attack reaches it"),
        L.Attack().Outcome != EFoundationOutcome::Committed || L.A.FindMob(Id)->Health.Health == 10.f);
    TestFalse(TEXT("the console switch defaults on"), !FoundationMobs::IsOn());
    return true;
}

// O12, O13: DA_Mobs' cow is the probe's model: 7 parts, 10 cubes; head pivot (0, 4, -8); body pivot (0, 5, 2) rotated
// x = pi/2; legs at (+-4, 12, 7) and (+-4, 12, -5), each cube (-2, 0, -2)-(2, 12, 2); drawn by FFoundationMobView the
// model spans x -6..6, y -1..24 and z -15..10 pixels with its feet on the ground and its head ahead (+X); every face's
// UVs match entity_models.json within 1e-6; the sheep's texture is 64 x 32. Mutation: the feet at y 0 (the y-up
// mistake).
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFoundationMobModelTest, "Unnamed.Foundation.Mobs.Model",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FFoundationMobModelTest::RunTest(const FString&)
{
    using namespace MobTestsLocal;
    const UFoundationMobAssets* Assets = UFoundationMobAssets::LoadDefault();
    if (!TestNotNull(TEXT("DA_Mobs"), Assets)) return false;
    const FFoundationMobModelAsset* Cow = Assets->FindModel(TEXT("cow"));
    if (!TestNotNull(TEXT("cow model"), Cow)) return false;
    TestEqual(TEXT("7 parts"), Cow->Parts.Num(), 7);
    int32 Faces = 0;
    for (const FFoundationMobPartAsset& Part : Cow->Parts)
        Faces += Part.Faces.Num();
    TestEqual(TEXT("10 cubes"), Faces, 60);
    const auto FindPart = [Cow](const TCHAR* Path)
    { return Cow->Parts.FindByPredicate([Path](const FFoundationMobPartAsset& P) { return P.Path == Path; }); };
    const FFoundationMobPartAsset* Head = FindPart(TEXT("head"));
    const FFoundationMobPartAsset* Body = FindPart(TEXT("body"));
    TestTrue(TEXT("head pivot (0, 4, -8)"), Head && Head->Pivot.Equals(FVector(0, 4, -8)));
    TestTrue(TEXT("body pivot (0, 5, 2), x = pi/2"),
        Body && Body->Pivot.Equals(FVector(0, 5, 2)) && FMath::IsNearlyEqual(Body->Rotation.X, UE_HALF_PI, 1e-6f));
    for (const TCHAR* Leg :
        {TEXT("right_hind_leg"), TEXT("left_hind_leg"), TEXT("right_front_leg"), TEXT("left_front_leg")})
    {
        const FFoundationMobPartAsset* Part = FindPart(Leg);
        if (!TestNotNull(Leg, Part)) continue;
        const float X = FString(Leg).StartsWith(TEXT("right")) ? -4 : 4,
                    Z = FString(Leg).Contains(TEXT("hind")) ? 7 : -5;
        TestTrue(*FString::Printf(TEXT("%s pivot"), Leg), Part->Pivot.Equals(FVector(X, 12, Z)));
        FBox Cube(ForceInit);
        for (const FFoundationMobFaceAsset& Face : Part->Faces)
            for (const FVector& Corner : Face.Positions)
                Cube += Corner;
        TestTrue(*FString::Printf(TEXT("%s cube"), Leg),
            Cube.Min.Equals(FVector(-2, 0, -2)) && Cube.Max.Equals(FVector(2, 12, 2)));
    }
    // Drawn: the rest pose of a mob at the origin facing +X.
    FFoundationMob Mob;
    FBox Drawn(ForceInit);
    const FTransform Root = FFoundationMobView::MakeRootTransform(Mob, FVector::ZeroVector, 1);
    for (const FFoundationMobPartAsset& Part : Cow->Parts)
    {
        TArray<FVector> Corners;
        TArray<FVector2D> UVs;
        FFoundationMobView::GetPartQuads(Part, Corners, UVs);
        const FTransform World = FFoundationMobView::MakePartTransform(Part, FVector::ZeroVector) * Root;
        for (const FVector& Corner : Corners)
            Drawn += World.TransformPosition(Corner);
    }
    // Model x -6..6 is Y -37.5..37.5, z -15..10 is X 93.75..-62.5, y -1..24 is Z 156.25..0.
    TestTrue(TEXT("drawn X -62.5..93.75 (head ahead)"),
        FMath::IsNearlyEqual(Drawn.Min.X, -62.5, 1e-3) && FMath::IsNearlyEqual(Drawn.Max.X, 93.75, 1e-3));
    TestTrue(TEXT("drawn Y -37.5..37.5"),
        FMath::IsNearlyEqual(Drawn.Min.Y, -37.5, 1e-3) && FMath::IsNearlyEqual(Drawn.Max.Y, 37.5, 1e-3));
    TestTrue(TEXT("drawn Z 0..156.25 (feet on the ground)"),
        FMath::IsNearlyEqual(Drawn.Min.Z, 0., 1e-3) && FMath::IsNearlyEqual(Drawn.Max.Z, 156.25, 1e-3));
    // O13: the faces' UVs against the facts, cube by cube in the facts' order.
    const TSharedPtr<FJsonObject> Facts = ReadReferenceJson(TEXT("facts/entity_models.json"));
    const TSharedPtr<FJsonObject>* Layer = nullptr;
    const TArray<TSharedPtr<FJsonValue>>* Cubes = nullptr;
    if (TestTrue(TEXT("entity_models.json read"),
            Facts.IsValid() && Facts->TryGetObjectField(TEXT("cow#main"), Layer) &&
                (*Layer)->TryGetArrayField(TEXT("cubes"), Cubes)))
    {
        int32 Checked = 0, Wrong = 0;
        TMap<FName, int32> NextFace;
        for (const TSharedPtr<FJsonValue>& CubeValue : *Cubes)
        {
            const TSharedPtr<FJsonObject> Cube = CubeValue->AsObject();
            const FName Path(
                Cube->GetStringField(TEXT("path")).Replace(TEXT("/"), TEXT(""), ESearchCase::CaseSensitive));
            const FFoundationMobPartAsset* Part = FindPart(*Path.ToString());
            int32& Face = NextFace.FindOrAdd(Path);
            for (const TSharedPtr<FJsonValue>& FaceValue : Cube->GetArrayField(TEXT("faces")))
            {
                const TArray<TSharedPtr<FJsonValue>>& Corners = FaceValue->AsObject()->GetArrayField(TEXT("v"));
                for (int32 Corner = 0; Corner < Corners.Num(); ++Corner)
                {
                    const TArray<TSharedPtr<FJsonValue>>& V = Corners[Corner]->AsArray();
                    const bool bMatch = Part && Part->Faces.IsValidIndex(Face) &&
                        FMath::IsNearlyEqual(Part->Faces[Face].UVs[Corner].X, V[3]->AsNumber(), 1e-6) &&
                        FMath::IsNearlyEqual(Part->Faces[Face].UVs[Corner].Y, V[4]->AsNumber(), 1e-6);
                    Wrong += bMatch ? 0 : 1;
                    ++Checked;
                }
                ++Face;
            }
        }
        TestEqual(TEXT("240 corner UVs checked"), Checked, 240);
        TestEqual(TEXT("UVs differing from the probe"), Wrong, 0);
    }
    TestEqual(TEXT("cow texture 64 x 64"), Cow->TextureSize, FIntPoint(64, 64));
    const FFoundationMobModelAsset* Sheep = Assets->FindModel(TEXT("sheep"));
    TestTrue(TEXT("sheep texture 64 x 32"), Sheep && Sheep->TextureSize == FIntPoint(64, 32));
    // 20b: the sheep's wool layer follows its main layer, with its own 64 x 32 texture.
    TArray<const FFoundationMobModelAsset*> Layers;
    Assets->FindLayers(TEXT("sheep"), Layers);
    TestTrue(TEXT("sheep layers main, wool"),
        Layers.Num() == 2 && Layers[0]->Layer == TEXT("sheep#main") && Layers[1]->Layer == TEXT("sheep#wool") &&
            Layers[1]->TextureSize == FIntPoint(64, 32));
    return true;
}

// Survival T6 20b: pig, sheep and chicken behaviour and the A* in the authority. Literals from survival.r10.ai_defaults
// (breeding foods F 5342-5347, tempt stop 2.5), survival.r10.idle_and_sun (chicken vy x0.6, egg 6000-11999, grazing).
namespace MobTestsLocal
{
    FIntVector SetBlock(FMobLab& L, const FIntVector& Cell, const TCHAR* Id)
    {
        const FFoundationBlockType* Block = L.A.World.FindBlock(Id);
        if (Block) L.A.World.Set(Cell, Block->GetDefaultState());
        return Cell;
    }

    FName BlockAt(FMobLab& L, const FIntVector& Cell)
    {
        return L.A.World.GetBlock(L.A.World.Get(Cell).State).Id;
    }
}

// A pig follows a player holding a carrot and stops close by; wheat (not its food) leaves it alone. Mutation: tempt
// stop distance 0 (it walks into the player).
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFoundationMobTemptTest, "Unnamed.Foundation.Mobs.Tempt",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FFoundationMobTemptTest::RunTest(const FString&)
{
    using namespace MobTestsLocal;
    using FoundationMobs::IsTemptItem;
    TestTrue(TEXT("cow wheat"), IsTemptItem(EFoundationMobType::Cow, TEXT("wheat")));
    TestTrue(TEXT("sheep wheat"), IsTemptItem(EFoundationMobType::Sheep, TEXT("wheat")));
    TestTrue(TEXT("pig beetroot"), IsTemptItem(EFoundationMobType::Pig, TEXT("beetroot")));
    TestFalse(TEXT("pig wheat"), IsTemptItem(EFoundationMobType::Pig, TEXT("wheat")));
    TestTrue(TEXT("chicken pitcher_pod"), IsTemptItem(EFoundationMobType::Chicken, TEXT("pitcher_pod")));
    TestFalse(TEXT("chicken wheat"), IsTemptItem(EFoundationMobType::Chicken, TEXT("wheat")));
    TestFalse(TEXT("zombie carrot"), IsTemptItem(EFoundationMobType::Zombie, TEXT("carrot")));
    FMobLab L;
    L.Stand(FVector(-600, 0, 0));
    const int32 Id = L.A.SpawnMob(EFoundationMobType::Pig, FVector::ZeroVector);
    L.Hold(TEXT("wheat"));
    L.Steps(5);
    TestEqual(TEXT("wheat does not tempt a pig"), L.A.FindMob(Id)->TemptedBy, 0);
    L.Hold(TEXT("carrot"));
    L.Steps(1);
    TestEqual(TEXT("a carrot tempts it"), L.A.FindMob(Id)->TemptedBy, L.Player);
    L.Steps(200);
    const FFoundationMob& Pig = *L.A.FindMob(Id);
    const double Distance = FVector::Dist(Pig.LocalCm, FVector(-600, 0, 0));
    TestTrue(
        *FString::Printf(TEXT("it stops about 2.5 blocks away (%.1f cm)"), Distance), Distance > 180 && Distance < 260);
    TestTrue(TEXT("it looks at the player"), Pig.LookTicks > 0);
    return true;
}

// A* in the authority: a cell's height from its collision (a full block 1, a slab 0.5, a fence's box leaves the cell
// upward, water avoided), and a pig tempted from behind a wall walks around it. Mutation: no path planning (straight
// steering stalls at the wall).
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFoundationMobPathCellsTest, "Unnamed.Foundation.Mobs.PathCells",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FFoundationMobPathCellsTest::RunTest(const FString&)
{
    using namespace MobTestsLocal;
    FMobLab L;
    TestEqual(TEXT("air"), L.A.GetPathCell(FIntVector(5, 5, 0)).Solid, 0.f);
    TestEqual(TEXT("the lab floor below z 0"), L.A.GetPathCell(FIntVector(5, 5, -1)).Solid, 1.f);
    TestEqual(TEXT("stone"), L.A.GetPathCell(SetBlock(L, FIntVector(6, 5, 0), TEXT("stone"))).Solid, 1.f);
    TestEqual(TEXT("a bottom slab"), L.A.GetPathCell(SetBlock(L, FIntVector(7, 5, 0), TEXT("oak_slab"))).Solid, .5f);
    TestTrue(
        TEXT("a fence reaches 1.5"), L.A.GetPathCell(SetBlock(L, FIntVector(8, 5, 0), TEXT("oak_fence"))).Solid > 1.4f);
    TestEqual(TEXT("above a fence is free"), L.A.GetPathCell(FIntVector(8, 5, 1)).Solid, 0.f);
    TestTrue(TEXT("water is avoided"), L.A.GetPathCell(SetBlock(L, FIntVector(9, 5, 0), TEXT("water"))).bAvoid);
    // A stone wall three high across x = 3, y -4..4; the pig at x 0.5 and the player behind it at x 7.5.
    for (int32 Y = -4; Y <= 4; ++Y)
        for (int32 Z = 0; Z < 3; ++Z)
            SetBlock(L, FIntVector(3, Y, Z), TEXT("stone"));
    L.Stand(FVector(750, 50, 0));
    L.Hold(TEXT("carrot"));
    const int32 Id = L.A.SpawnMob(EFoundationMobType::Pig, FVector(50, 50, 0));
    L.Steps(400);
    const FFoundationMob& Pig = *L.A.FindMob(Id);
    TestTrue(*FString::Printf(TEXT("the pig got past the wall (x %.0f)"), Pig.LocalCm.X), Pig.LocalCm.X > 400);
    TestTrue(TEXT("it planned paths"), L.A.MobPaths > 0);
    return true;
}

// A falling chicken's downward speed x0.6 before each move: from rest 0, -7.84, then -12.44992 (a cow: -15.5232).
// Mutation: factor 1.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFoundationMobChickenTest, "Unnamed.Foundation.Mobs.Chicken",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FFoundationMobChickenTest::RunTest(const FString&)
{
    using namespace MobTestsLocal;
    FFoundationMob Chicken, Cow;
    Chicken.Type = EFoundationMobType::Chicken;
    Chicken.LocalCm = Cow.LocalCm = FVector(0, 0, 1000);
    MoveAlone(Chicken, 2);
    MoveAlone(Cow, 2);
    TestEqual(TEXT("chicken vy after two steps"), Chicken.VelocityCm.Z, -12.44992, 1e-4);
    TestEqual(TEXT("cow vy after two steps"), Cow.VelocityCm.Z, -15.5232, 1e-4);
    // The wings: the flap speed rises by 1.2 a step in the air (clamped at 1); the roll is (sin(flap) + 1) x speed.
    FoundationMobs::Animate(Chicken);
    TestEqual(TEXT("flap speed in the air"), Chicken.FlapSpeed, 1.f);
    TestEqual(TEXT("flap after one airborne step"), Chicken.Flap, 1.8f, 1e-5f);
    TestEqual(TEXT("wing roll"), FoundationMobs::GetWingRoll(UE_PI / 2, .5f), 1.f, 1e-6f);
    return true;
}

// Eggs: the countdown is uniform 6000-11999 (Codex 02: 2,000 samples, 6001-11997); at 0 an egg lands as an item
// entity with the egg sound. Mutation: the countdown never runs.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFoundationMobEggTest, "Unnamed.Foundation.Mobs.Egg",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FFoundationMobEggTest::RunTest(const FString&)
{
    using namespace MobTestsLocal;
    FMobLab L;
    int32 Low = MAX_int32, High = 0;
    for (int32 Index = 0; Index < 2000; ++Index)
    {
        const int32 Id = L.A.SpawnMob(EFoundationMobType::Chicken, FVector(0, 0, 0));
        const FFoundationMob* Chicken = L.A.FindMob(Id);
        if (!TestNotNull(TEXT("chicken summoned"), Chicken)) return false;
        const int32 Ticks = Chicken->EggTicks;
        Low = FMath::Min(Low, Ticks);
        High = FMath::Max(High, Ticks);
    }
    L.A.ClearMobs();
    TestTrue(*FString::Printf(TEXT("egg timers %d-%d within 6000-11999 and spread"), Low, High),
        Low >= 6000 && High <= 11999 && Low < 6100 && High > 11900);
    const int32 Id = L.A.SpawnMob(EFoundationMobType::Chicken, FVector(0, 300, 0));
    L.A.FindMob(Id)->EggTicks = 3;
    L.Steps(2);
    TestEqual(TEXT("no egg before the countdown ends"), L.A.MobEggs, 0);
    L.Steps(1);
    TestEqual(TEXT("an egg at 0"), L.A.MobEggs, 1);
    const int32 Egg = L.A.Rules.FindItem(TEXT("egg"));
    TestTrue(TEXT("the egg is an item entity"),
        L.A.GetItemEntities().ContainsByPredicate(
            [Egg](const FFoundationItemEntity& Entity) { return Entity.Item == Egg; }));
    const int32 Next = L.A.FindMob(Id)->EggTicks;
    TestTrue(TEXT("a new countdown"), Next >= 6000 && Next <= 11999);
    return true;
}

// Grazing: the head's drop and nod by the countdown; a grazing sheep turns the grass block under it to dirt when 4
// steps are left (36 steps in), and short grass where it stands goes first. Mutation: eat at 0 left.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFoundationMobGrazeTest, "Unnamed.Foundation.Mobs.Graze",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FFoundationMobGrazeTest::RunTest(const FString&)
{
    using namespace MobTestsLocal;
    using namespace FoundationMobs;
    TestEqual(TEXT("drop at 40"), GetGrazeDrop(40), 0.f);
    TestEqual(TEXT("drop at 38"), GetGrazeDrop(38), .5f, 1e-6f);
    TestEqual(TEXT("drop at 20"), GetGrazeDrop(20), 1.f);
    TestEqual(TEXT("drop at 1"), GetGrazeDrop(1), .25f, 1e-6f);
    TestEqual(TEXT("pitch at 20"), GetGrazePitch(20, 0), UE_PI / 5 + .21991149f * FMath::Sin(.5f * 28.7f), 1e-5f);
    TestEqual(TEXT("pitch at 38"), GetGrazePitch(38, 0), UE_PI / 5, 1e-6f);
    TestEqual(TEXT("pitch when not grazing"), GetGrazePitch(0, .3f), .3f);
    FMobLab L;
    const FIntVector Grass = SetBlock(L, FIntVector(0, 0, 0), TEXT("grass_block"));
    const int32 Id = L.A.SpawnMob(EFoundationMobType::Sheep, FVector(50, 50, 100));
    L.Steps(1);
    L.A.FindMob(Id)->GrazeTicks = 40;
    L.Steps(35);
    TestEqual(TEXT("grass after 35 steps"), BlockAt(L, Grass), FName(TEXT("grass_block")));
    L.Steps(1);
    TestEqual(TEXT("dirt after 36"), BlockAt(L, Grass), FName(TEXT("dirt")));
    TestEqual(TEXT("one graze"), L.A.MobGrazes, 1);
    const FIntVector Tuft = SetBlock(L, FIntVector(0, 0, 1), TEXT("short_grass"));
    SetBlock(L, Grass, TEXT("grass_block"));
    L.Steps(4);
    L.A.FindMob(Id)->GrazeTicks = 40;
    L.Steps(36);
    TestEqual(TEXT("the short grass is eaten"), BlockAt(L, Tuft), FName(TEXT("air")));
    TestEqual(TEXT("the block below stays"), BlockAt(L, Grass), FName(TEXT("grass_block")));
    return true;
}
#endif
