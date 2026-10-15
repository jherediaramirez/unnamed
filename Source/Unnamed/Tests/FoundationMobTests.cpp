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
        // Mobs as they spawned (the Packs test records them).
        TArray<FFoundationMob> Spawned;

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
// tick 14; [hurt.overlay] red while dying; [survival2.r20.mob_death] the loot and experience drop on the killing hit,
// the corpse stays to death tick 19 and goes at 20. Mutation:
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
    TestEqual(TEXT("the loot drops on the killing hit"), L.A.MobsKilled, 1);
    TestTrue(TEXT("as item entities"), L.A.GetItemEntities().Num() > 0);
    TestTrue(TEXT("with its experience"), L.A.GetExperienceOrbs().Num() > 0);
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
    // A player's kill in the lab: items and orbs, on the killing hit [survival2.r20.mob_death] (later the player
    // picks them up).
    const int32 Id = L.Cow().Id;
    L.A.FindMob(Id)->Health.Health = 1;
    L.Hold(NAME_None);
    L.Attack();
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
    // [survival.r10.ai_defaults] FENCE: nothing stands on a fence (its box is 1.5 tall since T5 B2b).
    double Feet = 0;
    TestFalse(TEXT("no stand on a fence"),
        FoundationMobPath::GetStand(
            [&L](const FIntVector& Cell) { return L.A.GetPathCell(Cell); }, FIntVector(8, 5, 1), .9, Feet));
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
// Codex review 19 #3 [movement.knockback_resistance V:512; survival2.r20.mob_physics: 0.4 up from rest]: a grounded
// mob's vertical velocity after a hit is min(0.4, vy / 2 + power), whatever the power. Mutation: min(power, ...).
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFoundationMobKnockbackCapTest, "Unnamed.Foundation.Mobs.KnockbackCap",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FFoundationMobKnockbackCapTest::RunTest(const FString&)
{
    FVector Rest(0, 0, 0);
    FoundationMobs::ApplyKnockback(Rest, FVector2D(-1, 0), true, 40);
    TestEqual(TEXT("from vy 0: (-0.4, 0.4) (R20)"), Rest, FVector(-40, 0, 40));
    FVector Rising(0, 0, 30);
    FoundationMobs::ApplyKnockback(Rising, FVector2D(1, 0), true, 20);
    TestEqual(TEXT("half power, rising 0.3: 0.35"), Rising.Z, 35.);
    FVector Fast(0, 0, 100);
    FoundationMobs::ApplyKnockback(Fast, FVector2D(1, 0), true, 20);
    TestEqual(TEXT("capped at 0.4"), Fast.Z, 40.);
    FVector Airborne(0, 0, -20);
    FoundationMobs::ApplyKnockback(Airborne, FVector2D(1, 0), false, 40);
    TestEqual(TEXT("airborne keeps vy"), Airborne.Z, -20.);
    return true;
}

// Codex review 19 #1: mobs take the player's fall, lava and burning damage [fall.damage_thresholds V:324,
// hurt.lava_contact V:289]; the chicken takes no fall damage; water puts a fire out; a mob that dies burning drops
// cooked meat (is_on_fire); the loot drops on the killing blow [survival2.r20.mob_death]. Mutation: drop the chicken's
// immunity.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFoundationMobHazardsTest, "Unnamed.Foundation.Mobs.Hazards",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FFoundationMobHazardsTest::RunTest(const FString&)
{
    using namespace MobTestsLocal;
    {
        // A 10-block fall: 10 - 3 = 7 damage.
        FMobLab L;
        const int32 Cow = L.A.SpawnMob(EFoundationMobType::Cow, FVector(0, 0, 1000));
        const int32 Chicken = L.A.SpawnMob(EFoundationMobType::Chicken, FVector(300, 300, 1000));
        L.Steps(200);
        const FFoundationMob* C = L.A.FindMob(Cow);
        const FFoundationMob* K = L.A.FindMob(Chicken);
        if (!TestNotNull(TEXT("cow"), C) || !TestNotNull(TEXT("chicken"), K)) return false;
        TestTrue(TEXT("both landed"), C->bOnGround && K->bOnGround);
        TestEqual(TEXT("the cow took 7"), C->Health.Health, 3.f);
        TestEqual(TEXT("the chicken took none"), K->Health.Health, 4.f);
    }
    {
        // Lava: 4 on contact and 300 ticks of fire, only with foundation.Fire.
        FMobLab L;
        const FIntVector Lava = SetBlock(L, FIntVector(0, 0, 0), TEXT("lava"));
        TestEqual(TEXT("lava placed"), BlockAt(L, Lava), FName(TEXT("lava")));
        const int32 Id = L.A.SpawnMob(EFoundationMobType::Pig, FVector(50, 50, 0));
        L.A.Options.bFire = false;
        L.Steps(1);
        TestEqual(TEXT("no fire: no damage"), L.A.FindMob(Id)->Health.Health, 10.f);
        L.A.Options.bFire = true;
        L.Steps(1);
        const FFoundationMob* Pig = L.A.FindMob(Id);
        TestEqual(TEXT("lava hurts 4"), Pig->Health.Health, 6.f);
        TestTrue(TEXT("and sets it on fire"), Pig->Health.FireTicks >= 299);
        TestEqual(TEXT("the hurt overlay"), Pig->HurtTime, FoundationMobs::HurtTicks);
    }
    {
        // Burning to death: cooked beef, at the death itself.
        FMobLab L;
        L.A.Options.bFire = true;
        const int32 Id = L.A.SpawnMob(EFoundationMobType::Cow, FVector::ZeroVector);
        FFoundationMob& Cow = *L.A.FindMob(Id);
        Cow.Health.Health = 1;
        Cow.Health.FireTicks = 21;
        L.Steps(1);
        TestTrue(TEXT("alive at fire 20"), L.A.FindMob(Id) && L.A.FindMob(Id)->IsAlive());
        L.Steps(1);
        TestTrue(TEXT("burnt at fire 20"), L.A.FindMob(Id) && !L.A.FindMob(Id)->IsAlive());
        TestEqual(TEXT("dropped at the death"), L.A.MobsKilled, 1);
        const int32 Raw = L.A.Rules.FindItem(TEXT("beef")), Cooked = L.A.Rules.FindItem(TEXT("cooked_beef"));
        int32 RawCount = 0, CookedCount = 0;
        for (const FFoundationItemEntity& Entity : L.A.GetItemEntities())
        {
            RawCount += Entity.Item == Raw ? Entity.Count : 0;
            CookedCount += Entity.Item == Cooked ? Entity.Count : 0;
        }
        TestEqual(TEXT("no raw beef"), RawCount, 0);
        TestTrue(TEXT("cooked beef"), CookedCount >= 1);
    }
    {
        // Water puts the fire out.
        FMobLab L;
        L.A.Options.bFire = true;
        SetBlock(L, FIntVector(0, 0, 0), TEXT("water"));
        const int32 Id = L.A.SpawnMob(EFoundationMobType::Sheep, FVector(50, 50, 0));
        L.A.FindMob(Id)->Health.FireTicks = 100;
        L.Steps(1);
        TestEqual(TEXT("water puts it out"), L.A.FindMob(Id)->Health.FireTicks, 0);
    }
    return true;
}

// O9-O11 and Codex 02 B from DA_Mobs (the worldgen reference's natural_mob_spawns): plains keeps sheep 12, pig 10,
// chicken 10, cow 8 of its six entries (horse and donkey are not built), packs of 4; cherry grove pig 1-2, sheep 2-4;
// the chunk-generation chance 0.1 by default, snowy plains 0.07, badlands 0.03, wooded badlands 0.04; with our RNG the
// pick over 40,000 draws is each weight / 40 +- 0.01. Mutation: pick by entry count instead of weight.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFoundationMobSpawnTableTest, "Unnamed.Foundation.Mobs.SpawnTable",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FFoundationMobSpawnTableTest::RunTest(const FString&)
{
    using namespace MobTestsLocal;
    FMobLab L;
    const TMap<FName, FFoundationBiomeSpawns>& Spawns = L.A.Rules.BiomeSpawns;
    const FFoundationBiomeSpawns* Plains = Spawns.Find(TEXT("minecraft:plains"));
    if (!TestNotNull(TEXT("plains (run ImportMobs.py)"), Plains)) return false;
    const EFoundationMobType Order[] = {
        EFoundationMobType::Sheep, EFoundationMobType::Pig, EFoundationMobType::Chicken, EFoundationMobType::Cow};
    const int32 Weights[] = {12, 10, 10, 8};
    if (!TestEqual(TEXT("plains: four built entries"), Plains->Creatures.Num(), 4)) return false;
    for (int32 Index = 0; Index < 4; ++Index)
    {
        const FFoundationMobSpawn& Entry = Plains->Creatures[Index];
        TestEqual(*FString::Printf(TEXT("plains entry %d type"), Index), Entry.Type, Order[Index]);
        TestEqual(*FString::Printf(TEXT("plains entry %d weight"), Index), Entry.Weight, Weights[Index]);
        TestTrue(*FString::Printf(TEXT("plains entry %d pack 4"), Index), Entry.MinCount == 4 && Entry.MaxCount == 4);
    }
    TestEqual(TEXT("plains chance"), Plains->Chance, .1f);
    const auto Chance = [&Spawns](const TCHAR* Biome)
    {
        const FFoundationBiomeSpawns* Found = Spawns.Find(Biome);
        return Found ? Found->Chance : -1.f;
    };
    TestEqual(TEXT("snowy plains 0.07"), Chance(TEXT("minecraft:snowy_plains")), .07f);
    TestEqual(TEXT("badlands 0.03"), Chance(TEXT("minecraft:badlands")), .03f);
    TestEqual(TEXT("wooded badlands 0.04"), Chance(TEXT("minecraft:wooded_badlands")), .04f);
    TestEqual(TEXT("forest 0.1"), Chance(TEXT("minecraft:forest")), .1f);
    TestEqual(TEXT("snowy plains has no built creature"),
        Spawns.Contains(TEXT("minecraft:snowy_plains")) ? Spawns[TEXT("minecraft:snowy_plains")].Creatures.Num() : -1,
        0);
    if (const FFoundationBiomeSpawns* Cherry = Spawns.Find(TEXT("minecraft:cherry_grove")))
    {
        const FFoundationMobSpawn* Pig = Cherry->Creatures.FindByPredicate(
            [](const FFoundationMobSpawn& Entry) { return Entry.Type == EFoundationMobType::Pig; });
        const FFoundationMobSpawn* Sheep = Cherry->Creatures.FindByPredicate(
            [](const FFoundationMobSpawn& Entry) { return Entry.Type == EFoundationMobType::Sheep; });
        TestTrue(TEXT("cherry grove pig 1-2"), Pig && Pig->MinCount == 1 && Pig->MaxCount == 2);
        TestTrue(TEXT("cherry grove sheep 2-4"), Sheep && Sheep->MinCount == 2 && Sheep->MaxCount == 4);
        FRandomStream Random(7);
        TSet<int32> Sizes;
        for (int32 Draw = 0; Draw < 200; ++Draw)
            Sizes.Add(FoundationMobs::RollPackSize(*Sheep, Random));
        TestTrue(TEXT("sheep packs 2, 3 and 4 only"), Sizes.Num() == 3 && Sizes.Contains(2) && Sizes.Contains(4));
    }
    else
        AddError(TEXT("cherry grove missing"));
    FRandomStream Random(20262);
    int32 Counts[4] = {};
    constexpr int32 Draws = 40000;
    for (int32 Draw = 0; Draw < Draws; ++Draw)
        if (const FFoundationMobSpawn* Entry = FoundationMobs::PickSpawn(*Plains, Random))
            ++Counts[Entry - Plains->Creatures.GetData()];
    for (int32 Index = 0; Index < 4; ++Index)
        TestEqual(
            *FString::Printf(TEXT("pick share %d"), Index), double(Counts[Index]) / Draws, Weights[Index] / 40., .01);
    // The member offsets: -5 to 5 on each axis, triangular (0 the most common).
    int32 Offsets[11] = {};
    for (int32 Draw = 0; Draw < Draws; ++Draw)
        ++Offsets[FoundationMobs::RollPackOffset(Random) + 5];
    TestTrue(TEXT("offsets reach -5 and 5"), Offsets[0] > 0 && Offsets[10] > 0);
    TestTrue(TEXT("0 the most common"), Offsets[5] > Offsets[4] && Offsets[5] > Offsets[6]);
    return true;
}

namespace MobTestsLocal
{
    // A lab of columns -2..1 on each axis (cells -32..31) whose blocks are a grass floor (cells z 0) over the statics'
    // floor, with plains everywhere.
    void MakeGrassLab(FMobLab& L, uint64 Seed, const TCHAR* Top = TEXT("grass_block"))
    {
        L.A.World.BoundsMin = FIntVector(-32, -32, 0);
        L.A.World.BoundsMax = FIntVector(31, 31, 15);
        const FFoundationBlockType* Block = L.A.World.FindBlock(Top);
        for (int32 Y = L.A.World.BoundsMin.Y; Y <= L.A.World.BoundsMax.Y; ++Y)
            for (int32 X = L.A.World.BoundsMin.X; X <= L.A.World.BoundsMax.X; ++X)
                L.A.World.Set(FIntVector(X, Y, 0), Block->GetDefaultState());
        L.A.GetBiomeId = [](const FIntVector&) { return FName(TEXT("minecraft:plains")); };
        L.A.MobSpawnSeed = Seed;
        L.A.Options.bMobSpawn = true;
    }

    FString DescribeMobs(const FMobLab& L)
    {
        FString Text;
        for (const FFoundationMob& Mob : L.Spawned)
            Text += FString::Printf(TEXT("%d@%s "), int32(Mob.Type), *Mob.LocalCm.ToString());
        return Text;
    }
}

// Chunk-generation packs: each column (16 x 16) first seen loaded rolls once; with chance 1 every column of the lab
// spawns one pack of 4 inside itself, standing on the grass (feet at z 100); the same seed gives the same mobs, another
// seed others; chance 0, foundation.Mobs.Spawn 0, a biome without built creatures, stone instead of grass, or a roof
// over the grass spawn none. Mutation: skip the grass test.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFoundationMobPacksTest, "Unnamed.Foundation.Mobs.Packs",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FFoundationMobPacksTest::RunTest(const FString&)
{
    using namespace MobTestsLocal;
    const auto Run = [](uint64 Seed, float Chance, TFunctionRef<void(FMobLab&)> Setup)
    {
        TUniquePtr<FMobLab> L = MakeUnique<FMobLab>();
        MakeGrassLab(*L, Seed);
        if (FFoundationBiomeSpawns* Plains = L->A.Rules.BiomeSpawns.Find(TEXT("minecraft:plains")))
            Plains->Chance = Chance;
        Setup(*L);
        // 16 new columns a step: the ring's 441 are all seen within 28 steps. Each mob as it spawned (its first step
        // has not run yet: Age 0), before it strolls.
        for (int32 Step = 0; Step < 30; ++Step)
        {
            L->Steps(1);
            for (const FFoundationMob& Mob : L->A.GetMobs())
                if (Mob.Age == 0) L->Spawned.Add(Mob);
        }
        return L;
    };
    TUniquePtr<FMobLab> L = Run(1, 1, [](FMobLab&) {});
    TestEqual(TEXT("16 lab columns, 4 packs each"), L->A.MobsPackSpawned, 64);
    TestEqual(TEXT("16 packs"), L->A.MobPacks, 16);
    TestEqual(TEXT("the ring's 21 x 21 columns seen"), L->A.MobColumnsSeen, 441);
    {
        FMobLab One;
        MakeGrassLab(One, 1);
        One.Steps(1);
        TestEqual(TEXT("16 new columns a step"), One.A.MobColumnsSeen, 16);
    }
    TMap<FIntPoint, int32> PerColumn;
    bool bOnGrass = true, bBuilt = true;
    TestEqual(TEXT("every mob recorded at its spawn"), L->Spawned.Num(), 64);
    for (const FFoundationMob& Mob : L->Spawned)
    {
        ++PerColumn.FindOrAdd(FIntPoint(FoundationGrid::FloorDivide(FoundationGrid::FloorToCell(Mob.LocalCm.X), 16),
            FoundationGrid::FloorDivide(FoundationGrid::FloorToCell(Mob.LocalCm.Y), 16)));
        bOnGrass &= FMath::IsNearlyEqual(Mob.LocalCm.Z, 100., 1.);
        bBuilt &= int32(Mob.Type) <= int32(EFoundationMobType::Chicken);
    }
    TestEqual(TEXT("in 16 columns"), PerColumn.Num(), 16);
    for (const TPair<FIntPoint, int32>& Column : PerColumn)
        TestEqual(*FString::Printf(TEXT("4 in column %s"), *Column.Key.ToString()), Column.Value, 4);
    TestTrue(TEXT("standing on the grass"), bOnGrass);
    TestTrue(TEXT("farm animals only"), bBuilt);
    const FString First = DescribeMobs(*L);
    L->Steps(5);
    TestEqual(TEXT("each column rolls once"), L->A.MobPacks, 16);
    TestEqual(TEXT("the same seed, the same mobs"), DescribeMobs(*Run(1, 1, [](FMobLab&) {})), First);
    TestNotEqual(TEXT("another seed, other mobs"), DescribeMobs(*Run(2, 1, [](FMobLab&) {})), First);
    TestEqual(TEXT("chance 0"), Run(1, 0, [](FMobLab&) {})->A.GetMobs().Num(), 0);
    TestEqual(
        TEXT("spawning off"), Run(1, 1, [](FMobLab& Lab) { Lab.A.Options.bMobSpawn = false; })->A.GetMobs().Num(), 0);
    TestEqual(TEXT("snowy plains: no built creature"),
        Run(1, 1, [](FMobLab& Lab)
            { Lab.A.GetBiomeId = [](const FIntVector&) { return FName(TEXT("minecraft:snowy_plains")); }; })
            ->A.GetMobs()
            .Num(),
        0);
    TestEqual(TEXT("stone: none"),
        Run(1, 1, [](FMobLab& Lab) { MakeGrassLab(Lab, 1, TEXT("stone")); })->A.GetMobs().Num(), 0);
    TestEqual(TEXT("under a roof (no sky): none"),
        Run(1, 1,
            [](FMobLab& Lab)
            {
                const FFoundationBlockType* Stone = Lab.A.World.FindBlock(TEXT("stone"));
                for (int32 Y = Lab.A.World.BoundsMin.Y; Y <= Lab.A.World.BoundsMax.Y; ++Y)
                    for (int32 X = Lab.A.World.BoundsMin.X; X <= Lab.A.World.BoundsMax.X; ++X)
                        Lab.A.World.Set(FIntVector(X, Y, 5), Stone->GetDefaultState());
            })
            ->A.GetMobs()
            .Num(),
        0);
    return true;
}

// The A* budget rotates (20b1 fold): six mobs that all need a path get the four searches of the first step, then the
// two left first on the next. Mutation: no rotation (PathStart stays 0).
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFoundationMobPathBudgetTest, "Unnamed.Foundation.Mobs.PathBudget",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FFoundationMobPathBudgetTest::RunTest(const FString&)
{
    using namespace MobTestsLocal;
    FMobLab L;
    TArray<int32> Ids;
    for (int32 Index = 0; Index < 6; ++Index)
        Ids.Add(L.A.SpawnMob(EFoundationMobType::Cow, FVector(0, -500 + Index * 200, 0)));
    L.Steps(2);
    const auto NeedAll = [&L, &Ids]
    {
        for (const int32 Id : Ids)
        {
            FFoundationMob& Mob = *L.A.FindMob(Id);
            Mob.bMoving = true;
            Mob.TargetCm = Mob.LocalCm + FVector(600, 0, 0);
            Mob.bNeedPath = true;
            Mob.MoveTicks = 0;
        }
    };
    NeedAll();
    L.Steps(1);
    const auto Waiting = [&L, &Ids]
    {
        TArray<int32> Out;
        for (int32 Index = 0; Index < Ids.Num(); ++Index)
            if (L.A.FindMob(Ids[Index])->bNeedPath) Out.Add(Index);
        return Out;
    };
    TestEqual(TEXT("the first four planned"), Waiting(), TArray<int32>({4, 5}));
    // Everyone asks again: the two that waited go first.
    NeedAll();
    L.Steps(1);
    TestEqual(TEXT("then the two that waited and the first two"), Waiting(), TArray<int32>({2, 3}));
    return true;
}

// A ledge and a turn (20b1 fold): a cow walking to the far corner of a one-block ledge climbs it and turns, passing
// each waypoint within 0.5 block horizontally (heights ignored), and stops on the ledge. Mutation: ArriveCm 0 (the
// waypoints are never reached).
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFoundationMobLedgeTest, "Unnamed.Foundation.Mobs.Ledge",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FFoundationMobLedgeTest::RunTest(const FString&)
{
    using namespace MobTestsLocal;
    FMobLab L;
    for (int32 Y = 0; Y <= 5; ++Y)
        for (int32 X = 3; X <= 5; ++X)
            SetBlock(L, FIntVector(X, Y, 0), TEXT("stone"));
    const int32 Id = L.A.SpawnMob(EFoundationMobType::Cow, FVector(50, 50, 0));
    L.Steps(2);
    FFoundationMob& Mob = *L.A.FindMob(Id);
    Mob.bMoving = true;
    Mob.TargetCm = FVector(450, 550, 100);
    Mob.SpeedModifier = 1;
    Mob.bNeedPath = true;
    Mob.MoveTicks = 0;
    L.Steps(1);
    const TArray<FIntVector> Path = L.A.FindMob(Id)->Path;
    if (!TestTrue(TEXT("a path"), Path.Num() >= 3)) return false;
    TestTrue(TEXT("it climbs"), Path.Last().Z == 1);
    TArray<double> Nearest;
    Nearest.Init(1e9, Path.Num());
    for (int32 Step = 0; Step < 300 && L.A.FindMob(Id)->bMoving; ++Step)
    {
        L.Steps(1);
        const FVector At = L.A.FindMob(Id)->LocalCm;
        for (int32 Index = 0; Index < Path.Num(); ++Index)
            Nearest[Index] = FMath::Min(
                Nearest[Index], FVector2D(At.X - (Path[Index].X + .5) * 100, At.Y - (Path[Index].Y + .5) * 100).Size());
    }
    for (int32 Index = 0; Index < Path.Num(); ++Index)
        TestTrue(*FString::Printf(
                     TEXT("waypoint %d %s within 0.5 block (%.1f cm)"), Index, *Path[Index].ToString(), Nearest[Index]),
            Nearest[Index] <= 50);
    const FFoundationMob& Done = *L.A.FindMob(Id);
    TestFalse(TEXT("navigation done"), Done.bMoving);
    TestEqual(TEXT("on the ledge"), Done.LocalCm.Z, 100., 1.);
    TestTrue(TEXT("near the corner"), FVector2D(Done.LocalCm.X - 450, Done.LocalCm.Y - 550).Size() < 100);
    return true;
}
#endif
