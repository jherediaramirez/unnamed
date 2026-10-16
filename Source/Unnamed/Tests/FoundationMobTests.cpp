#include "Misc/AutomationTest.h"
#include "Dom/JsonObject.h"
#include "Engine/World.h"
#include "Foundation/FoundationAuthority.h"
#include "Foundation/FoundationBlockRegistry.h"
#include "Foundation/FoundationGrid.h"
#include "Foundation/FoundationInteractionSettings.h"
#include "Foundation/FoundationInteractionView.h"
#include "Foundation/FoundationMobAssets.h"
#include "Foundation/FoundationMobFarm.h"
#include "Foundation/FoundationMobView.h"
#include "Foundation/FoundationMobs.h"
#include "GameFramework/Actor.h"
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
        // 20c: the zombie is summonable too; 20c2 the spider.
        TestEqual(*FString::Printf(TEXT("%s spawnable"), *Kind.Id.ToString()), Kind.bSpawnable,
            Type < 4 || Type == int32(EFoundationMobType::Zombie) || Type == int32(EFoundationMobType::Spider));
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
    // 20b: the sheep's wool layer follows its main layer, with its own 64 x 32 texture; 20e1: then its undercoat.
    TArray<const FFoundationMobModelAsset*> Layers;
    Assets->FindLayers(TEXT("sheep"), Layers);
    TestTrue(TEXT("sheep layers main, wool, undercoat"),
        Layers.Num() == 3 && Layers[0]->Layer == TEXT("sheep#main") && Layers[1]->Layer == TEXT("sheep#wool") &&
            Layers[1]->TextureSize == FIntPoint(64, 32) && Layers[2]->Layer == TEXT("sheep#wool_undercoat"));
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
    // Codex review 23 #2: the whole edible_for_sheep tag. A fern on grass is eaten and the grass block stays; dry grass
    // on sand (no grass block) is still food. Mutation: only short_grass counts.
    const FIntVector Fern = SetBlock(L, Tuft, TEXT("fern"));
    L.Steps(4);
    L.A.FindMob(Id)->GrazeTicks = 40;
    L.Steps(36);
    TestEqual(TEXT("the fern is eaten"), BlockAt(L, Fern), FName(TEXT("air")));
    TestEqual(TEXT("the grass block under the fern stays"), BlockAt(L, Grass), FName(TEXT("grass_block")));
    SetBlock(L, Grass, TEXT("sand"));
    for (const TCHAR* Dry : {TEXT("short_dry_grass"), TEXT("tall_dry_grass")})
    {
        SetBlock(L, Tuft, Dry);
        L.Steps(4);
        const int32 Before = L.A.MobGrazes;
        L.A.FindMob(Id)->GrazeTicks = 40;
        L.Steps(36);
        TestEqual(FString::Printf(TEXT("%s on sand is eaten"), Dry), BlockAt(L, Tuft), FName(TEXT("air")));
        TestEqual(FString::Printf(TEXT("%s: one more graze"), Dry), L.A.MobGrazes, Before + 1);
        TestEqual(TEXT("the sand stays"), BlockAt(L, Grass), FName(TEXT("sand")));
    }
    const int32 Grazes = L.A.MobGrazes;
    L.Steps(4);
    L.A.FindMob(Id)->GrazeTicks = 40;
    L.Steps(36);
    TestEqual(TEXT("bare sand is no food"), L.A.MobGrazes, Grazes);
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
    // 20c2 [survival.r10.spawn_registry; plains.json natural_mob_spawns monster]: every entry kept in order, the
    // unbuilt ones blank (SM decision): spider 100, zombie 90 of 515, packs of 4; the enderman 1-4.
    const int32 MonsterWeights[] = {100, 90, 5, 5, 100, 100, 100, 10, 5};
    if (TestEqual(TEXT("plains: nine monster entries"), Plains->Monsters.Num(), 9))
    {
        for (int32 Index = 0; Index < 9; ++Index)
        {
            const FFoundationMobSpawn& Entry = Plains->Monsters[Index];
            TestEqual(*FString::Printf(TEXT("monster %d weight"), Index), Entry.Weight, MonsterWeights[Index]);
            TestEqual(*FString::Printf(TEXT("monster %d blank"), Index), Entry.bBlank, Index >= 2);
        }
        TestTrue(TEXT("spider first, packs of 4"),
            Plains->Monsters[0].Type == EFoundationMobType::Spider && Plains->Monsters[0].MinCount == 4 &&
                Plains->Monsters[0].MaxCount == 4);
        TestEqual(TEXT("zombie second"), Plains->Monsters[1].Type, EFoundationMobType::Zombie);
        TestTrue(TEXT("enderman 1-4"), Plains->Monsters[7].MinCount == 1 && Plains->Monsters[7].MaxCount == 4);
        int32 Spiders = 0, Zombies = 0, Blanks = 0;
        for (int32 Draw = 0; Draw < Draws; ++Draw)
            if (const FFoundationMobSpawn* Entry = FoundationMobs::PickSpawn(Plains->Monsters, Random))
            {
                Blanks += Entry->bBlank ? 1 : 0;
                Spiders += !Entry->bBlank && Entry->Type == EFoundationMobType::Spider ? 1 : 0;
                Zombies += !Entry->bBlank && Entry->Type == EFoundationMobType::Zombie ? 1 : 0;
            }
        TestEqual(TEXT("spider share 100 / 515"), double(Spiders) / Draws, 100 / 515., .01);
        TestEqual(TEXT("zombie share 90 / 515"), double(Zombies) / Draws, 90 / 515., .01);
        TestEqual(TEXT("blank share 325 / 515"), double(Blanks) / Draws, 325 / 515., .01);
    }
    return true;
}

namespace MobTestsLocal
{
    // A lab of columns -2..1 on each axis (cells -32..31) whose blocks are a grass floor (cells z 0) over the statics'
    // floor, with plains everywhere.
    void MakeGrassLab(FMobLab& L, uint64 Seed, const TCHAR* Top = TEXT("grass_block"), int32 Half = 32)
    {
        L.A.World.BoundsMin = FIntVector(-Half, -Half, 0);
        L.A.World.BoundsMax = FIntVector(Half - 1, Half - 1, 15);
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
    // 20c2: the dark under the roof spawns monsters; no creature pack.
    const auto Creatures = [](const FMobLab& Lab)
    {
        int32 Count = 0;
        for (const FFoundationMob& Mob : Lab.A.GetMobs())
            Count += FoundationMobs::IsCreature(Mob.Type) ? 1 : 0;
        return Count;
    };
    TestEqual(TEXT("under a roof (no sky): no creature"),
        Creatures(*Run(1, 1,
            [](FMobLab& Lab)
            {
                const FFoundationBlockType* Stone = Lab.A.World.FindBlock(TEXT("stone"));
                for (int32 Y = Lab.A.World.BoundsMin.Y; Y <= Lab.A.World.BoundsMax.Y; ++Y)
                    for (int32 X = Lab.A.World.BoundsMin.X; X <= Lab.A.World.BoundsMax.X; ++X)
                        Lab.A.World.Set(FIntVector(X, Y, 5), Stone->GetDefaultState());
            })),
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

namespace MobTestsLocal
{
    // A 192 x 192 grass lab (columns -6..5) with the chunk-generation packs off, for the creature cycle; the player
    // stands at cell (-2, 0).
    TUniquePtr<FMobLab> MakeCycleLab(uint64 Seed)
    {
        TUniquePtr<FMobLab> L = MakeUnique<FMobLab>();
        MakeGrassLab(*L, Seed, TEXT("grass_block"), 96);
        if (FFoundationBiomeSpawns* Plains = L->A.Rules.BiomeSpawns.Find(TEXT("minecraft:plains"))) Plains->Chance = 0;
        return L;
    }

    // Steps, recording each mob as it spawned (Age 0).
    void StepRecording(FMobLab& L, int32 Count)
    {
        for (int32 Step = 0; Step < Count; ++Step)
        {
            L.Steps(1);
            for (const FFoundationMob& Mob : L.A.GetMobs())
                if (Mob.Age == 0) L.Spawned.Add(Mob);
        }
    }

    int32 CountCreatures(const FMobLab& L)
    {
        int32 Count = 0;
        for (const FFoundationMob& Mob : L.A.GetMobs())
            Count += Mob.IsAlive() && FoundationMobs::IsCreature(Mob.Type) ? 1 : 0;
        return Count;
    }
}

// Survival T6 20b2b, the creature cycle [survival2.r20 population runs; survival.r10.spawning_rules S 5472; Codex 02
// A, B]: the cap floor(10 x eligible / 289); nothing before step 400, a cycle at 400 that fills the lab to the cap (a
// column's packs can overshoot by at most ClusterMax - 1), none at 800 while at the cap; every spawn at least 24 blocks
// from the player; at most 4 a column; the same seed, the same mobs; foundation.Mobs.Spawn 0, none. Mutations: no
// minimum distance; no cluster limit; cycle every 399; the cap from 10 x eligible / 288.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFoundationMobCycleTest, "Unnamed.Foundation.Mobs.Cycle",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FFoundationMobCycleTest::RunTest(const FString&)
{
    using namespace MobTestsLocal;
    using namespace FoundationMobs;
    TestEqual(TEXT("cap over 289 columns"), GetCreatureCap(289), 10);
    TestEqual(TEXT("cap over 288 columns"), GetCreatureCap(288), 9);
    TestEqual(TEXT("cap over 144 columns"), GetCreatureCap(144), 4);
    TestEqual(TEXT("cap over 578 columns (two players apart)"), GetCreatureCap(578), 20);
    TestEqual(TEXT("cap over none"), GetCreatureCap(0), 0);

    TUniquePtr<FMobLab> L = MakeCycleLab(1);
    StepRecording(*L, CreatureCycleTicks - 1);
    TestEqual(TEXT("no cycle before step 400"), L->A.MobCycles, 0);
    TestEqual(TEXT("no mob before step 400"), L->A.GetMobs().Num(), 0);
    StepRecording(*L, 1);
    TestEqual(TEXT("a cycle at step 400"), L->A.MobCycles, 1);
    TestEqual(TEXT("289 eligible columns (the lab's storage counts every column loaded)"), L->A.MobCycleEligible, 289);
    TestEqual(TEXT("cap 10"), L->A.MobCycleCap, 10);
    const int32 First = CountCreatures(*L);
    TestTrue(*FString::Printf(TEXT("filled to the cap 10 (overshoot under 4): %d"), First),
        First >= 10 && First <= 10 + ClusterMax - 1);
    TestEqual(TEXT("all from the cycle"), L->A.MobsCycleSpawned, First);
    const FVector Player(-2, 0, 0);
    double Nearest = TNumericLimits<double>::Max();
    for (const FFoundationMob& Mob : L->Spawned)
        Nearest = FMath::Min(Nearest, FVector::Dist(Mob.LocalCm / 100, Player));
    TestTrue(*FString::Printf(TEXT("every spawn at least 24 blocks away (nearest %.2f)"), Nearest), Nearest >= 24);
    const FString Mobs = DescribeMobs(*L);
    StepRecording(*L, CreatureCycleTicks);
    TestEqual(TEXT("at the cap at step 800: no cycle"), L->A.MobCycles, 1);
    TestEqual(TEXT("at the cap: no new mob"), L->A.GetMobs().Num(), First);
    {
        TUniquePtr<FMobLab> Again = MakeCycleLab(1);
        StepRecording(*Again, CreatureCycleTicks);
        TestEqual(TEXT("the same seed, the same mobs"), DescribeMobs(*Again), Mobs);
        TUniquePtr<FMobLab> Other = MakeCycleLab(2);
        StepRecording(*Other, CreatureCycleTicks);
        TestNotEqual(TEXT("another seed, other mobs"), DescribeMobs(*Other), Mobs);
        TUniquePtr<FMobLab> Off = MakeCycleLab(1);
        Off->A.Options.bMobSpawn = false;
        Off->Steps(CreatureCycleTicks);
        TestEqual(TEXT("foundation.Mobs.Spawn 0: no cycle"), Off->A.MobCycles, 0);
        TestEqual(TEXT("foundation.Mobs.Spawn 0: no mob"), Off->A.GetMobs().Num(), 0);
    }
    {
        // One column's rounds: never more than ClusterMax; the limit is reached; none nearer than 24 blocks or
        // farther than 128 from every player.
        TUniquePtr<FMobLab> C = MakeCycleLab(3);
        const FVector Far[] = {FVector(-40, -40, 0)};
        int32 Most = 0, Total = 0;
        for (int32 Cycle = 0; Cycle < 200; ++Cycle)
        {
            const int32 Spawned = C->A.SpawnCycleColumn(FIntPoint(3, 3), Far, Cycle);
            Most = FMath::Max(Most, Spawned);
            Total += Spawned;
        }
        TestEqual(TEXT("a column spawns at most 4, and reaches 4"), Most, ClusterMax);
        TestTrue(TEXT("columns spawn"), Total > 0);
        const FVector Inside[] = {FVector(56, 56, 0)};
        const FVector Away[] = {FVector(56 + 200, 56, 0)};
        int32 Near = 0, Distant = 0;
        for (int32 Cycle = 0; Cycle < 50; ++Cycle)
        {
            Near += C->A.SpawnCycleColumn(FIntPoint(3, 3), Inside, Cycle);
            Distant += C->A.SpawnCycleColumn(FIntPoint(3, 3), Away, Cycle);
        }
        TestEqual(TEXT("a player in the column: none within 24 blocks"), Near, 0);
        TestEqual(TEXT("a player 200 blocks away: none beyond 128"), Distant, 0);
    }
    return true;
}

// A sanity band, not a gate: a plains lab over 12000 steps (30 cycles) against R20's plains_day creature arrivals
// (sheep 7, chicken 1, besides horses and a donkey we do not build). Logged; it only checks the cap holds.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFoundationMobCycleLongRunTest, "Unnamed.Foundation.Mobs.CycleLongRun",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FFoundationMobCycleLongRunTest::RunTest(const FString&)
{
    using namespace MobTestsLocal;
    TUniquePtr<FMobLab> L = MakeCycleLab(7);
    const double Start = FPlatformTime::Seconds();
    StepRecording(*L, 12000);
    int32 ByType[int32(EFoundationMobType::Count)] = {};
    for (const FFoundationMob& Mob : L->Spawned)
        ++ByType[int32(Mob.Type)];
    AddInfo(FString::Printf(TEXT("12000 steps in %.1f s: cycles %d, arrivals cow %d pig %d sheep %d chicken %d; "
                                 "R20 plains_day sheep 7 chicken 1"),
        FPlatformTime::Seconds() - Start, L->A.MobCycles, ByType[0], ByType[1], ByType[2], ByType[3]));
    TestTrue(TEXT("arrivals"), L->Spawned.Num() > 0);
    TestTrue(TEXT("the cap holds (10, overshoot under 4)"), CountCreatures(*L) <= 10 + FoundationMobs::ClusterMax - 1);
    return true;
}

// Survival T6 20c1: the zombie's data. value-swaps.md T6 [mobs.base_attributes, catalog.entities]: 20 health, 0.23,
// attack 3, armor 2, 0.6 x 1.95 (eye 1.74); [survival.r10.ai_rules] follow range 35; [survival.r10.ai_defaults
// S 5388] look distance 8; en_us.json "Zombie". Its loot (entities/zombie.json, the vehicle pool never passes):
// rotten flesh 0-2 each a third; the rare pool only on a player's kill, 2.5%, iron ingot, carrot and potato a third
// each (a weighted choice, not the first that passes); a burning zombie's potato is baked; never red mushroom.
// Mutations: the rare pool as ordered alternatives (only iron ingots); the zombie's attack 2.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFoundationMobZombieTest, "Unnamed.Foundation.Mobs.Zombie",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FFoundationMobZombieTest::RunTest(const FString&)
{
    using namespace MobTestsLocal;
    const FFoundationMobKind& Kind = FoundationMobs::GetKind(EFoundationMobType::Zombie);
    TestEqual(TEXT("health"), Kind.MaxHealth, 20.f);
    TestEqual(TEXT("movement"), Kind.Speed, .23f);
    TestEqual(TEXT("armor"), Kind.Armor, 2.f);
    TestEqual(TEXT("attack"), Kind.AttackDamage, 3.f);
    TestEqual(TEXT("box"), FoundationMobs::GetBox(EFoundationMobType::Zombie, FVector::ZeroVector).GetSize(),
        FVector(60, 60, 195));
    TestEqual(TEXT("eye"), Kind.EyeCm, 174.);
    TestEqual(TEXT("follow range"), Kind.FollowRangeCm, 3500.);
    TestEqual(TEXT("look distance"), Kind.LookCm, 800.);
    TestEqual(TEXT("name"), FString(Kind.Name), FString(TEXT("Zombie")));
    TestTrue(TEXT("a monster"),
        FoundationMobs::IsMonster(EFoundationMobType::Zombie) && !FoundationMobs::IsMonster(EFoundationMobType::Cow) &&
            !FoundationMobs::IsCreature(EFoundationMobType::Zombie));
    FMobLab L;
    const FFoundationLootTable* Table = L.A.Rules.EntityLootTables.Find(TEXT("zombie"));
    if (!TestNotNull(TEXT("entities/zombie from DA_Mobs"), Table)) return false;
    const int32 Flesh = L.A.Rules.FindItem(TEXT("rotten_flesh")), Iron = L.A.Rules.FindItem(TEXT("iron_ingot")),
                Carrot = L.A.Rules.FindItem(TEXT("carrot")), Potato = L.A.Rules.FindItem(TEXT("potato")),
                Baked = L.A.Rules.FindItem(TEXT("baked_potato")), Mushroom = L.A.Rules.FindItem(TEXT("red_mushroom"));
    TestTrue(TEXT("the drops are items"),
        Flesh >= 0 && Iron >= 0 && Carrot >= 0 && Potato >= 0 && Baked >= 0 && Mushroom >= 0);
    FRandomStream Random(263);
    constexpr int32 Rolls = 30000;
    int32 FleshCounts[3] = {}, Rare = 0, RareBy[3] = {}, Mushrooms = 0, Unkilled = 0;
    for (int32 Roll = 0; Roll < Rolls; ++Roll)
    {
        FFoundationLootContext Context{L.A.Rules.Hand};
        Context.bKilledByPlayer = true;
        TArray<FFoundationLootDrop> Drops;
        FoundationLoot::Roll(*Table, L.A.Rules.LootPredicates, Context, Random, Drops);
        int32 FleshCount = 0;
        for (const FFoundationLootDrop& Drop : Drops)
        {
            FleshCount += Drop.Item == Flesh ? Drop.Count : 0;
            Mushrooms += Drop.Item == Mushroom;
            const int32 Which = Drop.Item == Iron ? 0 : Drop.Item == Carrot ? 1 : Drop.Item == Potato ? 2 : -1;
            if (Which >= 0)
            {
                ++Rare;
                ++RareBy[Which];
            }
        }
        if (FleshCount <= 2) ++FleshCounts[FleshCount];
        FFoundationLootContext NotPlayer{L.A.Rules.Hand};
        TArray<FFoundationLootDrop> Plain;
        FoundationLoot::Roll(*Table, L.A.Rules.LootPredicates, NotPlayer, Random, Plain);
        for (const FFoundationLootDrop& Drop : Plain)
            Unkilled += Drop.Item == Iron || Drop.Item == Carrot || Drop.Item == Potato;
    }
    for (int32 Count = 0; Count < 3; ++Count)
        TestEqual(*FString::Printf(TEXT("rotten flesh %d"), Count), FleshCounts[Count] / double(Rolls), 1 / 3., .01);
    TestEqual(TEXT("the rare pool 2.5%"), Rare / double(Rolls), .025, .005);
    for (int32 Which = 0; Which < 3; ++Which)
        TestEqual(*FString::Printf(TEXT("rare drop %d a third"), Which), RareBy[Which] / double(FMath::Max(Rare, 1)),
            1 / 3., .06);
    TestEqual(TEXT("no red mushroom (nothing rides)"), Mushrooms, 0);
    TestEqual(TEXT("no rare drop without a player's kill"), Unkilled, 0);
    bool bBaked = false, bRawWhileBurning = false;
    for (int32 Roll = 0; Roll < 20000 && !bBaked; ++Roll)
    {
        FFoundationLootContext Context{L.A.Rules.Hand};
        Context.bKilledByPlayer = Context.bOnFire = true;
        TArray<FFoundationLootDrop> Drops;
        FoundationLoot::Roll(*Table, L.A.Rules.LootPredicates, Context, Random, Drops);
        for (const FFoundationLootDrop& Drop : Drops)
        {
            bBaked |= Drop.Item == Baked;
            bRawWhileBurning |= Drop.Item == Potato;
        }
    }
    TestTrue(TEXT("a burning zombie's potato is baked"), bBaked && !bRawWhileBurning);
    return true;
}

namespace MobTestsLocal
{
    // A zombie at the origin and the lab's player 2 blocks away on -X (a survival player of 180 x 60 cm).
    int32 SpawnZombie(FMobLab& L, const FVector& At = FVector::ZeroVector)
    {
        return L.A.SpawnMob(EFoundationMobType::Zombie, At);
    }
}

// Survival T6 20c1: the zombie's melee [survival.r10.ai_defaults S 5392-5398: attackInterval 20, speedModifier 1;
// mobs.base_attributes: attack 3]. It finds the survival player 2 blocks away, walks up and hits it; standing still in
// reach it hits every 20 steps; each hit is queued for the player's owner with 3 damage, the name and where it
// stood. A creative player is never targeted; a wall in between hides the player. Mutations: interval 10; no line of
// sight test (it targets through the wall); the creative player targeted.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFoundationMobZombieMeleeTest, "Unnamed.Foundation.Mobs.ZombieMelee",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FFoundationMobZombieMeleeTest::RunTest(const FString&)
{
    using namespace MobTestsLocal;
    {
        FMobLab L;
        const int32 Id = SpawnZombie(L);
        TArray<int64> HitSteps;
        TArray<FFoundationMobHit> Hits;
        for (int32 Step = 0; Step < 200; ++Step)
        {
            L.Steps(1);
            for (const FFoundationMobHit& Hit : L.A.TakeMobHits(L.Player))
            {
                HitSteps.Add(L.StepIndex);
                Hits.Add(Hit);
            }
        }
        const FFoundationMob* Zombie = L.A.FindMob(Id);
        if (!TestNotNull(TEXT("the zombie"), Zombie)) return false;
        TestEqual(TEXT("it targets the player"), Zombie->Target, L.Player);
        if (!TestTrue(TEXT("it hit the player"), HitSteps.Num() >= 3)) return false;
        TestEqual(TEXT("3 damage"), Hits[0].Damage, 3.f);
        TestEqual(TEXT("named"), Hits[0].Attacker, FString(TEXT("Zombie")));
        TestTrue(TEXT("from where it stood"), FVector::Dist2D(Hits[0].FromCm, L.P().Facts.FeetCm) < 200);
        for (int32 Index = 1; Index < HitSteps.Num(); ++Index)
            TestEqual(*FString::Printf(TEXT("hit %d 20 steps after the last"), Index),
                HitSteps[Index] - HitSteps[Index - 1], int64(20));
        TestEqual(TEXT("counted"), L.A.MobMeleeHits, HitSteps.Num());
    }
    {
        FMobLab L;
        FFoundationPlayerFacts Facts = L.P().Facts;
        Facts.Mode = EFoundationPlayerMode::Creative;
        L.A.SetFacts(L.Player, Facts);
        const int32 Id = SpawnZombie(L);
        L.Steps(200);
        TestEqual(TEXT("a creative player is not targeted"), L.A.FindMob(Id)->Target, 0);
        TestEqual(TEXT("nor hit"), L.A.TakeMobHits(L.Player).Num(), 0);
    }
    {
        // A stone wall across the lab between them, three blocks high (above the zombie's eyes and the player's).
        FMobLab L;
        for (int32 Y = -20; Y <= 19; ++Y)
            for (int32 Z = 0; Z < 3; ++Z)
                SetBlock(L, FIntVector(-1, Y, Z), TEXT("stone"));
        const int32 Id = SpawnZombie(L, FVector(150, 0, 0));
        L.Steps(200);
        TestEqual(TEXT("behind a wall: no target"), L.A.FindMob(Id)->Target, 0);
    }
    return true;
}

// Survival T6 20c1: targeting as a pure rule. The search runs on even steps with a 1 in 5 roll [S 5398: randomInterval
// 5]; a player inside the follow range (35 blocks) is found, one just outside is not; an unseen target is dropped after
// 60 steps unseen [unseenMemoryTicks 60]; a hit from a player makes it the target (HurtByTargetGoal). Mutations: range
// 16; memory 20.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFoundationMobTargetTest, "Unnamed.Foundation.Mobs.Target",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FFoundationMobTargetTest::RunTest(const FString&)
{
    FFoundationMob Mob;
    Mob.Type = EFoundationMobType::Zombie;
    Mob.Health.MaxHealth = Mob.Health.Health = 20;
    Mob.Random.Initialize(5);
    bool bSee = true;
    const auto CanSee = [&bSee](const FVector&, const FVector&) { return bSee; };
    FFoundationMobPlayer Far;
    Far.Id = 7;
    Far.FeetCm = FVector(3510, 0, 0);
    Far.EyeCm = Far.FeetCm + FVector(0, 0, 162);
    for (int32 Step = 0; Step < 400; ++Step, ++Mob.Age)
        FoundationMobs::ThinkHostile(Mob, FVector::ZeroVector, MakeArrayView(&Far, 1), CanSee);
    TestEqual(TEXT("35.1 blocks: out of range"), Mob.Target, 0);
    FFoundationMobPlayer Near = Far;
    Near.FeetCm = FVector(3450, 0, 0);
    Near.EyeCm = Near.FeetCm + FVector(0, 0, 162);
    int32 Found = -1;
    for (int32 Step = 0; Step < 400 && Found < 0; ++Step, ++Mob.Age)
    {
        FoundationMobs::ThinkHostile(Mob, FVector::ZeroVector, MakeArrayView(&Near, 1), CanSee);
        if (Mob.Target == Near.Id) Found = Step;
    }
    TestTrue(TEXT("34.5 blocks: found"), Found >= 0);
    // Out of sight: kept for 60 unseen steps, dropped after.
    bSee = false;
    int32 Kept = 0;
    for (int32 Step = 0; Step < 100 && Mob.Target != 0; ++Step, ++Mob.Age)
    {
        FoundationMobs::ThinkHostile(Mob, FVector::ZeroVector, MakeArrayView(&Near, 1), CanSee);
        Kept += Mob.Target != 0;
    }
    TestEqual(TEXT("kept 60 steps unseen"), Kept, 60);
    // A player's hit: that player is the target at once.
    FoundationMobs::OnHurt(Mob, FVector::ZeroVector, Near.FeetCm, true, true, Near.Id);
    TestEqual(TEXT("hurt by a player: its target"), Mob.Target, Near.Id);
    return true;
}

// Survival T6 20c1: the zombie's sun burn [survival.r10.idle_and_sun S 5457]. The brightness curve f / (4 - 3f): 1 at
// light 15, 4/15 -> 1/12; at brightness 1 the roll U x 30 < 1.2 passes 4% of the time. In the lab under a burning day
// (sky light 15) it catches fire for 160 steps; not with monsters_burn off, at night (sky 4), under a roof, or with
// foundation.Fire 0. Mutations: the roll's factor 30 -> 3; the roof ignored.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFoundationMobSunBurnTest, "Unnamed.Foundation.Mobs.SunBurn",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FFoundationMobSunBurnTest::RunTest(const FString&)
{
    using namespace MobTestsLocal;
    TestEqual(TEXT("brightness 15"), FoundationMobs::GetBrightness(15), 1.f);
    TestEqual(TEXT("brightness 4"), FoundationMobs::GetBrightness(4), 1.f / 12.f, 1e-6f);
    TestEqual(TEXT("brightness 0"), FoundationMobs::GetBrightness(0), 0.f);
    FFoundationMob Roller;
    Roller.Random.Initialize(263);
    int32 Passes = 0;
    for (int32 Roll = 0; Roll < 30000; ++Roll)
        Passes += FoundationMobs::RollSunBurn(Roller, 1);
    TestEqual(TEXT("4% at brightness 1"), Passes / 30000., .04, .005);
    const auto Burned = [](float Level, bool bBurns, bool bFire, bool bRoof)
    {
        FMobLab L;
        L.A.Options.bFire = bFire;
        L.A.GetSkyFacts = [Level, bBurns](float& OutLevel, bool& bOutBurns)
        {
            OutLevel = Level;
            bOutBurns = bBurns;
        };
        // A creative player (no target to chase); the zombie held in place by a pen two blocks high.
        FFoundationPlayerFacts Facts = L.P().Facts;
        Facts.Mode = EFoundationPlayerMode::Creative;
        L.A.SetFacts(L.Player, Facts);
        const FVector At(1500, 1500, 0);
        for (int32 X = 14; X <= 16; ++X)
            for (int32 Y = 14; Y <= 16; ++Y)
                for (int32 Z = 0; Z < 2; ++Z)
                    if (X != 15 || Y != 15) SetBlock(L, FIntVector(X, Y, Z), TEXT("stone"));
        if (bRoof) SetBlock(L, FIntVector(15, 15, 5), TEXT("stone"));
        const int32 Id = L.A.SpawnMob(EFoundationMobType::Zombie, At + FVector(50, 50, 0));
        int32 MaxFire = 0;
        for (int32 Step = 0; Step < 600 && MaxFire == 0; ++Step)
        {
            L.Steps(1);
            if (const FFoundationMob* Zombie = L.A.FindMob(Id)) MaxFire = Zombie->Health.FireTicks;
        }
        return MaxFire;
    };
    const int32 Fire = Burned(15, true, true, false);
    TestTrue(TEXT("noon: it catches fire for 8 s"), Fire >= 159 && Fire <= 160);
    TestEqual(TEXT("monsters_burn off"), Burned(15, false, true, false), 0);
    TestEqual(TEXT("night sky 4"), Burned(4, true, true, false), 0);
    TestEqual(TEXT("under a roof"), Burned(15, true, true, true), 0);
    TestEqual(TEXT("foundation.Fire 0"), Burned(15, true, false, false), 0);
    return true;
}

// Survival T6 20c1: a mob's hit on the player's vitals [FFoundationVitals::Hurt with the attacker; the difficulty
// rules]: Normal 3, Easy min(3 / 2 + 1, 3) = 2.5, Hard 4.5, Peaceful none; a death by it reads "Player was slain by
// Zombie" (death.attack.mob); without a name it stays death.attack.generic, as before. The knockback the owner applies
// is the mobs' [survival2.r20.mob_physics: grounded from rest 0.4 away and 0.4 up]. Mutation: drop the name.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFoundationMobPlayerHitTest, "Unnamed.Foundation.Mobs.PlayerHit",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FFoundationMobPlayerHitTest::RunTest(const FString&)
{
    const EFoundationDifficulty Difficulties[] = {EFoundationDifficulty::Normal, EFoundationDifficulty::Easy,
        EFoundationDifficulty::Hard, EFoundationDifficulty::Peaceful};
    const float Expected[] = {3.f, 2.5f, 4.5f, 0.f};
    for (int32 Index = 0; Index < 4; ++Index)
    {
        FFoundationVitals Vitals;
        Vitals.Difficulty = Difficulties[Index];
        Vitals.Hurt(EFoundationDamage::MobAttack, 3, TEXT("Zombie"));
        TestEqual(*FString::Printf(TEXT("difficulty %d"), Index), 20.f - Vitals.Health, Expected[Index]);
    }
    FFoundationVitals Vitals;
    Vitals.Health = 2;
    Vitals.Hurt(EFoundationDamage::MobAttack, 3, TEXT("Zombie"));
    TestEqual(TEXT("slain by a zombie"), Vitals.DeathMessage, FString(TEXT("Player was slain by Zombie")));
    TestEqual(TEXT("death.attack.mob"), Vitals.DeathKey, FString(TEXT("death.attack.mob")));
    FFoundationVitals Nameless;
    Nameless.Health = 2;
    Nameless.Hurt(EFoundationDamage::MobAttack, 3);
    TestEqual(TEXT("no name: generic, as before"), Nameless.DeathKey, FString(TEXT("death.attack.generic")));
    FVector Velocity = FVector::ZeroVector;
    FoundationMobs::ApplyKnockback(Velocity, FVector2D(-1, 0), true, FoundationMobs::KnockbackCm);
    TestEqual(TEXT("knocked 0.4 away and 0.4 up"), Velocity, FVector(-40, 0, 40));
    return true;
}
namespace MobTestsLocal
{
    // The cycle lab under a stone roof at z 5 (cells MinCell..MaxCell, the whole lab by default): dark, no sky.
    TUniquePtr<FMobLab> MakeDarkLab(
        uint64 Seed, FIntPoint MinCell = FIntPoint(-96, -96), FIntPoint MaxCell = FIntPoint(95, 95))
    {
        TUniquePtr<FMobLab> L = MakeCycleLab(Seed);
        const FFoundationBlockType* Stone = L->A.World.FindBlock(TEXT("stone"));
        for (int32 Y = MinCell.Y; Y <= MaxCell.Y; ++Y)
            for (int32 X = MinCell.X; X <= MaxCell.X; ++X)
                L->A.World.Set(FIntVector(X, Y, 5), Stone->GetDefaultState());
        return L;
    }

    // The day's sky as AVoxelWorld's hook reports it: gameplay/sky_light_level, no burning.
    void SetSky(FMobLab& L, float Level)
    {
        L.A.GetSkyFacts = [Level](float& OutLevel, bool& bOutBurns)
        {
            OutLevel = Level;
            bOutBurns = false;
        };
    }

    int32 CountMonsters(const FMobLab& L)
    {
        int32 Count = 0;
        for (const FFoundationMob& Mob : L.A.GetMobs())
            Count += Mob.IsAlive() && FoundationMobs::IsMonster(Mob.Type) ? 1 : 0;
        return Count;
    }
}

// Survival T6 20c2, the spider's data and motion. value-swaps.md T6 [mobs.base_attributes, catalog.entities]: 16
// health, 0.3, attack 2, 1.4 x 0.9 (eye 0.65); [survival.r10.death_roll] 180; [survival.r10.ai_defaults S 5447]
// stroll 0.8; summonable. [survival.r10.model_motion F 5264-5299] its legs: at speed 0 the rest pose, the yaw swing at
// speed 1 is 0.4 rad (rest 45 -> 67.917735 degrees), 0.2 at 0.5; the roll stays within the rest's 45. Mutations: yaw
// amplitude 0.5; the leap's vertical 0.42.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFoundationMobSpiderTest, "Unnamed.Foundation.Mobs.Spider",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FFoundationMobSpiderTest::RunTest(const FString&)
{
    using namespace MobTestsLocal;
    using namespace FoundationMobs;
    const FFoundationMobKind& Kind = GetKind(EFoundationMobType::Spider);
    TestEqual(TEXT("health"), Kind.MaxHealth, 16.f);
    TestEqual(TEXT("movement"), Kind.Speed, .3f);
    TestEqual(TEXT("attack"), Kind.AttackDamage, 2.f);
    TestEqual(TEXT("box"), GetBox(EFoundationMobType::Spider, FVector::ZeroVector).GetSize(), FVector(140, 140, 90));
    TestEqual(TEXT("eye"), Kind.EyeCm, 65.);
    TestEqual(TEXT("death roll"), Kind.DeathRollDegrees, 180.f);
    TestEqual(TEXT("stroll 0.8"), Kind.StrollSpeed, .8f);
    TestEqual(TEXT("the zombie strolls at 1"), GetKind(EFoundationMobType::Zombie).StrollSpeed, 1.f);
    TestTrue(TEXT("summonable"), Kind.bSpawnable);
    TestTrue(TEXT("the spider climbs, the zombie does not"),
        CanClimb(EFoundationMobType::Spider) && !CanClimb(EFoundationMobType::Zombie));
    float MaxYaw[4] = {}, MaxRoll[4] = {}, MinRoll[4] = {1, 1, 1, 1}, HalfYaw = 0, StillYaw = 0;
    for (int32 Pair = 0; Pair < 4; ++Pair)
        for (float Position = 0; Position < 20; Position += .01f)
        {
            const FVector2D Swing = GetSpiderLegSwing(Pair, Position, 1);
            MaxYaw[Pair] = FMath::Max(MaxYaw[Pair], FMath::Abs(Swing.X));
            MaxRoll[Pair] = FMath::Max(MaxRoll[Pair], Swing.Y);
            MinRoll[Pair] = FMath::Min(MinRoll[Pair], Swing.Y);
            HalfYaw = FMath::Max(HalfYaw, FMath::Abs(GetSpiderLegSwing(Pair, Position, .5f).X));
            StillYaw = FMath::Max(StillYaw, GetSpiderLegSwing(Pair, Position, 0).GetAbsMax());
        }
    for (int32 Pair = 0; Pair < 4; ++Pair)
    {
        TestEqual(*FString::Printf(TEXT("pair %d yaw swing 0.4"), Pair), MaxYaw[Pair], .4f, 1e-3f);
        TestTrue(*FString::Printf(TEXT("pair %d roll toward the rest, 0 to 0.4"), Pair),
            MinRoll[Pair] >= 0 && MaxRoll[Pair] <= .4f + 1e-4f && MaxRoll[Pair] > .39f);
    }
    TestEqual(TEXT("yaw 0.2 at speed 0.5"), HalfYaw, .2f, 1e-3f);
    TestEqual(TEXT("still at speed 0"), StillYaw, 0.f);
    TestEqual(TEXT("hind leg: rest 45 + swing = 67.917735 degrees"), 45.f + FMath::RadiansToDegrees(MaxYaw[0]),
        67.917735f, .05f);
    // The legs' rest pose is the model data's: the hind and front legs at 45 degrees yaw and roll.
    if (const UFoundationMobAssets* Assets = UFoundationMobAssets::LoadDefault())
    {
        const FFoundationMobModelAsset* Model = Assets->FindModel(TEXT("spider"));
        const FFoundationMobPartAsset* Hind = Model
            ? Model->Parts.FindByPredicate(
                  [](const FFoundationMobPartAsset& Part) { return Part.Path == TEXT("right_hind_leg"); })
            : nullptr;
        TestTrue(TEXT("right hind leg rest (0, 45, -45) degrees"),
            Hind && FMath::IsNearlyEqual(Hind->Rotation.Y, UE_PI / 4, 1e-4) &&
                FMath::IsNearlyEqual(Hind->Rotation.Z, -UE_PI / 4, 1e-4));
    }
    else
        AddError(TEXT("DA_Mobs missing (run ImportMobs.py)"));
    // The leap [S 5423]: on the ground, the target 3 blocks away: 0.4 up, pushed 0.4 toward it; not at 1.5 or 5 blocks.
    const auto Leaps = [](double Distance)
    {
        FFoundationMob Mob;
        Mob.Type = EFoundationMobType::Spider;
        Mob.Random.Initialize(5);
        FFoundationMobPlayer Player;
        Player.Id = 7;
        Player.FeetCm = FVector(Distance, 0, 0);
        Player.EyeCm = Player.FeetCm + FVector(0, 0, 162);
        Mob.Target = 7;
        for (int32 Step = 0; Step < 200; ++Step)
        {
            Mob.bOnGround = true;
            Mob.VelocityCm = FVector::ZeroVector;
            ThinkHostile(Mob, FVector::ZeroVector, {Player}, [](const FVector&, const FVector&) { return true; }, 0);
            if (Mob.VelocityCm.Z != 0) return Mob.VelocityCm;
        }
        return FVector::ZeroVector;
    };
    TestEqual(TEXT("leap at 3 blocks: 0.4 toward and 0.4 up"), Leaps(300), FVector(40, 0, 40));
    TestEqual(TEXT("no leap at 1.5 blocks"), Leaps(150), FVector::ZeroVector);
    TestEqual(TEXT("no leap at 5 blocks"), Leaps(500), FVector::ZeroVector);
    return true;
}

// The spider's targeting (Codex 02 C, provisional): it takes a target only below brightness 0.5 at its eyes (light 11
// and below: brightness(11) < 0.5 <= brightness(12)); in the light it forgets one now and then, in the dark it keeps
// it; SpiderAttackGoal follows an unseen target (a fresh path when the delay runs out), the zombie does not; both drop
// it after 60 unseen steps. Mutations: light ignored; the spider follows only a seen target.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFoundationMobSpiderTargetTest, "Unnamed.Foundation.Mobs.SpiderTarget",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FFoundationMobSpiderTargetTest::RunTest(const FString&)
{
    using namespace FoundationMobs;
    TestTrue(TEXT("light 11 dark, 12 bright"), GetBrightness(11) < .5f && GetBrightness(12) >= .5f);
    FFoundationMobPlayer Player;
    Player.Id = 7;
    Player.FeetCm = FVector(600, 0, 0);
    Player.EyeCm = Player.FeetCm + FVector(0, 0, 162);
    const auto Seen = [](const FVector&, const FVector&) { return true; };
    const auto Unseen = [](const FVector&, const FVector&) { return false; };
    const auto MakeMob = [](EFoundationMobType Type)
    {
        FFoundationMob Mob;
        Mob.Type = Type;
        Mob.Random.Initialize(11);
        Mob.bOnGround = true;
        return Mob;
    };
    const auto Acquires = [&](float Brightness)
    {
        FFoundationMob Mob = MakeMob(EFoundationMobType::Spider);
        for (int32 Step = 0; Step < 400 && Mob.Target == 0; ++Step, ++Mob.Age)
            ThinkHostile(Mob, FVector::ZeroVector, {Player}, Seen, Brightness);
        return Mob.Target;
    };
    TestEqual(TEXT("dark (light 11): targets"), Acquires(GetBrightness(11)), 7);
    TestEqual(TEXT("bright (light 12): never"), Acquires(GetBrightness(12)), 0);
    const auto Keeps = [&](float Brightness)
    {
        FFoundationMob Mob = MakeMob(EFoundationMobType::Spider);
        Mob.Target = 7;
        int32 Step = 0;
        for (; Step < 3000 && Mob.Target == 7; ++Step, ++Mob.Age)
            ThinkHostile(Mob, FVector::ZeroVector, {Player}, Seen, Brightness);
        return Step;
    };
    TestEqual(TEXT("dark: kept"), Keeps(0), 3000);
    const int32 Forgot = Keeps(1);
    TestTrue(*FString::Printf(TEXT("bright: forgotten (step %d)"), Forgot), Forgot < 3000);
    const auto Repaths = [&](EFoundationMobType Type)
    {
        FFoundationMob Mob = MakeMob(Type);
        Mob.Target = 7;
        Mob.bMoving = true;
        ThinkHostile(Mob, FVector::ZeroVector, {Player}, Unseen, 0);
        return Mob.RepathTicks > 0;
    };
    TestTrue(TEXT("the spider plans toward an unseen target"), Repaths(EFoundationMobType::Spider));
    TestFalse(TEXT("the zombie does not"), Repaths(EFoundationMobType::Zombie));
    FFoundationMob Mob = MakeMob(EFoundationMobType::Spider);
    Mob.Target = 7;
    for (int32 Step = 0; Step < 60; ++Step, ++Mob.Age)
        ThinkHostile(Mob, FVector::ZeroVector, {Player}, Unseen, 0);
    TestEqual(TEXT("60 unseen steps kept"), Mob.Target, 7);
    ThinkHostile(Mob, FVector::ZeroVector, {Player}, Unseen, 0);
    TestEqual(TEXT("the 61st drops it"), Mob.Target, 0);
    return true;
}

// Climbing (Codex 02 C, provisional): a spider walking into a 3-block wall goes up it and onto its top; a zombie stays
// on the floor. Mutation: no climbing.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFoundationMobSpiderClimbTest, "Unnamed.Foundation.Mobs.SpiderClimb",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FFoundationMobSpiderClimbTest::RunTest(const FString&)
{
    const TArray<FBox> Boxes = {
        FBox(FVector(-2000, -2000, -100), FVector(2000, 2000, 0)), FBox(FVector(100, -500, 0), FVector(400, 500, 300))};
    const auto Highest = [&Boxes](EFoundationMobType Type)
    {
        FFoundationMob Mob;
        Mob.Type = Type;
        Mob.LocalCm = FVector(100 - FoundationMobs::GetKind(Type).WidthCm / 2 - 5, 0, 0);
        Mob.bOnGround = true;
        double Top = 0;
        for (int32 Step = 0; Step < 120; ++Step)
        {
            FoundationMobs::Move(Mob, FVector::ZeroVector, Boxes, FVector2D(1, 0), false);
            Top = FMath::Max(Top, Mob.LocalCm.Z);
        }
        return TPair<double, double>(Top, Mob.LocalCm.X);
    };
    const TPair<double, double> Spider = Highest(EFoundationMobType::Spider);
    TestTrue(*FString::Printf(TEXT("the spider climbs onto the wall (top %.1f, x %.1f)"), Spider.Key, Spider.Value),
        Spider.Key >= 299 && Spider.Value > 100);
    TestTrue(TEXT("the zombie stays below"), Highest(EFoundationMobType::Zombie).Key < 61);
    return true;
}

// The light rule [mobs.spawn_light V:469-471]: the sky light above a random 0-31 fails, any block light fails, then
// the light must be at most a random 0-7. Shares over 40000 rolls. And the light proxy: block light is the max over
// emitters of emission minus the Manhattan distance (glowstone 15: 13 two cells away, 1 at fourteen, 0 at fifteen);
// under a roof no sky; on the open ground the raw sky 15 and the day's open-sky light. Mutations: 0-6 instead of 0-7;
// the block light test dropped; the distance as Chebyshev.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFoundationMobSpawnLightTest, "Unnamed.Foundation.Mobs.SpawnLight",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FFoundationMobSpawnLightTest::RunTest(const FString&)
{
    using namespace MobTestsLocal;
    using namespace FoundationMobs;
    const auto Share = [](int32 Sky, int32 Block, int32 Light)
    {
        FRandomStream Random(20262);
        constexpr int32 Rolls = 40000;
        int32 Pass = 0;
        for (int32 Roll = 0; Roll < Rolls; ++Roll)
            Pass += RollSkyLight(Random, Sky) && RollMonsterLight(Random, Block, Light) ? 1 : 0;
        return double(Pass) / Rolls;
    };
    TestEqual(TEXT("dark: always"), Share(0, 0, 0), 1.);
    TestEqual(TEXT("block light 1: never"), Share(0, 1, 1), 0.);
    TestEqual(TEXT("sky 15: 17 / 32"), Share(15, 0, 0), 17 / 32., .01);
    TestEqual(TEXT("sky 31: 1 / 32"), Share(31, 0, 0), 1 / 32., .005);
    TestEqual(TEXT("light 7: 1 / 8"), Share(0, 0, 7), 1 / 8., .01);
    TestEqual(TEXT("light 4: 1 / 2"), Share(0, 0, 4), .5, .01);
    TestEqual(TEXT("light 8: never"), Share(0, 0, 8), 0.);
    TUniquePtr<FMobLab> L = MakeDarkLab(1, FIntPoint(-40, -40), FIntPoint(39, 39));
    SetSky(*L, 4);
    SetBlock(*L, FIntVector(0, 0, 3), TEXT("glowstone"));
    TestEqual(TEXT("glowstone's cell 15"), L->A.GetBlockLightProxy(FIntVector(0, 0, 3)), 15);
    TestEqual(TEXT("two cells away 13"), L->A.GetBlockLightProxy(FIntVector(0, 0, 1)), 13);
    TestEqual(TEXT("Manhattan 12 away: 3"), L->A.GetBlockLightProxy(FIntVector(5, -5, 1)), 3);
    TestEqual(TEXT("14 away: 1"), L->A.GetBlockLightProxy(FIntVector(0, 14, 3)), 1);
    TestEqual(TEXT("15 away: 0"), L->A.GetBlockLightProxy(FIntVector(0, 15, 3)), 0);
    TestEqual(TEXT("across a section edge: 7 away 8"), L->A.GetBlockLightProxy(FIntVector(-7, 0, 3)), 8);
    int32 Sky = -1, Block = -1;
    TestEqual(
        TEXT("under the roof, far from the light: 0"), L->A.GetLightProxy(FIntVector(30, 30, 1), &Sky, &Block), 0);
    TestTrue(TEXT("no sky there"), Sky == 0 && Block == 0);
    TestEqual(
        TEXT("open ground at night: the day's light 4"), L->A.GetLightProxy(FIntVector(60, 60, 1), &Sky, &Block), 4);
    TestEqual(TEXT("raw sky 15"), Sky, 15);
    return true;
}

// Survival T6 20c2, the monster cycle [survival.r10.spawn_caps F 5461-5470; survival2.r20 population runs]: the cap
// floor(70 x eligible / 289); a cycle every step; a dark lab fills to the cap (a column's packs overshoot by at most
// 3), zombies and spiders only, standing on the floor under the roof, each at least 24 blocks from the player; none
// on a lit spot (a glowstone lights its surroundings); the open ground at noon spawns none, at night some; Peaceful
// none. Logged: the cycle's ms per step in the worst case (under the cap, nothing spawnable). Mutations: the monster
// cap 71; the full-solid start test dropped.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFoundationMobMonsterCycleTest, "Unnamed.Foundation.Mobs.MonsterCycle",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FFoundationMobMonsterCycleTest::RunTest(const FString&)
{
    using namespace MobTestsLocal;
    using namespace FoundationMobs;
    const EFoundationMobCategory Monster = EFoundationMobCategory::Monster;
    TestEqual(TEXT("cap over 289 columns"), GetCategoryCap(Monster, 289), 70);
    TestEqual(TEXT("cap over 288"), GetCategoryCap(Monster, 288), 69);
    TestEqual(TEXT("cap over 144"), GetCategoryCap(Monster, 144), 34);
    TestEqual(TEXT("local cap 70"), GetBaseCap(Monster), 70);
    TestEqual(TEXT("the creature cap unchanged"), GetCategoryCap(EFoundationMobCategory::Creature, 289), 10);
    TUniquePtr<FMobLab> L = MakeDarkLab(1);
    StepRecording(*L, 1);
    TestEqual(TEXT("a monster cycle on the first step"), L->A.MonsterCycles, 1);
    TestEqual(TEXT("cap 70"), L->A.MonsterCycleCap, 70);
    StepRecording(*L, 199);
    const int32 Count = CountMonsters(*L);
    TestTrue(*FString::Printf(TEXT("filled to the cap 70 (overshoot under 4): %d"), Count), Count >= 70 && Count <= 73);
    TestEqual(TEXT("none despawned yet"), L->A.MobsDespawned, 0);
    TestEqual(TEXT("all from the cycle"), L->A.MonstersCycleSpawned, Count);
    TestEqual(TEXT("no creature"), CountCreatures(*L), 0);
    int32 Zombies = 0, Spiders = 0;
    bool bFloor = true;
    double Nearest = TNumericLimits<double>::Max();
    for (const FFoundationMob& Mob : L->Spawned)
    {
        Zombies += Mob.Type == EFoundationMobType::Zombie ? 1 : 0;
        Spiders += Mob.Type == EFoundationMobType::Spider ? 1 : 0;
        bFloor &= FMath::IsNearlyEqual(Mob.LocalCm.Z, 100., 1.);
        Nearest = FMath::Min(Nearest, FVector::Dist(Mob.LocalCm / 100, FVector(-2, 0, 1)));
    }
    AddInfo(FString::Printf(
        TEXT("dark lab: %d zombies, %d spiders; %.3f ms worst cycle"), Zombies, Spiders, L->A.MonsterCycleMsMax));
    TestTrue(TEXT("zombies and spiders only"), Zombies + Spiders == L->Spawned.Num() && Zombies > 0 && Spiders > 0);
    TestTrue(TEXT("on the floor under the roof"), bFloor);
    TestTrue(*FString::Printf(TEXT("at least 24 blocks away (nearest %.2f)"), Nearest), Nearest >= 24);
    const int32 Cycles = L->A.MonsterCycles;
    StepRecording(*L, 20);
    TestEqual(TEXT("at the cap: no cycle runs"), L->A.MonsterCycles, Cycles);
    {
        // A glowstone in a 40 x 40 roofed patch, the rest open ground at noon: spawns only where its light is 0.
        TUniquePtr<FMobLab> Lit = MakeDarkLab(2, FIntPoint(20, -20), FIntPoint(59, 19));
        SetSky(*Lit, 15);
        SetBlock(*Lit, FIntVector(40, 0, 3), TEXT("glowstone"));
        StepRecording(*Lit, 399);
        int32 Dark = 0, Bright = 0;
        for (const FFoundationMob& Mob : Lit->Spawned)
        {
            if (!IsMonster(Mob.Type)) continue;
            const FIntVector Feet(FoundationGrid::FloorToCell(Mob.LocalCm.X),
                FoundationGrid::FloorToCell(Mob.LocalCm.Y), FoundationGrid::FloorToCell(Mob.LocalCm.Z));
            (Lit->A.GetBlockLightProxy(Feet) > 0 ? Bright : Dark) += 1;
        }
        TestTrue(*FString::Printf(TEXT("spawns in the patch's dark corners (%d)"), Dark), Dark > 0);
        TestEqual(TEXT("none where the glowstone lights"), Bright, 0);
    }
    {
        // The open ground: at noon nothing (the worst case: every step a full cycle), at night some.
        TUniquePtr<FMobLab> Day = MakeCycleLab(3);
        SetSky(*Day, 15);
        Day->Steps(400);
        TestEqual(TEXT("noon: none"), CountMonsters(*Day), 0);
        TestEqual(TEXT("noon: a cycle every step"), Day->A.MonsterCycles, 400);
        AddInfo(FString::Printf(TEXT("worst case (lab, 289 columns, nothing spawnable): %.4f ms mean, %.4f ms worst"),
            Day->A.MonsterCycleMsTotal / 400, Day->A.MonsterCycleMsMax));
        TUniquePtr<FMobLab> Night = MakeCycleLab(3);
        SetSky(*Night, 4);
        Night->Steps(400);
        TestTrue(*FString::Printf(TEXT("night: monsters on the open ground (%d)"), CountMonsters(*Night)),
            CountMonsters(*Night) > 0);
    }
    {
        // Every entry blank (mobs not built yet): a round that picks one spawns nothing.
        TUniquePtr<FMobLab> Blank = MakeDarkLab(1);
        if (FFoundationBiomeSpawns* Plains = Blank->A.Rules.BiomeSpawns.Find(TEXT("minecraft:plains")))
            for (FFoundationMobSpawn& Entry : Plains->Monsters)
                Entry.bBlank = true;
        Blank->Steps(50);
        TestEqual(TEXT("blank entries: none"), CountMonsters(*Blank), 0);
    }
    {
        TUniquePtr<FMobLab> Calm = MakeDarkLab(1);
        Calm->P().Facts.Difficulty = EFoundationDifficulty::Peaceful;
        Calm->Steps(50);
        TestEqual(TEXT("Peaceful: no monster cycle"), Calm->A.MonsterCycles, 0);
        TestEqual(TEXT("Peaceful: no monster"), CountMonsters(*Calm), 0);
    }
    return true;
}

// The census (Codex 30 #1, #2): the global cap counts every loaded mob of the category, not only those in eligible
// columns (cows beyond the player's 17 x 17 hold the creature cycle at the cap), and frozen mobs count for neither
// cap (cows frozen in an unloaded column inside the player's square leave its local cap free). Mutations: count only
// eligible columns; count frozen mobs locally.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFoundationMobCensusTest, "Unnamed.Foundation.Mobs.Census",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FFoundationMobCensusTest::RunTest(const FString&)
{
    using namespace MobTestsLocal;
    using namespace FoundationMobs;
    {
        TUniquePtr<FMobLab> L = MakeCycleLab(1);
        L->Stand(FVector(-9000, 0, 0));
        for (int32 Index = 0; Index < 10; ++Index)
            L->A.SpawnMob(EFoundationMobType::Cow, FVector(7000, -500 + Index * 100, 100));
        L->Steps(CreatureCycleTicks);
        TestEqual(TEXT("the far cows are counted"), L->A.MobCycleCount, 10);
        TestEqual(TEXT("at the cap: no creature cycle"), L->A.MobCycles, 0);
    }
    {
        TUniquePtr<FMobLab> L = MakeUnique<FMobLab>();
        TUniquePtr<FColumnStorage> Storage = MakeUnique<FColumnStorage>();
        FColumnStorage* Columns = Storage.Get();
        L->A.World.SetStorage(MoveTemp(Storage));
        MakeGrassLab(*L, 1, TEXT("grass_block"), 96);
        if (FFoundationBiomeSpawns* Plains = L->A.Rules.BiomeSpawns.Find(TEXT("minecraft:plains"))) Plains->Chance = 0;
        for (int32 Index = 0; Index < 10; ++Index)
            L->A.SpawnMob(EFoundationMobType::Cow, FVector(200 + Index * 100, 800, 100));
        L->Steps(2);
        Columns->Unloaded.Add(FIntPoint(0, 0));
        L->Steps(CreatureCycleTicks - 2);
        TestEqual(TEXT("frozen cows are not counted"), L->A.MobCycleCount, 0);
        TestTrue(TEXT("the cycle spawns"), L->A.MobCycles == 1 && L->A.MobsCycleSpawned > 0);
    }
    return true;
}

// Despawn [mobs.despawn; survival.r10.spawning_rules S 5472]: beyond 128 blocks at once; within 32 the idle count
// restarts and nothing goes; beyond 32 only after more than 600 idle steps, then 1 in 800 a step; a hit restarts the
// count; in the lab a zombie 132 blocks away goes on the next step, a cow there stays; an idle zombie 62 blocks away
// counts its steps and goes on a roll. Mutations: the far distance 129; idle 599; chance 1 in 400.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFoundationMobDespawnTest, "Unnamed.Foundation.Mobs.Despawn",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FFoundationMobDespawnTest::RunTest(const FString&)
{
    using namespace MobTestsLocal;
    using namespace FoundationMobs;
    FFoundationMob Mob;
    Mob.Type = EFoundationMobType::Zombie;
    Mob.Random.Initialize(3);
    TestTrue(TEXT("beyond 128 blocks"), ShouldDespawn(Mob, 12800.5));
    TestFalse(TEXT("at 128 blocks"), ShouldDespawn(Mob, 12800));
    Mob.NoActionTicks = 5000;
    TestFalse(TEXT("within 32"), ShouldDespawn(Mob, 3200));
    TestEqual(TEXT("within 32 the idle count restarts"), Mob.NoActionTicks, 0);
    int32 Early = 0;
    Mob.NoActionTicks = 600;
    for (int32 Roll = 0; Roll < 20000; ++Roll)
        Early += ShouldDespawn(Mob, 5000) ? 1 : 0;
    TestEqual(TEXT("600 idle steps: never"), Early, 0);
    Mob.NoActionTicks = 601;
    int32 Gone = 0;
    constexpr int32 Rolls = 400000;
    for (int32 Roll = 0; Roll < Rolls; ++Roll)
        Gone += ShouldDespawn(Mob, 5000) ? 1 : 0;
    TestEqual(TEXT("past 600: 1 in 800"), double(Gone) / Rolls, 1 / 800., .0002);
    Mob.NoActionTicks = 700;
    OnHurt(Mob, FVector::ZeroVector, FVector(100, 0, 0), true, true, 1);
    TestEqual(TEXT("a hit restarts the count"), Mob.NoActionTicks, 0);

    FMobLab L;
    L.P().Facts.Mode = EFoundationPlayerMode::Creative;
    FFoundationStatic Ground;
    Ground.Box = FBox(FVector(2000, -5000, -100), FVector(12000, 5000, 0));
    Ground.Label = TEXT("ground");
    Ground.bAnchor = true;
    L.A.Statics.Add(Ground);
    const int32 Far = L.A.SpawnMob(EFoundationMobType::Zombie, FVector(13000, 0, 0));
    const int32 Cow = L.A.SpawnMob(EFoundationMobType::Cow, FVector(13000, 300, 0));
    L.Steps(1);
    TestNull(TEXT("the far zombie is gone"), L.A.FindMob(Far));
    TestNotNull(TEXT("the far cow stays"), L.A.FindMob(Cow));
    TestEqual(TEXT("one despawned"), L.A.MobsDespawned, 1);
    const int32 Idle = L.A.SpawnMob(EFoundationMobType::Zombie, FVector(6000, 0, 0));
    L.Steps(10);
    TestEqual(TEXT("idle steps counted"), L.A.FindMob(Idle) ? L.A.FindMob(Idle)->NoActionTicks : -1, 10);
    L.A.FindMob(Idle)->NoActionTicks = 601;
    int32 Steps = 0;
    for (; Steps < 20000 && L.A.FindMob(Idle); ++Steps)
        L.Steps(1);
    TestTrue(*FString::Printf(TEXT("gone on a roll (after %d steps)"), Steps), !L.A.FindMob(Idle));
    TestEqual(TEXT("two despawned"), L.A.MobsDespawned, 2);
    return true;
}

// Peaceful (minecraft.wiki "Difficulty"): every monster goes on the next step, creatures stay; nothing spawns.
// Mutation: Peaceful ignored.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFoundationMobPeacefulTest, "Unnamed.Foundation.Mobs.Peaceful",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FFoundationMobPeacefulTest::RunTest(const FString&)
{
    using namespace MobTestsLocal;
    FMobLab L;
    const int32 Zombie = L.A.SpawnMob(EFoundationMobType::Zombie, FVector(300, 0, 0));
    const int32 Spider = L.A.SpawnMob(EFoundationMobType::Spider, FVector(300, 300, 0));
    const int32 Cow = L.A.SpawnMob(EFoundationMobType::Cow, FVector(300, -300, 0));
    L.Steps(2);
    TestTrue(TEXT("on Normal they stay"), L.A.FindMob(Zombie) && L.A.FindMob(Spider));
    L.P().Facts.Difficulty = EFoundationDifficulty::Peaceful;
    L.Steps(1);
    TestTrue(TEXT("Peaceful: the monsters go"), !L.A.FindMob(Zombie) && !L.A.FindMob(Spider));
    TestNotNull(TEXT("the cow stays"), L.A.FindMob(Cow));
    TestEqual(TEXT("counted as despawned"), L.A.MobsDespawned, 2);
    return true;
}

// K-PLAYER-7 (the smear fix): a mob's parts are instanced components whose instances move, not the components, so they
// keep per-instance previous transforms (with them the engine writes the parts' motion vectors; without them TSR took a
// moving mob for still scenery and smeared its texture). A pig drawn in its slot again moves from last frame's
// transform; a mob new to its slot starts still. Drawn by FFoundationMobView in a bare world from DA_Mobs' models.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFoundationMobMotionVectorsTest, "Unnamed.Foundation.Mobs.MotionVectors",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FFoundationMobMotionVectorsTest::RunTest(const FString&)
{
    MobTestsLocal::FMobLab Lab;
    const int32 Pig = Lab.A.SpawnMob(EFoundationMobType::Pig, FVector(0, 0, 0), 90);
    // A bare world, never begun, rooted while the test holds it (no world context, so DestroyWorld(false)).
    UWorld* World = UWorld::CreateWorld(EWorldType::Game, false);
    if (World) World->AddToRoot();
    AActor* Owner = World ? World->SpawnActor<AActor>() : nullptr;
    if (TestNotNull(TEXT("an actor"), Owner))
    {
        USceneComponent* Root = NewObject<USceneComponent>(Owner);
        Owner->SetRootComponent(Root);
        Root->RegisterComponent();
        FFoundationMobView View(*NewObject<UFoundationInteractionView>(Owner));
        TArray<UInstancedStaticMeshComponent*> Parts;
        // Each part's first slot (its first instance): its transform and previous transform, in the world.
        auto Slot0 = [&Parts](TArray<FTransform>& OutNow, TArray<FTransform>& OutPrevious)
        {
            OutNow.Reset();
            OutPrevious.Reset();
            for (const UInstancedStaticMeshComponent* Part : Parts)
            {
                Part->GetInstanceTransform(0, OutNow.AddDefaulted_GetRef(), true);
                Part->GetInstancePrevTransform(0, OutPrevious.AddDefaulted_GetRef(), true);
            }
        };
        View.Update(Lab.A, nullptr, 1);
        Owner->GetComponents(Parts);
        if (TestTrue(TEXT("the pig is drawn by its part components"), View.GetDrawnCount() == 1 && Parts.Num() > 0))
        {
            for (const UInstancedStaticMeshComponent* Part : Parts)
                TestEqual(TEXT("every instance has a previous transform"), Part->PerInstancePrevTransform.Num(),
                    Part->GetInstanceCount());
            TArray<FTransform> First, FirstPrevious, Now, Previous;
            Slot0(First, FirstPrevious);
            for (int32 Index = 0; Index < Parts.Num(); ++Index)
                TestTrue(TEXT("drawn first: still"), FirstPrevious[Index].Equals(First[Index], 1e-3));
            // It walks 50 cm.
            FFoundationMob* Mob = Lab.A.FindMob(Pig);
            Mob->PreviousLocalCm = Mob->LocalCm;
            Mob->LocalCm += FVector(0, 50, 0);
            View.Update(Lab.A, nullptr, 1);
            Slot0(Now, Previous);
            for (int32 Index = 0; Index < Parts.Num(); ++Index)
            {
                TestTrue(TEXT("walking: the previous transform is last frame's"),
                    Previous[Index].Equals(First[Index], 1e-3));
                TestEqual(TEXT("walking: 50 cm since last frame"),
                    Now[Index].GetLocation() - Previous[Index].GetLocation(), FVector(0, 50, 0), 1e-3f);
            }
            // Another pig takes the slot: it starts still.
            Lab.A.ClearMobs();
            Lab.A.SpawnMob(EFoundationMobType::Pig, FVector(300, 0, 0), 90);
            View.Update(Lab.A, nullptr, 1);
            Slot0(Now, Previous);
            for (int32 Index = 0; Index < Parts.Num(); ++Index)
                TestTrue(TEXT("a new mob in the slot: still"), Previous[Index].Equals(Now[Index], 1e-3));
        }
    }
    if (World)
    {
        World->RemoveFromRoot();
        World->DestroyWorld(false);
    }
    return true;
}

// Survival T6 20e1: farm extras. Literals from the jar's data (loot_table/shearing/sheep*.json, entities/sheep.json,
// the variant files and their biome tags) and minecraft.wiki (colour weights, dye colours; provisional).
namespace MobTestsLocal
{
    // The lab with farm extras on, a sheep (or another mob) at the origin where the player aims.
    struct FFarmLab : FMobLab
    {
        FFarmLab()
        {
            A.Options.bMobFarm = true;
            A.Options.bToolWear = true;
            // DA_Items has no shears yet (the items reference's 146 items lack them; an item request): a stand-in
            // definition with the 26.3 durability [minecraft.wiki "Shears": 238] so the shearing path runs.
            if (A.Rules.FindItem(TEXT("shears")) == INDEX_NONE)
            {
                FFoundationItemDefinition& Shears = A.Rules.Items.AddDefaulted_GetRef();
                Shears.Id = TEXT("shears");
                Shears.MaxStack = 1;
                Shears.MaxDurability = 238;
            }
        }

        int32 Spawn(EFoundationMobType Type)
        {
            A.ClearMobs();
            return A.SpawnMob(Type, FVector::ZeroVector);
        }

        void HoldStack(FName Item, int32 Count)
        {
            P().Inventory.SetSlot(0, P().Inventory.MakeStack(A.Rules.FindItem(Item), Count));
            P().Inventory.Select(0);
        }

        // A right-click on the aimed entity with the main hand.
        FFoundationResult Use(EFoundationPlayerMode Mode = EFoundationPlayerMode::Survival)
        {
            Vitals.Mode = Mode;
            FFoundationCommand Command;
            Command.SessionId = A.GetSessionId();
            Command.Type = EFoundationCommandType::UseEntity;
            Command.Hand = EFoundationHand::Main;
            Command.Item = P().Inventory.GetHand(EFoundationHand::Main).GetRef();
            Command.Target = A.Resolve(P().Facts);
            Command.Sequence = Sequence++;
            return A.Execute(Player, Command, Vitals);
        }

        int32 Dropped(FName Item) const
        {
            const int32 Index = A.Rules.FindItem(Item);
            int32 Count = 0;
            for (const FFoundationItemEntity& Entity : A.GetItemEntities())
                Count += Entity.Item == Index ? Entity.Count : 0;
            return Count;
        }

        bool HasSound(const TCHAR* Sound) const
        {
            return A.GetEvents().ContainsByPredicate([Sound](const FFoundationEvent& Event)
                { return Event.Kind == EFoundationEventKind::MobSound && Event.Interaction == FName(Sound); });
        }

        int32 CountHeld(int32 Item)
        {
            int32 Count = 0;
            for (int32 Slot = 0; Slot < FFoundationInventory::Slots; ++Slot)
                if (const FFoundationItemInstance* Stack = P().Inventory.GetSlot(Slot); Stack && Stack->Item == Item)
                    Count += Stack->Count;
            return Count;
        }

        FName HeldId() { return A.Rules.GetItem(P().Inventory.GetHand(EFoundationHand::Main).Item).Id; }
    };
}

// [minecraft.wiki "Sheep", "Dye"; provisional] 30,000 natural colours: white 0.81836, black, gray and light gray 0.05,
// brown 0.03, pink 0.00164, nothing else; the dye names and the wool tints. Mutation: brown 4% (Roll < 19).
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFoundationMobSheepColorTest, "Unnamed.Foundation.Mobs.SheepColor",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FFoundationMobSheepColorTest::RunTest(const FString&)
{
    using namespace FoundationMobFarm;
    TestEqual(TEXT("16 colours"), ColorCount, 16);
    TestEqual(TEXT("colour 0 white"), GetColorName(0), FName(TEXT("white")));
    TestEqual(TEXT("colour 14 red"), GetColorName(14), FName(TEXT("red")));
    TestEqual(TEXT("colour 15 black"), GetColorName(15), FName(TEXT("black")));
    TestEqual(TEXT("red_dye"), FindDyeColor(TEXT("red_dye")), 14);
    TestEqual(TEXT("light_blue_dye"), FindDyeColor(TEXT("light_blue_dye")), 3);
    TestEqual(TEXT("red_wool is no dye"), FindDyeColor(TEXT("red_wool")), INDEX_NONE);
    TestEqual(TEXT("wheat is no dye"), FindDyeColor(TEXT("wheat")), INDEX_NONE);
    int32 Counts[16] = {};
    FRandomStream Random(2026);
    constexpr int32 Rolls = 30000;
    for (int32 Roll = 0; Roll < Rolls; ++Roll)
        ++Counts[RollSheepColor(Random)];
    TestEqual(TEXT("white"), Counts[0] / double(Rolls), .81836, .01);
    TestEqual(TEXT("black"), Counts[15] / double(Rolls), .05, .006);
    TestEqual(TEXT("gray"), Counts[7] / double(Rolls), .05, .006);
    TestEqual(TEXT("light gray"), Counts[8] / double(Rolls), .05, .006);
    TestEqual(TEXT("brown"), Counts[12] / double(Rolls), .03, .005);
    TestEqual(TEXT("pink"), Counts[6] / double(Rolls), .00164, .0012);
    TestEqual(
        TEXT("no other colour"), Rolls - Counts[0] - Counts[15] - Counts[7] - Counts[8] - Counts[12] - Counts[6], 0);
    // [minecraft.wiki "Dye"] red B02E26 x 0.75 = (132, 34, 28); white E6E6E6.
    TestTrue(TEXT("red wool tint"), GetWoolTint(14).ToFColor(true) == FColor(132, 34, 28));
    TestTrue(TEXT("white wool tint"), GetWoolTint(0).ToFColor(true) == FColor(230, 230, 230));
    return true;
}

// [jar loot_table/shearing/sheep.json + sheep/red.json] shears on a red sheep with its wool: 1-3 red wool (uniform),
// the shear sound, one point of wear; a shorn sheep passes. The table over 3,000 rolls: only red wool, 1-3 each about
// a third; shorn: nothing. Creative shears do not wear. Mutations: shearing a shorn sheep; no wear.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFoundationMobShearTest, "Unnamed.Foundation.Mobs.Shear",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FFoundationMobShearTest::RunTest(const FString&)
{
    using namespace MobTestsLocal;
    FFarmLab L;
    const FFoundationLootTable* Table = L.A.Rules.EntityLootTables.Find(TEXT("shearing/sheep"));
    if (!TestNotNull(TEXT("shearing/sheep from DA_Mobs"), Table)) return false;
    const int32 Red = L.A.Rules.FindItem(TEXT("red_wool"));
    int32 Counts[4] = {}, Other = 0;
    FRandomStream Random(20);
    constexpr int32 Rolls = 3000;
    for (int32 Roll = 0; Roll < Rolls; ++Roll)
    {
        FFoundationLootContext Context{L.A.Rules.Hand};
        Context.SheepColor = TEXT("red");
        TArray<FFoundationLootDrop> Drops;
        FoundationLoot::Roll(*Table, L.A.Rules.LootPredicates, Context, Random, Drops);
        int32 Count = 0;
        for (const FFoundationLootDrop& Drop : Drops)
            (Drop.Item == Red ? Count : Other) += Drop.Count;
        ++Counts[FMath::Min(Count, 3)];
    }
    TestEqual(TEXT("never none"), Counts[0], 0);
    TestEqual(TEXT("only red wool"), Other, 0);
    for (int32 Count = 1; Count <= 3; ++Count)
        TestEqual(*FString::Printf(TEXT("%d wool"), Count), Counts[Count] / double(Rolls), 1 / 3., .04);
    {
        FFoundationLootContext Context{L.A.Rules.Hand};
        Context.SheepColor = TEXT("red");
        Context.bSheared = true;
        TArray<FFoundationLootDrop> Drops;
        FoundationLoot::Roll(*Table, L.A.Rules.LootPredicates, Context, Random, Drops);
        TestEqual(TEXT("a shorn sheep's table drops nothing"), Drops.Num(), 0);
    }
    const int32 Id = L.Spawn(EFoundationMobType::Sheep);
    L.A.FindMob(Id)->Color = 14;
    L.Hold(TEXT("shears"));
    const int32 MaxDurability = L.A.Rules.GetItem(L.A.Rules.FindItem(TEXT("shears"))).MaxDurability;
    TestEqual(TEXT("shears last 238 uses"), MaxDurability, 238);
    const FFoundationResult First = L.Use();
    TestTrue(TEXT("shearing commits"), First.IsCommitted());
    TestTrue(TEXT("the sheep is shorn"), L.A.FindMob(Id)->bSheared);
    const int32 Wool = L.Dropped(TEXT("red_wool"));
    TestTrue(TEXT("1-3 red wool"), Wool >= 1 && Wool <= 3);
    TestTrue(TEXT("the shear sound"), L.HasSound(TEXT("entity.sheep.shear")));
    TestEqual(TEXT("one point of wear"), L.P().Inventory.GetHand(EFoundationHand::Main).Damage, 1);
    TestEqual(TEXT("one shear"), L.A.MobShears, 1);
    const FFoundationResult Again = L.Use();
    TestEqual(TEXT("a shorn sheep passes"), Again.Outcome, EFoundationOutcome::Pass);
    TestEqual(TEXT("no more wool"), L.Dropped(TEXT("red_wool")), Wool);
    TestEqual(TEXT("no more wear"), L.P().Inventory.GetHand(EFoundationHand::Main).Damage, 1);
    // Creative: no wear.
    const int32 Second = L.Spawn(EFoundationMobType::Sheep);
    L.Hold(TEXT("shears"));
    TestTrue(TEXT("creative shears"), L.Use(EFoundationPlayerMode::Creative).IsCommitted());
    TestTrue(TEXT("creative shore it"), L.A.FindMob(Second)->bSheared);
    TestEqual(TEXT("creative: no wear"), L.P().Inventory.GetHand(EFoundationHand::Main).Damage, 0);
    // Shears on a cow pass.
    L.Spawn(EFoundationMobType::Cow);
    TestEqual(TEXT("shears on a cow pass"), L.Use().Outcome, EFoundationOutcome::Pass);
    return true;
}

// [jar loot_table/entities/sheep.json] a player's kill of a red sheep with its wool drops one red wool and mutton; a
// shorn one only mutton; with farm extras off no wool, as before 20e1. Grazing regrows the wool
// [survival.r10.ai_defaults]. Mutations: the kill ignores bSheared; grazing keeps the sheep shorn.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFoundationMobShearKillTest, "Unnamed.Foundation.Mobs.ShearKill",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FFoundationMobShearKillTest::RunTest(const FString&)
{
    using namespace MobTestsLocal;
    const auto Kill = [](FFarmLab& L, bool bSheared, int32& OutWool, int32& OutMutton)
    {
        const int32 Id = L.Spawn(EFoundationMobType::Sheep);
        L.A.FindMob(Id)->Color = 14;
        L.A.FindMob(Id)->bSheared = bSheared;
        L.A.FindMob(Id)->Health.Health = 1;
        L.Hold(NAME_None);
        L.Attack();
        OutWool = L.Dropped(TEXT("red_wool"));
        OutMutton = L.Dropped(TEXT("mutton"));
    };
    int32 Wool = 0, Mutton = 0;
    {
        FFarmLab L;
        Kill(L, false, Wool, Mutton);
        TestEqual(TEXT("one red wool"), Wool, 1);
        TestTrue(TEXT("mutton"), Mutton >= 1 && Mutton <= 2);
    }
    {
        FFarmLab L;
        Kill(L, true, Wool, Mutton);
        TestEqual(TEXT("shorn: no wool"), Wool, 0);
        TestTrue(TEXT("shorn: mutton"), Mutton >= 1 && Mutton <= 2);
    }
    {
        FFarmLab L;
        L.A.Options.bMobFarm = false;
        Kill(L, false, Wool, Mutton);
        TestEqual(TEXT("farm off: no wool"), Wool + L.Dropped(TEXT("white_wool")), 0);
    }
    // Regrowth: a shorn sheep eating the grass block below it.
    FFarmLab L;
    const FIntVector Grass = SetBlock(L, FIntVector(0, 0, 0), TEXT("grass_block"));
    L.A.ClearMobs();
    const int32 Id = L.A.SpawnMob(EFoundationMobType::Sheep, FVector(50, 50, 100));
    L.Steps(1);
    L.A.FindMob(Id)->bSheared = true;
    L.A.FindMob(Id)->GrazeTicks = 40;
    L.Steps(35);
    TestTrue(TEXT("still shorn before eating"), L.A.FindMob(Id)->bSheared);
    L.Steps(1);
    TestEqual(TEXT("the grass is eaten"), BlockAt(L, Grass), FName(TEXT("dirt")));
    TestFalse(TEXT("the wool grew back"), L.A.FindMob(Id)->bSheared);
    return true;
}

// [minecraft.wiki "Dye", provisional] a green dye recolours a white sheep and is used up; the same colour again, or a
// shorn sheep, passes; creative keeps the dye. (DA_Items holds two of the 16 dyes, green and lime, since T1 8a2.)
// Mutation: a shorn sheep takes the dye.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFoundationMobDyeTest, "Unnamed.Foundation.Mobs.Dye",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FFoundationMobDyeTest::RunTest(const FString&)
{
    using namespace MobTestsLocal;
    FFarmLab L;
    const int32 Id = L.Spawn(EFoundationMobType::Sheep);
    L.A.FindMob(Id)->Color = 0;
    L.HoldStack(TEXT("green_dye"), 2);
    TestTrue(TEXT("dyeing commits"), L.Use().IsCommitted());
    TestEqual(TEXT("green now"), int32(L.A.FindMob(Id)->Color), 13);
    TestEqual(TEXT("one dye used"), L.P().Inventory.GetHand(EFoundationHand::Main).Count, 1);
    TestEqual(TEXT("green on green passes"), L.Use().Outcome, EFoundationOutcome::Pass);
    TestEqual(TEXT("no dye used"), L.P().Inventory.GetHand(EFoundationHand::Main).Count, 1);
    L.HoldStack(TEXT("lime_dye"), 1);
    L.A.FindMob(Id)->bSheared = true;
    TestEqual(TEXT("a shorn sheep passes"), L.Use().Outcome, EFoundationOutcome::Pass);
    TestEqual(TEXT("still green"), int32(L.A.FindMob(Id)->Color), 13);
    L.A.FindMob(Id)->bSheared = false;
    TestTrue(TEXT("creative dyeing"), L.Use(EFoundationPlayerMode::Creative).IsCommitted());
    TestEqual(TEXT("lime now"), int32(L.A.FindMob(Id)->Color), 5);
    TestEqual(TEXT("creative keeps the dye"), L.P().Inventory.GetHand(EFoundationHand::Main).Count, 1);
    TestEqual(TEXT("two dyes"), L.A.MobDyes, 2);
    L.Spawn(EFoundationMobType::Cow);
    TestEqual(TEXT("dye on a cow passes"), L.Use().Outcome, EFoundationOutcome::Pass);
    return true;
}

// [minecraft.wiki "Cow", "Bucket"; provisional] an empty bucket on a cow becomes a milk bucket with the milk sound;
// from a stack of two, one bucket goes and the milk is added; creative keeps the bucket and adds one milk bucket; a pig
// passes. Mutation: milk without the cow check.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFoundationMobMilkTest, "Unnamed.Foundation.Mobs.Milk",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FFoundationMobMilkTest::RunTest(const FString&)
{
    using namespace MobTestsLocal;
    FFarmLab L;
    const int32 Milk = L.A.Rules.FindItem(TEXT("milk_bucket"));
    if (!TestTrue(TEXT("milk_bucket is an item"), Milk >= 0)) return false;
    L.Spawn(EFoundationMobType::Cow);
    L.Hold(TEXT("bucket"));
    TestTrue(TEXT("milking commits"), L.Use().IsCommitted());
    TestEqual(TEXT("a milk bucket in hand"), L.HeldId(), FName(TEXT("milk_bucket")));
    TestTrue(TEXT("the milk sound"), L.HasSound(TEXT("entity.cow.milk")));
    TestEqual(TEXT("one milking"), L.A.MobMilks, 1);
    TestEqual(TEXT("a milk bucket milks nothing"), L.Use().Outcome, EFoundationOutcome::Pass);
    L.HoldStack(TEXT("bucket"), 2);
    TestTrue(TEXT("milking from a stack"), L.Use().IsCommitted());
    TestEqual(TEXT("one bucket left"), L.P().Inventory.GetHand(EFoundationHand::Main).Count, 1);
    TestEqual(TEXT("the milk went to the inventory"), L.CountHeld(Milk), 1);
    L.HoldStack(TEXT("bucket"), 1);
    TestTrue(TEXT("creative milking"), L.Use(EFoundationPlayerMode::Creative).IsCommitted());
    TestEqual(TEXT("creative keeps the bucket"), L.HeldId(), FName(TEXT("bucket")));
    TestEqual(TEXT("creative adds none while one is held"), L.CountHeld(Milk), 1);
    L.P().Inventory.SetSlot(1, FFoundationItemInstance());
    for (int32 Slot = 1; Slot < FFoundationInventory::Slots; ++Slot)
        if (const FFoundationItemInstance* Stack = L.P().Inventory.GetSlot(Slot); Stack && Stack->Item == Milk)
            L.P().Inventory.SetSlot(Slot, FFoundationItemInstance());
    TestTrue(TEXT("creative milking again"), L.Use(EFoundationPlayerMode::Creative).IsCommitted());
    TestEqual(TEXT("creative adds one milk bucket when none is held"), L.CountHeld(Milk), 1);
    TestEqual(TEXT("and keeps the bucket"), L.HeldId(), FName(TEXT("bucket")));
    L.Spawn(EFoundationMobType::Pig);
    TestEqual(TEXT("a bucket on a pig passes"), L.Use().Outcome, EFoundationOutcome::Pass);
    TestEqual(TEXT("the bucket stays"), L.HeldId(), FName(TEXT("bucket")));
    return true;
}

// [jar cow/pig/chicken_variant, tags spawns_{cold,warm}_variant_farm_animals] the biome's variant: 5 cold, 5 warm
// (2 through #is_* tags), 3 temperate; the models (cold and warm cow, cold pig and chicken their own geometry; the warm
// pig and chicken the main one) in DA_Mobs; a spawn takes its biome's variant, a sheep its colour stream. Farm off:
// temperate and white. Mutations: swap cold and warm; drop the #is_savanna members.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFoundationMobVariantTest, "Unnamed.Foundation.Mobs.Variant",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FFoundationMobVariantTest::RunTest(const FString&)
{
    using namespace MobTestsLocal;
    using namespace FoundationMobFarm;
    for (const TCHAR* Biome : {TEXT("minecraft:snowy_plains"), TEXT("minecraft:taiga"), TEXT("minecraft:grove"),
             TEXT("minecraft:frozen_river"), TEXT("minecraft:end_highlands")})
        TestEqual(Biome, GetVariant(Biome), EFoundationMobVariant::Cold);
    for (const TCHAR* Biome : {TEXT("minecraft:desert"), TEXT("minecraft:savanna_plateau"), TEXT("minecraft:jungle"),
             TEXT("minecraft:wooded_badlands"), TEXT("minecraft:mangrove_swamp")})
        TestEqual(Biome, GetVariant(Biome), EFoundationMobVariant::Warm);
    for (const TCHAR* Biome : {TEXT("minecraft:plains"), TEXT("minecraft:forest"), TEXT("minecraft:swamp")})
        TestEqual(Biome, GetVariant(Biome), EFoundationMobVariant::Temperate);
    TestEqual(TEXT("cold cow model"), GetModelId(EFoundationMobType::Cow, EFoundationMobVariant::Cold),
        FName(TEXT("cold_cow")));
    TestEqual(TEXT("temperate pig model"), GetModelId(EFoundationMobType::Pig, EFoundationMobVariant::Temperate),
        FName(TEXT("pig")));
    TestEqual(TEXT("sheep have no variant"), GetModelId(EFoundationMobType::Sheep, EFoundationMobVariant::Warm),
        FName(TEXT("sheep")));
    if (const UFoundationMobAssets* Assets = UFoundationMobAssets::LoadDefault(); TestNotNull(TEXT("DA_Mobs"), Assets))
    {
        // [facts/entity_models.json] parts per layer: cold cow 9 (horns), warm cow 7, cold pig 7, cold chicken 9.
        const TPair<const TCHAR*, int32> Models[] = {{TEXT("cold_cow"), 9}, {TEXT("warm_cow"), 7},
            {TEXT("cold_pig"), 7}, {TEXT("warm_pig"), 7}, {TEXT("cold_chicken"), 9}, {TEXT("warm_chicken"), 9}};
        for (const TPair<const TCHAR*, int32>& Model : Models)
        {
            const FFoundationMobModelAsset* Found = Assets->FindModel(Model.Key);
            TestTrue(*FString::Printf(TEXT("%s with %d parts"), Model.Key, Model.Value),
                Found && Found->Parts.Num() == Model.Value);
        }
        const FFoundationMobModelAsset* WarmPig = Assets->FindModel(TEXT("warm_pig"));
        TestTrue(TEXT("the warm pig keeps pig#main"), WarmPig && WarmPig->Layer == TEXT("pig#main"));
        TArray<const FFoundationMobModelAsset*> Cow;
        Assets->FindLayers(TEXT("cow"), Cow);
        TestEqual(TEXT("the cow draws one layer still"), Cow.Num(), 1);
    }
    FFarmLab L;
    FName Biome = TEXT("minecraft:snowy_plains");
    L.A.GetBiomeId = [&Biome](const FIntVector&) { return Biome; };
    TestEqual(TEXT("a cow in snowy plains is cold"), L.A.FindMob(L.Spawn(EFoundationMobType::Cow))->Variant,
        EFoundationMobVariant::Cold);
    Biome = TEXT("minecraft:desert");
    TestEqual(TEXT("a pig in the desert is warm"), L.A.FindMob(L.Spawn(EFoundationMobType::Pig))->Variant,
        EFoundationMobVariant::Warm);
    TestEqual(TEXT("a sheep in the desert is temperate"), L.A.FindMob(L.Spawn(EFoundationMobType::Sheep))->Variant,
        EFoundationMobVariant::Temperate);
    Biome = TEXT("minecraft:plains");
    TestEqual(TEXT("a chicken in plains is temperate"), L.A.FindMob(L.Spawn(EFoundationMobType::Chicken))->Variant,
        EFoundationMobVariant::Temperate);
    // Colours over many sheep: some not white.
    int32 Coloured = 0;
    for (int32 Index = 0; Index < 200; ++Index)
        Coloured += L.A.FindMob(L.Spawn(EFoundationMobType::Sheep))->Color != 0 ? 1 : 0;
    TestTrue(TEXT("some of 200 sheep are coloured"), Coloured > 10 && Coloured < 80);
    L.A.Options.bMobFarm = false;
    Biome = TEXT("minecraft:snowy_plains");
    TestEqual(TEXT("farm off: a temperate cow"), L.A.FindMob(L.Spawn(EFoundationMobType::Cow))->Variant,
        EFoundationMobVariant::Temperate);
    int32 OffColoured = 0;
    for (int32 Index = 0; Index < 200; ++Index)
        OffColoured += L.A.FindMob(L.Spawn(EFoundationMobType::Sheep))->Color != 0 ? 1 : 0;
    TestEqual(TEXT("farm off: white sheep"), OffColoured, 0);
    return true;
}

// foundation.Mobs.Farm 0: shears, dye and a bucket on a mob all pass and change nothing (as before 20e1); the switch
// defaults on. Mutation: UseEntity ignores bMobFarm.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFoundationMobFarmSwitchTest, "Unnamed.Foundation.Mobs.FarmSwitch",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FFoundationMobFarmSwitchTest::RunTest(const FString&)
{
    using namespace MobTestsLocal;
    FFarmLab L;
    L.A.Options.bMobFarm = false;
    const int32 Sheep = L.Spawn(EFoundationMobType::Sheep);
    L.Hold(TEXT("shears"));
    TestEqual(TEXT("off: shears pass"), L.Use().Outcome, EFoundationOutcome::Pass);
    TestFalse(TEXT("off: not shorn"), L.A.FindMob(Sheep)->bSheared);
    TestEqual(TEXT("off: no wear"), L.P().Inventory.GetHand(EFoundationHand::Main).Damage, 0);
    L.Hold(TEXT("green_dye"));
    TestEqual(TEXT("off: dye passes"), L.Use().Outcome, EFoundationOutcome::Pass);
    TestEqual(TEXT("off: still white"), int32(L.A.FindMob(Sheep)->Color), 0);
    L.Spawn(EFoundationMobType::Cow);
    L.Hold(TEXT("bucket"));
    TestEqual(TEXT("off: the bucket passes"), L.Use().Outcome, EFoundationOutcome::Pass);
    TestEqual(TEXT("off: still a bucket"), L.HeldId(), FName(TEXT("bucket")));
    TestTrue(TEXT("the console switch defaults on"), FoundationMobFarm::IsOn());
    return true;
}
#endif
