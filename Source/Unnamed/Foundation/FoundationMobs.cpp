#include "Foundation/FoundationMobs.h"
#include "Foundation/FoundationGrid.h"
#include "Foundation/FoundationItemEntities.h"
#include "Foundation/FoundationSimulation.h"
#include "HAL/IConsoleManager.h"

namespace
{
    TAutoConsoleVariable<int32> CVarMobs(TEXT("foundation.Mobs"), 1,
        TEXT("1: mobs exist (survival T6 20a): foundation.Mobs.Summon cow spawns one that wanders, panics when hit, is "
             "knocked back and dies into its loot and experience. 0: no mobs are spawned, stepped or drawn, and only "
             "training targets can be hit, as before."));
    TAutoConsoleVariable<int32> CVarMobSpawn(TEXT("foundation.Mobs.Spawn"), 1,
        TEXT("1: passive spawning in generated worlds (survival T6 20b2): a column first seen loaded may spawn a pack "
             "of farm animals with its biome's chance. 0: mobs come only from foundation.Mobs.Summon."));

    // [mobs.base_attributes, catalog.entities; value-swaps.md T6] health, movement attribute, armor, toughness,
    // knockback resistance, width x height (eye); [survival.r10.ai_defaults] the panic and tempt modifiers;
    // [survival.r5.play_calls] the volume; [survival.r10.death_roll] the roll; wiki "Experience" (provisional) the
    // experience; the attack damage, follow range and look distance (FFoundationMobKind); the en_us.json name.
    const FFoundationMobKind MobKinds[] = {
        {TEXT("cow"), 10, .2f, 0, 0, 0, 90, 140, 130, 2.f, 1.25f, .4f, 90, 1, 3, true, 0, 1600, 600, TEXT("Cow")},
        {TEXT("pig"), 10, .25f, 0, 0, 0, 90, 90, 76.5, 1.25f, 1.2f, 1, 90, 1, 3, true, 0, 1600, 600, TEXT("Pig")},
        {TEXT("sheep"), 8, .23f, 0, 0, 0, 90, 130, 123.5, 1.25f, 1.1f, 1, 90, 1, 3, true, 0, 1600, 600, TEXT("Sheep")},
        {TEXT("chicken"), 4, .25f, 0, 0, 0, 40, 70, 64.4, 1.4f, 1.f, 1, 90, 1, 3, true, 0, 1600, 600, TEXT("Chicken")},
        {TEXT("zombie"), 20, .23f, 2, 0, 0, 60, 195, 174, 0, 0, 1, 90, 5, 5, true, 3, 3500, 800, TEXT("Zombie")},
        {TEXT("skeleton"), 20, .25f, 0, 0, 0, 60, 199, 174, 0, 0, 1, 90, 5, 5, false, 2, 1600, 800, TEXT("Skeleton")},
        {TEXT("creeper"), 20, .25f, 0, 0, 0, 60, 170, 144.5, 0, 0, 1, 90, 5, 5, false, 2, 1600, 800, TEXT("Creeper")},
        {TEXT("spider"), 16, .3f, 0, 0, 0, 140, 90, 65, 0, 0, 1, 180, 5, 5, true, 2, 1600, 800, TEXT("Spider"), .8f},
    };
    static_assert(UE_ARRAY_COUNT(MobKinds) == int32(EFoundationMobType::Count));

    // [survival.r10.ai_defaults] breeding foods (F 5342-5347), which tempt: cow and sheep wheat; pig carrot, potato,
    // beetroot; chicken six seeds. Pig's carrot on a stick waits for that item.
    const TCHAR* const MobCowFoods[] = {TEXT("wheat")};
    const TCHAR* const MobPigFoods[] = {TEXT("carrot"), TEXT("potato"), TEXT("beetroot")};
    const TCHAR* const MobChickenFoods[] = {TEXT("wheat_seeds"), TEXT("pumpkin_seeds"), TEXT("melon_seeds"),
        TEXT("torchflower_seeds"), TEXT("pitcher_pod"), TEXT("beetroot_seeds")};

    TConstArrayView<const TCHAR* const> MobFoods(EFoundationMobType Type)
    {
        switch (Type)
        {
        case EFoundationMobType::Cow:
        case EFoundationMobType::Sheep:
            return MobCowFoods;
        case EFoundationMobType::Pig:
            return MobPigFoods;
        case EFoundationMobType::Chicken:
            return MobChickenFoods;
        default:
            return {};
        }
    }

    FVector2D MobHorizontal(const FVector& V)
    {
        return FVector2D(V.X, V.Y);
    }

    // A random point around From within Range cells horizontally and RangeUp vertically (whole cells, as the AI's
    // random positions); PlanPath settles it on a stand cell.
    FVector MobRandomTarget(FFoundationMob& Mob, const FVector& From, int32 Range, int32 RangeUp)
    {
        const double Block = 100;
        const int32 X = Mob.Random.RandRange(-Range, Range), Y = Mob.Random.RandRange(-Range, Range);
        const int32 Z = Mob.Random.RandRange(-RangeUp, RangeUp);
        return From + FVector(X * Block, Y * Block, Z * Block);
    }

    void MobStartMove(FFoundationMob& Mob, const FVector& Target, float Modifier, bool bPanic, int32 TargetUpCells)
    {
        Mob.bMoving = true;
        Mob.bPanicking = bPanic;
        Mob.TargetCm = Target;
        Mob.TargetUpCells = TargetUpCells;
        Mob.SpeedModifier = Modifier;
        Mob.MoveTicks = Mob.StuckTicks = 0;
        Mob.Path.Reset();
        Mob.PathIndex = 0;
        Mob.bPathPartial = false;
        Mob.bNeedPath = true;
    }

    void MobStopMove(FFoundationMob& Mob)
    {
        Mob.bMoving = false;
        Mob.bNeedPath = false;
        Mob.Path.Reset();
        Mob.PathIndex = 0;
    }

    FIntVector MobCellOf(const FVector& LocalCm)
    {
        // A centimetre up: feet resting on a block's top are in the cell above it.
        return FIntVector(FoundationGrid::FloorToCell(LocalCm.X), FoundationGrid::FloorToCell(LocalCm.Y),
            FoundationGrid::FloorToCell(LocalCm.Z + 1));
    }

    float MobTurnToward(float From, float To, float MaxStep)
    {
        return From + FMath::Clamp(FMath::FindDeltaAngleDegrees(From, To), -MaxStep, MaxStep);
    }
}

bool FoundationMobs::IsOn()
{
    return CVarMobs.GetValueOnAnyThread() != 0;
}

bool FoundationMobs::IsSpawnOn()
{
    return CVarMobSpawn.GetValueOnAnyThread() != 0;
}

const FFoundationMobKind& FoundationMobs::GetKind(EFoundationMobType Type)
{
    return MobKinds[FMath::Clamp(int32(Type), 0, int32(EFoundationMobType::Count) - 1)];
}

bool FoundationMobs::FindType(FName Id, EFoundationMobType& OutType)
{
    for (int32 Index = 0; Index < UE_ARRAY_COUNT(MobKinds); ++Index)
        if (MobKinds[Index].Id == Id)
        {
            OutType = EFoundationMobType(Index);
            return true;
        }
    return false;
}

FBox FoundationMobs::GetBox(EFoundationMobType Type, const FVector& WorldCm)
{
    const FFoundationMobKind& Kind = GetKind(Type);
    const FVector Half(Kind.WidthCm * .5, Kind.WidthCm * .5, 0);
    return FBox(WorldCm - Half, WorldCm + Half + FVector(0, 0, Kind.HeightCm));
}

FBox FoundationMobs::GetStepRegion(const FFoundationMob& Mob, const FVector& OriginCm)
{
    // This step's velocity with the most input and a jump, the step-up's rise, and a centimetre for contact.
    const double Drive = GetKind(Mob.Type).Speed * Mob.SpeedModifier;
    const double Accel = FMath::Max(Drive * Drive * WalkAccelCm, AirAccelCm);
    const FVector Reach =
        Mob.VelocityCm.GetAbs() + FVector(Accel, Accel, FMath::Max(JumpCm, StepCm)) + FVector(GravityCm + 1);
    return GetBox(Mob.Type, OriginCm + Mob.LocalCm).ExpandBy(Reach);
}

void FoundationMobs::ApplyKnockback(FVector& VelocityCm, const FVector2D& Away, bool bGrounded, double PowerCm)
{
    if (PowerCm <= 0) return;
    const FVector2D Unit = Away.GetSafeNormal();
    VelocityCm.X = VelocityCm.X * .5 + Unit.X * PowerCm;
    VelocityCm.Y = VelocityCm.Y * .5 + Unit.Y * PowerCm;
    if (bGrounded) VelocityCm.Z = FMath::Min(KnockbackUpCm, VelocityCm.Z * .5 + PowerCm);
}

const FFoundationMobSpawn* FoundationMobs::PickSpawn(const FFoundationBiomeSpawns& Biome, FRandomStream& Random)
{
    return PickSpawn(Biome.Creatures, Random);
}

const FFoundationMobSpawn* FoundationMobs::PickSpawn(
    TConstArrayView<FFoundationMobSpawn> Entries, FRandomStream& Random)
{
    int32 Total = 0;
    for (const FFoundationMobSpawn& Entry : Entries)
        Total += FMath::Max(0, Entry.Weight);
    if (Total <= 0) return nullptr;
    int32 Draw = Random.RandHelper(Total);
    for (const FFoundationMobSpawn& Entry : Entries)
        if ((Draw -= FMath::Max(0, Entry.Weight)) < 0) return &Entry;
    return nullptr;
}

int32 FoundationMobs::RollPackSize(const FFoundationMobSpawn& Entry, FRandomStream& Random)
{
    return Random.RandRange(Entry.MinCount, FMath::Max(Entry.MinCount, Entry.MaxCount));
}

int32 FoundationMobs::RollPackOffset(FRandomStream& Random)
{
    // One draw per statement: argument order is unspecified.
    const int32 Plus = Random.RandRange(0, PackSpreadCells);
    return Plus - Random.RandRange(0, PackSpreadCells);
}

bool FoundationMobs::IsCreature(EFoundationMobType Type)
{
    return Type < EFoundationMobType::Zombie;
}

bool FoundationMobs::IsMonster(EFoundationMobType Type)
{
    return Type >= EFoundationMobType::Zombie && Type < EFoundationMobType::Count;
}

float FoundationMobs::GetBrightness(int32 Level)
{
    const float F = float(FMath::Clamp(Level, 0, 15)) / 15.f;
    return F / (4.f - 3.f * F);
}

bool FoundationMobs::RollSunBurn(FFoundationMob& Mob, float Brightness)
{
    return Mob.Random.GetFraction() * 30.f < (Brightness - .4f) * 2.f;
}

bool FoundationMobs::IsInMeleeReach(
    const FFoundationMob& Mob, const FVector& OriginCm, const FFoundationMobPlayer& Player)
{
    const FBox Reach = GetBox(Mob.Type, OriginCm + Mob.LocalCm).ExpandBy(FVector(MeleeReachCm, MeleeReachCm, 0));
    const FVector Half(Player.WidthCm * .5, Player.WidthCm * .5, 0);
    return Reach.Intersect(FBox(Player.FeetCm - Half, Player.FeetCm + Half + FVector(0, 0, Player.HeightCm)));
}

void FoundationMobs::ThinkHostile(FFoundationMob& Mob, const FVector& OriginCm,
    TConstArrayView<FFoundationMobPlayer> Players, TFunctionRef<bool(const FVector&, const FVector&)> CanSee,
    float Brightness)
{
    Mob.bAttackNow = false;
    if (Mob.AttackSwing < AttackSwingTicks) ++Mob.AttackSwing;
    if (!Mob.IsAlive() || !IsMonster(Mob.Type)) return;
    const FFoundationMobKind& Kind = GetKind(Mob.Type);
    const FVector Feet = OriginCm + Mob.LocalCm;
    const FVector Eye = Feet + FVector(0, 0, Kind.EyeCm);
    const auto InRange = [&](const FFoundationMobPlayer& Player)
    {
        const FVector Delta = Player.FeetCm - Feet;
        return Player.bTargetable && FMath::Abs(Delta.Z) <= TargetSearchUpCm + Kind.HeightCm &&
            FMath::Abs(Delta.X) <= Kind.FollowRangeCm && FMath::Abs(Delta.Y) <= Kind.FollowRangeCm &&
            Delta.SizeSquared() <= Kind.FollowRangeCm * Kind.FollowRangeCm;
    };
    // Keep the target while it is targetable, in range and seen within UnseenMemoryTicks.
    const FFoundationMobPlayer* Target =
        Players.FindByPredicate([&Mob](const FFoundationMobPlayer& Player) { return Player.Id == Mob.Target; });
    // Only a target in range is looked for (the line of sight is a grid walk).
    const bool bInRange = Target && InRange(*Target);
    bool bSees = bInRange && CanSee(Eye, Target->EyeCm);
    if (bInRange) Mob.UnseenTicks = bSees ? 0 : Mob.UnseenTicks + 1;
    // The spider: SpiderTargetGoal takes a target only in the dark; in the light it forgets one now and then.
    const bool bSpider = Mob.Type == EFoundationMobType::Spider;
    const bool bDark = !bSpider || Brightness < SpiderDarkBrightness;
    const bool bForget = !bDark && Mob.Target != 0 && Mob.Random.RandHelper(SpiderForgetChance) == 0;
    if (Mob.Target != 0 && (!bInRange || Mob.UnseenTicks > UnseenMemoryTicks || bForget))
    {
        Mob.Target = 0;
        Target = nullptr;
        bSees = false;
        if (Mob.bMoving) MobStopMove(Mob);
    }
    // A new target: the nearest player in range that it sees.
    if (!Target && bDark && Mob.Age % 2 == 0 && Mob.Random.RandHelper(TargetChance) == 0)
    {
        double Best = MAX_dbl;
        for (const FFoundationMobPlayer& Player : Players)
            if (const double Distance = FVector::DistSquared(Player.FeetCm, Feet);
                Distance < Best && InRange(Player) && CanSee(Eye, Player.EyeCm))
            {
                Best = Distance;
                Target = &Player;
            }
        if (Target)
        {
            Mob.Target = Target->Id;
            Mob.UnseenTicks = 0;
            Mob.AttackCooldown = 0;
            Mob.RepathTicks = 0;
            bSees = true;
        }
    }
    if (!Target) return;
    // The melee goal: chase it (a fresh path when the delay ran out and it can see it), look at it, and hit it when
    // the swing is due and it is in reach and in sight.
    const FVector Goal = Target->FeetCm - OriginCm;
    // SpiderAttackGoal follows a target it does not see.
    if (Mob.RepathTicks <= 0 && (bSees || bSpider || !Mob.bMoving))
    {
        MobStartMove(Mob, Goal, 1, false, 0);
        // Farther targets wait longer (squared blocks).
        const double Distance = FVector::DistSquared(Target->FeetCm, Feet) / 1e4;
        Mob.RepathTicks = RepathMinTicks + Mob.Random.RandHelper(RepathRandomTicks) +
            (Distance > 1024         ? 10
                    : Distance > 256 ? 5
                                     : 0);
    }
    else if (Mob.bMoving)
        Mob.TargetCm = Goal;
    Mob.LookTicks = FMath::Max(Mob.LookTicks, 1);
    Mob.LookAtCm = Target->EyeCm - OriginCm;
    // LeapAtTargetGoal: on the ground, the target a few blocks away.
    if (bSpider && Mob.bOnGround)
    {
        const double Distance = FVector::Dist(Target->FeetCm, Feet);
        if (Distance >= LeapMinCm && Distance <= LeapMaxCm && Mob.Random.RandHelper(LeapChance) == 0)
        {
            const FVector2D Push = MobHorizontal(Target->FeetCm - Feet).GetSafeNormal() * LeapPushCm;
            Mob.VelocityCm.X = Push.X + Mob.VelocityCm.X * LeapKeep;
            Mob.VelocityCm.Y = Push.Y + Mob.VelocityCm.Y * LeapKeep;
            Mob.VelocityCm.Z = LeapUpCm;
        }
    }
    Mob.AttackCooldown = FMath::Max(Mob.AttackCooldown - 1, 0);
    if (Mob.AttackCooldown <= 0 && bSees && IsInMeleeReach(Mob, OriginCm, *Target))
    {
        Mob.AttackCooldown = MeleeIntervalTicks;
        Mob.AttackSwing = 0;
        Mob.bAttackNow = true;
    }
}

int32 FoundationMobs::GetCreatureCap(int32 EligibleColumns)
{
    return GetCategoryCap(EFoundationMobCategory::Creature, EligibleColumns);
}

EFoundationMobCategory FoundationMobs::GetCategory(EFoundationMobType Type)
{
    return IsMonster(Type) ? EFoundationMobCategory::Monster : EFoundationMobCategory::Creature;
}

int32 FoundationMobs::GetBaseCap(EFoundationMobCategory Category)
{
    return Category == EFoundationMobCategory::Monster ? MonsterBaseCap : CreatureBaseCap;
}

int32 FoundationMobs::GetCategoryCap(EFoundationMobCategory Category, int32 EligibleColumns)
{
    return GetBaseCap(Category) * FMath::Max(0, EligibleColumns) / CapChunks;
}

bool FoundationMobs::RollSkyLight(FRandomStream& Random, int32 SkyLight)
{
    return SkyLight <= Random.RandHelper(SkyLightRandom);
}

bool FoundationMobs::RollMonsterLight(FRandomStream& Random, int32 BlockLight, int32 Light)
{
    if (BlockLight > MonsterBlockLightLimit) return false;
    return Light <= Random.RandRange(0, MonsterLightMax);
}

bool FoundationMobs::ShouldDespawn(FFoundationMob& Mob, double NearestCm)
{
    if (NearestCm > DespawnFarCm) return true;
    if (NearestCm <= DespawnNearCm)
    {
        Mob.NoActionTicks = 0;
        return false;
    }
    return Mob.NoActionTicks > DespawnIdleTicks && Mob.Random.RandHelper(DespawnChance) == 0;
}

bool FoundationMobs::CanClimb(EFoundationMobType Type)
{
    return Type == EFoundationMobType::Spider;
}

FVector2D FoundationMobs::GetSpiderLegSwing(int32 Pair, float Position, float Speed)
{
    static constexpr float Offsets[] = {0, UE_PI, UE_HALF_PI, UE_PI * 1.5f};
    const float Offset = Offsets[FMath::Clamp(Pair, 0, 3)];
    const float Yaw = -FMath::Cos(Position * LegPhase * 2 + Offset) * SpiderLegSwing * Speed;
    const float Roll = FMath::Abs(FMath::Sin(Position * LegPhase + Offset) * SpiderLegSwing) * Speed;
    return FVector2D(Yaw, Roll);
}

void FoundationMobs::OnHurt(FFoundationMob& Mob, const FVector& OriginCm, const FVector& AttackerWorldCm,
    bool bFreshHit, bool bByPlayer, int32 Player)
{
    const FFoundationMobKind& Kind = GetKind(Mob.Type);
    Mob.HurtTime = HurtTicks;
    if (bFreshHit)
    {
        FVector2D Away = MobHorizontal(OriginCm + Mob.LocalCm - AttackerWorldCm);
        // Standing on the attacker: a random direction, as the knockback needs one.
        if (Away.IsNearlyZero()) Away = FVector2D(Mob.Random.FRandRange(-1, 1), Mob.Random.FRandRange(-1, 1));
        ApplyKnockback(Mob.VelocityCm, Away, Mob.bOnGround, KnockbackCm * (1 - Kind.KnockbackResistance));
    }
    Mob.bHurtByEntity = true;
    Mob.HurtFromCm = AttackerWorldCm - OriginCm;
    Mob.NoActionTicks = 0;
    // [survival.r10.ai_defaults] HurtByTargetGoal: a monster turns on the player that hit it (ThinkHostile drops a
    // player it may not target). Alerting the others of its type waits.
    if (bByPlayer && IsMonster(Mob.Type) && Mob.Target != Player)
    {
        Mob.Target = Player;
        Mob.UnseenTicks = Mob.AttackCooldown = Mob.RepathTicks = 0;
    }
    if (Mob.Health.IsDead())
    {
        Mob.bKilledByPlayer = bByPlayer;
        Mob.Killer = Player;
    }
}

bool FoundationMobs::IsTemptItem(EFoundationMobType Type, FName Item)
{
    if (Item.IsNone()) return false;
    for (const TCHAR* Food : MobFoods(Type))
        if (Item == FName(Food)) return true;
    return false;
}

void FoundationMobs::Think(
    FFoundationMob& Mob, const FVector& OriginCm, TConstArrayView<FFoundationMobPlayer> Players, bool bCanGraze)
{
    Mob.bEatNow = false;
    if (!Mob.IsAlive()) return;
    const FFoundationMobKind& Kind = GetKind(Mob.Type);
    const FVector Feet = OriginCm + Mob.LocalCm;
    if (Mob.RepathTicks > 0) --Mob.RepathTicks;
    // Panic (priority 1) takes over from the other goals on a hit by an entity, and runs leg after leg while the hit
    // is recent.
    if (Mob.bHurtByEntity) Mob.SinceHurt = 0;
    const bool bRecent = Mob.SinceHurt < PanicMemoryTicks;
    ++Mob.SinceHurt;
    if (Kind.PanicSpeed > 0 && (Mob.bHurtByEntity || (bRecent && Mob.bPanicking && !Mob.bMoving)))
    {
        MobStartMove(Mob, MobRandomTarget(Mob, Mob.LocalCm, PanicRangeCells, PanicRangeUpCells), Kind.PanicSpeed, true,
            PanicRangeUpCells);
        Mob.GrazeTicks = 0;
    }
    Mob.bHurtByEntity = false;
    if (!Mob.bMoving) Mob.bPanicking = false;
    // Tempt (priority 3-4): the nearest player in range with a breeding food in either hand; it walks to the player
    // and stops close by.
    const FFoundationMobPlayer* Tempter = nullptr;
    if (Kind.TemptSpeed > 0 && !Mob.bPanicking)
    {
        double Best = TemptRangeCm * TemptRangeCm;
        for (const FFoundationMobPlayer& Player : Players)
            if (IsTemptItem(Mob.Type, Player.MainHand) || IsTemptItem(Mob.Type, Player.Offhand))
                if (const double Distance = FVector::DistSquared(Player.FeetCm, Feet); Distance < Best)
                {
                    Best = Distance;
                    Tempter = &Player;
                }
    }
    if (Tempter)
    {
        Mob.GrazeTicks = 0;
        const FVector Goal = Tempter->FeetCm - OriginCm;
        if (FVector::DistSquared(Tempter->FeetCm, Feet) < TemptStopCm * TemptStopCm)
            MobStopMove(Mob);
        else if (Mob.TemptedBy != Tempter->Id || (!Mob.bMoving && Mob.RepathTicks <= 0))
            MobStartMove(Mob, Goal, Kind.TemptSpeed, false, 0);
        else if (Mob.bMoving)
        {
            // The player moves: steer at it, and plan again once it left the cell the path was planned to.
            if (MobCellOf(Goal) != Mob.PathGoal && Mob.RepathTicks <= 0) Mob.bNeedPath = true;
            Mob.TargetCm = Goal;
        }
        Mob.TemptedBy = Tempter->Id;
    }
    else if (Mob.TemptedBy != 0)
    {
        Mob.TemptedBy = 0;
        if (!Mob.bPanicking) MobStopMove(Mob);
    }
    // Grazing (the sheep, priority 5) holds the movement while it eats; the grass goes GrazeEatTicks before the end.
    if (Mob.GrazeTicks > 0)
    {
        --Mob.GrazeTicks;
        Mob.bEatNow = Mob.GrazeTicks == GrazeEatTicks;
        if (Mob.bMoving) MobStopMove(Mob);
    }
    else if (Mob.Type == EFoundationMobType::Sheep && !Mob.bPanicking && !Tempter && bCanGraze &&
        Mob.Random.RandHelper(GrazeChance) == 0)
    {
        Mob.GrazeTicks = GrazeLengthTicks;
        MobStopMove(Mob);
    }
    if (!Mob.bMoving && Mob.GrazeTicks == 0 && !Tempter && Mob.Target == 0 && Mob.Random.RandHelper(StrollChance) == 0)
        MobStartMove(Mob, MobRandomTarget(Mob, Mob.LocalCm, StrollRangeCells, StrollRangeUpCells), Kind.StrollSpeed,
            false, StrollRangeUpCells);
    // Look-at-player (priority 6) runs beside the movement goals; a tempted mob looks at its player.
    const FVector Eye = Feet + FVector(0, 0, Kind.EyeCm);
    const FVector* Nearest = nullptr;
    double Best = Kind.LookCm * Kind.LookCm;
    for (const FFoundationMobPlayer& Player : Players)
        if (const double Distance = FVector::DistSquared(Player.EyeCm, Eye); Distance < Best)
        {
            Best = Distance;
            Nearest = &Player.EyeCm;
        }
    if (Mob.LookTicks > 0)
        --Mob.LookTicks;
    else if (Nearest && Mob.Random.GetFraction() < LookChance)
        Mob.LookTicks = LookMinTicks + Mob.Random.RandHelper(LookRandomTicks);
    if (Nearest && Mob.LookTicks > 0)
        Mob.LookAtCm = *Nearest - OriginCm;
    else
        Mob.LookTicks = 0;
    if (Tempter)
    {
        Mob.LookTicks = FMath::Max(Mob.LookTicks, 1);
        Mob.LookAtCm = Tempter->EyeCm - OriginCm;
    }
    // A monster with a target looks at it (ThinkHostile set the point).
    if (Mob.Target != 0)
        if (const FFoundationMobPlayer* Target =
                Players.FindByPredicate([&Mob](const FFoundationMobPlayer& Player) { return Player.Id == Mob.Target; }))
        {
            Mob.LookTicks = FMath::Max(Mob.LookTicks, 1);
            Mob.LookAtCm = Target->EyeCm - OriginCm;
        }
}

void FoundationMobs::PlanPath(FFoundationMob& Mob, TFunctionRef<FFoundationPathCell(const FIntVector&)> Query)
{
    Mob.bNeedPath = false;
    Mob.Path.Reset();
    Mob.PathIndex = 0;
    Mob.bPathPartial = false;
    Mob.RepathTicks = TemptRepathTicks;
    if (!Mob.bMoving) return;
    const double Height = GetKind(Mob.Type).HeightCm / 100;
    FIntVector Goal = MobCellOf(Mob.TargetCm);
    if (Mob.TargetUpCells > 0)
    {
        // The stand cell of the goal's column nearest the drawn height: 0, +1, -1, +2, -2 ...
        bool bFound = false;
        double Feet = 0;
        for (int32 Offset = 0; Offset <= 2 * Mob.TargetUpCells && !bFound; ++Offset)
        {
            const FIntVector Cell = Goal + FIntVector(0, 0, (Offset + 1) / 2 * (Offset % 2 ? 1 : -1));
            if (FoundationMobPath::GetStand(Query, Cell, Height, Feet))
            {
                Goal = Cell;
                bFound = true;
            }
        }
        if (!bFound)
        {
            MobStopMove(Mob);
            return;
        }
        Mob.TargetCm = FVector((Goal.X + .5) * 100, (Goal.Y + .5) * 100, Feet * 100);
        Mob.TargetUpCells = 0;
    }
    Mob.PathGoal = Goal;
    Mob.bPathPartial = !FoundationMobPath::FindPath(Query, MobCellOf(Mob.LocalCm), Goal, Height, Mob.Path);
    if (Mob.bPathPartial && Mob.Path.IsEmpty()) MobStopMove(Mob);
}

FVector2D FoundationMobs::Steer(FFoundationMob& Mob, const FVector& OriginCm, bool& bOutJump)
{
    bOutJump = false;
    if (!Mob.IsAlive() || !Mob.bMoving) return FVector2D::ZeroVector;
    // The next waypoint not yet reached (a stand cell's centre); after the last, the target itself. A path not planned
    // yet steers straight at the target.
    FVector Aim = Mob.TargetCm;
    for (; Mob.PathIndex < Mob.Path.Num(); ++Mob.PathIndex)
    {
        const FIntVector& Cell = Mob.Path[Mob.PathIndex];
        const FVector Centre((Cell.X + .5) * 100, (Cell.Y + .5) * 100, Cell.Z * 100);
        if (MobHorizontal(Centre - Mob.LocalCm).Size() >= ArriveCm)
        {
            Aim = Centre;
            break;
        }
    }
    const bool bLast = Mob.PathIndex >= Mob.Path.Num();
    const FVector2D ToTarget = MobHorizontal(Aim - Mob.LocalCm);
    const double Distance = ToTarget.Size();
    const double Before = MobHorizontal(Aim - Mob.PreviousLocalCm).Size();
    Mob.StuckTicks = Before - Distance < StuckCm ? Mob.StuckTicks + 1 : 0;
    // A partial path ends at its last waypoint; a whole one at the target.
    if ((bLast && (Distance < ArriveCm || (Mob.bPathPartial && !Mob.Path.IsEmpty()))) ||
        Mob.StuckTicks >= GiveUpTicks || ++Mob.MoveTicks > MaxMoveTicks)
    {
        MobStopMove(Mob);
        return FVector2D::ZeroVector;
    }
    const float Heading = FMath::RadiansToDegrees(FMath::Atan2(ToTarget.Y, ToTarget.X));
    Mob.Yaw = FMath::UnwindDegrees(MobTurnToward(Mob.Yaw, Heading, BodyTurnDegrees));
    // Blocked on the ground past the step-up: jump.
    bOutJump = Mob.bOnGround && Mob.bHorizontalCollision && !CanClimb(Mob.Type);
    return ToTarget / Distance;
}

void FoundationMobs::Move(
    FFoundationMob& Mob, const FVector& OriginCm, const TArray<FBox>& Boxes, const FVector2D& InputCm, bool bJump)
{
    Mob.PreviousLocalCm = Mob.LocalCm;
    const bool bWasGrounded = Mob.bOnGround;
    FVector& Velocity = Mob.VelocityCm;
    // [survival.r10.idle_and_sun] a chicken in the air falls slowly: its downward speed x0.6 before the move.
    if (Mob.Type == EFoundationMobType::Chicken && !bWasGrounded && Velocity.Z < 0) Velocity.Z *= ChickenFallFactor;
    // A climber against a wall (since the last step) falls slowly (Codex 02 C, provisional).
    const bool bClimbing = CanClimb(Mob.Type) && Mob.bHorizontalCollision;
    if (bClimbing) Velocity.Z = FMath::Max(Velocity.Z, -ClimbFallCm);
    const double Drive = GetKind(Mob.Type).Speed * Mob.SpeedModifier;
    const FVector2D Push = InputCm * (bWasGrounded ? Drive * Drive * WalkAccelCm : AirAccelCm);
    Velocity.X += Push.X;
    Velocity.Y += Push.Y;
    if (bJump && bWasGrounded) Velocity.Z = JumpCm;
    const FBox Box = GetBox(Mob.Type, OriginCm + Mob.LocalCm);
    FVector Moved = FFoundationSimulation::Clip(Box, Velocity, Boxes);
    const auto Blocked = [&Velocity](const FVector& Delta) { return Delta.X != Velocity.X || Delta.Y != Velocity.Y; };
    bool bStepped = false;
    // [mobs.base_attributes] a grounded mob blocked sideways climbs up to the step height: up, across, back down.
    if (bWasGrounded && Blocked(Moved) && Velocity.Z <= 0)
    {
        const FVector Up = FFoundationSimulation::Clip(Box, FVector(0, 0, StepCm), Boxes);
        const FVector Across = FFoundationSimulation::Clip(Box.ShiftBy(Up), FVector(Velocity.X, Velocity.Y, 0), Boxes);
        const FVector Down = FFoundationSimulation::Clip(
            Box.ShiftBy(Up + Across), FVector(0, 0, -Up.Z + FMath::Min(Velocity.Z, 0.)), Boxes);
        const FVector Stepped = Up + Across + Down;
        if (MobHorizontal(Stepped).SizeSquared() > MobHorizontal(Moved).SizeSquared() + 1e-6 &&
            Down.Z > -Up.Z + Velocity.Z)
        {
            Moved = Stepped;
            bStepped = true;
        }
    }
    Mob.LocalCm += Moved;
    Mob.bHorizontalCollision = Blocked(Moved) && !bStepped;
    Mob.bOnGround = bStepped || (Velocity.Z < 0 && Moved.Z != Velocity.Z);
    if (Moved.X != Velocity.X && !bStepped) Velocity.X = 0;
    if (Moved.Y != Velocity.Y && !bStepped) Velocity.Y = 0;
    if (Moved.Z != Velocity.Z && !bStepped) Velocity.Z = 0;
    if (bStepped) Velocity.Z = 0;
    // Still pressed against the wall it was climbing, it goes up.
    if (bClimbing && Mob.bHorizontalCollision) Velocity.Z = ClimbUpCm;
    // The ground's retention follows where the step began (the scout's kb_probe: 0.546 for the step the hit landed on
    // the ground, then 0.91 in the air).
    const double Retention = bWasGrounded ? GroundRetention : AirRetention;
    Velocity.X *= Retention;
    Velocity.Y *= Retention;
    Velocity.Z = (Velocity.Z - GravityCm) * VerticalDrag;
}

void FoundationMobs::Animate(FFoundationMob& Mob)
{
    Mob.PreviousWalkPosition = Mob.WalkPosition;
    Mob.PreviousWalkSpeed = Mob.WalkSpeed;
    Mob.PreviousHeadYaw = Mob.HeadYaw;
    Mob.PreviousHeadPitch = Mob.HeadPitch;
    const double Blocks = MobHorizontal(Mob.LocalCm - Mob.PreviousLocalCm).Size() / 100;
    const float Wanted = FMath::Min(1.f, float(Blocks) * WalkSpeedScale);
    Mob.WalkSpeed += (Wanted - Mob.WalkSpeed) * WalkSpeedSmoothing;
    Mob.WalkPosition += Mob.WalkSpeed;
    // [survival.r10.idle_and_sun] the chicken's wings: the flap speed falls on the ground and rises in the air; in the
    // air the strength is at least 1, then decays by 0.9; the phase advances by twice the strength.
    Mob.PreviousFlap = Mob.Flap;
    Mob.PreviousFlapSpeed = Mob.FlapSpeed;
    if (Mob.Type == EFoundationMobType::Chicken)
    {
        Mob.FlapSpeed = FMath::Clamp(Mob.FlapSpeed + (Mob.bOnGround ? FlapGround : FlapAir), 0.f, 1.f);
        if (!Mob.bOnGround && Mob.FlapStrength < 1) Mob.FlapStrength = 1;
        Mob.FlapStrength *= FlapDecay;
        Mob.Flap += Mob.FlapStrength * 2;
    }
    // The head: toward the looked-at player, else back over the body; the yaw within HeadYawLimit of the body.
    const FFoundationMobKind& Kind = GetKind(Mob.Type);
    float WantedYaw = Mob.Yaw, WantedPitch = 0;
    if (Mob.LookTicks > 0)
    {
        const FVector Delta = Mob.LookAtCm - (Mob.LocalCm + FVector(0, 0, Kind.EyeCm));
        WantedYaw = FMath::RadiansToDegrees(FMath::Atan2(Delta.Y, Delta.X));
        WantedPitch = FMath::RadiansToDegrees(-FMath::Atan2(Delta.Z, MobHorizontal(Delta).Size()));
    }
    Mob.HeadYaw = MobTurnToward(Mob.HeadYaw, WantedYaw, HeadTurnDegrees);
    const float Relative =
        FMath::Clamp(FMath::FindDeltaAngleDegrees(Mob.Yaw, Mob.HeadYaw), -HeadYawLimit, HeadYawLimit);
    Mob.HeadYaw = FMath::UnwindDegrees(Mob.Yaw + Relative);
    Mob.HeadPitch =
        FMath::Clamp(MobTurnToward(Mob.HeadPitch, WantedPitch, HeadTurnDegrees), -HeadPitchLimit, HeadPitchLimit);
}

float FoundationMobs::GetDeathRoll(float DeathTime, float MaxDegrees)
{
    if (DeathTime <= 1) return 0;
    return MaxDegrees * FMath::Min(1.f, FMath::Sqrt((DeathTime - 1) * 1.6f / 20.f));
}

float FoundationMobs::GetLegPitch(int32 Leg, float Position, float Speed)
{
    // Right hind and left front swing together; the other pair half a cycle later.
    const float Offset = Leg == 1 || Leg == 2 ? UE_PI : 0;
    return FMath::Cos(Position * LegPhase + Offset) * LegAmplitude * Speed;
}

float FoundationMobs::GetBipedLegPitch(int32 Leg, float Position, float Speed)
{
    return FMath::Cos(Position * LegPhase + (Leg == 1 ? UE_PI : 0)) * LegAmplitude * Speed;
}

float FoundationMobs::GetWingRoll(float Flap, float FlapSpeed)
{
    return (FMath::Sin(Flap) + 1) * FlapSpeed;
}

float FoundationMobs::GetGrazeDrop(float Ticks)
{
    // Down over the first GrazeEatTicks (40 to 36 left), held, up over the last ones (4 to 0 left).
    if (Ticks <= 0) return 0;
    if (Ticks < GrazeEatTicks) return Ticks / GrazeEatTicks;
    if (Ticks > GrazeLengthTicks - GrazeEatTicks) return (GrazeLengthTicks - Ticks) / GrazeEatTicks;
    return 1;
}

float FoundationMobs::GetGrazePitch(float Ticks, float LookRadians)
{
    // pi/5 + 0.21991149 sin(((ticks - 4) / 32) x 28.7) while eating, pi/5 at the ends.
    if (Ticks > GrazeEatTicks && Ticks <= GrazeLengthTicks - GrazeEatTicks)
        return UE_PI / 5 + .21991149f * FMath::Sin((Ticks - GrazeEatTicks) / 32.f * 28.7f);
    return Ticks > 0 ? UE_PI / 5 : LookRadians;
}

float FoundationMobs::GetSoundPitch(float U, float V)
{
    return 1 + .2f * (U - V);
}
