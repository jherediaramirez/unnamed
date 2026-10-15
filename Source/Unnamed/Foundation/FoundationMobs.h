#pragma once

#include "CoreMinimal.h"
#include "Foundation/FoundationMobPath.h"
#include "Foundation/FoundationVitals.h"
#include "Math/RandomStream.h"

// Mobs (survival T6 20a; scout C:\dev\sandbox\scout-mobs\facts.md, Codex T6 oracles). The authority owns the list and
// steps it once per reference step after the item entities; FFoundationMobView only draws it. Positions are cm from
// the block world's origin; velocities cm per reference step (1 block = 100 cm, 1 tick = 1 step). Sources in brackets
// are evidence ids of the value report (MINECRAFT-26.3-VALUES.md, V) and SURVIVAL-FINDINGS-26.3.md (S); "provisional"
// values have a row in C:\dev\tools\shift-managers\survival\provisional-values.md.

// The eight mobs of track T6. The four farm animals are spawnable since 20b; the others are data (kinds, models, loot)
// for 20c-20d.
enum class EFoundationMobType : uint8
{
    Cow,
    Pig,
    Sheep,
    Chicken,
    Zombie,
    Skeleton,
    Creeper,
    Spider,
    Count
};

struct FFoundationMobKind
{
    // The entity id ("cow"): its loot table entities/<Id>, its model "<Id>#main", its sounds entity.<Id>.*.
    FName Id;
    // [mobs.base_attributes, catalog.entities] health, the movement attribute (scaled by the AI's speed modifiers; not
    // a speed in blocks per tick), armor, toughness, knockback resistance, the box and the eyes.
    float MaxHealth = 10, Speed = .2f, Armor = 0, Toughness = 0, KnockbackResistance = 0;
    double WidthCm = 90, HeightCm = 140, EyeCm = 130;
    // [survival.r10.ai_defaults] the panic and tempt goals' speed modifiers (0: no such goal).
    float PanicSpeed = 0, TemptSpeed = 0;
    // [survival.r5.play_calls] ambient, hurt and death volume (steps play at StepVolume).
    float Volume = 1;
    // [survival.r10.death_roll] the death roll's maximum, degrees.
    float DeathRollDegrees = 90;
    // Experience a player kill pays, inclusive (minecraft.wiki "Experience", provisional).
    int32 ExperienceMin = 1, ExperienceMax = 3;
    // foundation.Mobs.Summon accepts it (20b: the four farm animals).
    bool bSpawnable = false;
};

// A live player as the mobs' AI sees it (world cm): its eyes, its feet and the item ids in its hands.
struct FFoundationMobPlayer
{
    int32 Id = 0;
    FVector EyeCm = FVector::ZeroVector, FeetCm = FVector::ZeroVector;
    FName MainHand, Offhand;
};

struct FFoundationMob
{
    int32 Id = 0;
    EFoundationMobType Type = EFoundationMobType::Cow;
    // The bottom centre of its box now and one step ago (the view draws between the two).
    FVector LocalCm = FVector::ZeroVector, PreviousLocalCm = FVector::ZeroVector;
    FVector VelocityCm = FVector::ZeroVector;
    // Body and head yaw (degrees, Unreal: 0 faces +X, 90 faces +Y) and head pitch (positive looks down), now and one
    // step ago.
    float Yaw = 0, PreviousYaw = 0, HeadYaw = 0, PreviousHeadYaw = 0, HeadPitch = 0, PreviousHeadPitch = 0;
    // Health, the damage cooldown and its repeated-hit rule [hurt.cooldown, hurt.repeated_hits], armor.
    FFoundationVitals Health;
    // [hurt.overlay] counts down from HurtTicks after an accepted hit; DeathTime counts up from the killing hit.
    int32 HurtTime = 0, DeathTime = 0;
    // The killing hit came from a player (Killer, its id): the loot's killed_by_player and the experience.
    bool bKilledByPlayer = false;
    int32 Killer = 0;
    // The walk animation's phase and speed input [survival.r10.model_motion], now and one step ago.
    float WalkPosition = 0, WalkSpeed = 0, PreviousWalkPosition = 0, PreviousWalkSpeed = 0;
    // Navigation: the goal it heads for (world-origin cm), the AI speed modifier, and how long it has been on it.
    bool bMoving = false, bPanicking = false;
    FVector TargetCm = FVector::ZeroVector;
    float SpeedModifier = 1;
    int32 MoveTicks = 0, StuckTicks = 0;
    // The goal is a random point (stroll, panic): PlanPath settles it on a stand cell of its column within
    // TargetUpCells of the drawn height.
    int32 TargetUpCells = 0;
    // A* (FoundationMobPath): the stand cells toward TargetCm and the next one; bNeedPath asks StepMobs to plan;
    // bPathPartial: the path ends short of the goal (navigation is done at its end). Steps until a moving goal (tempt)
    // may plan again.
    TArray<FIntVector> Path;
    FIntVector PathGoal = FIntVector::ZeroValue;
    int32 PathIndex = 0, RepathTicks = 0;
    bool bNeedPath = false, bPathPartial = false;
    // Tempt: the id of the player it follows (0: none).
    int32 TemptedBy = 0;
    // Sheep [survival.r10.idle_and_sun]: the grazing countdown (GrazeTicks to 0); bEatNow: eat the grass this step.
    int32 GrazeTicks = 0;
    bool bEatNow = false;
    // Chicken [survival.r10.idle_and_sun]: the wings' flap phase, speed and strength, now and one step ago; steps to
    // the next egg.
    float Flap = 0, FlapSpeed = 0, FlapStrength = 0, PreviousFlap = 0, PreviousFlapSpeed = 0;
    int32 EggTicks = 0;
    // Look-at-player: ticks left and the point (world-origin cm).
    int32 LookTicks = 0;
    FVector LookAtCm = FVector::ZeroVector;
    // A hit by an attacker standing at HurtFromCm waits for the next step's panic goal.
    bool bHurtByEntity = false;
    FVector HurtFromCm = FVector::ZeroVector;
    // Steps since the last hit by an entity (large: never).
    int32 SinceHurt = MAX_int32 / 2;
    // Sounds: ticks to the next ambient roll; distance walked since the last step sound.
    int32 AmbientTicks = 0;
    double StepDistanceCm = 0;
    int32 Age = 0;
    bool bOnGround = false, bHorizontalCollision = false;
    // Its column is not loaded: it neither moves, thinks nor counts its timers, and is not drawn.
    bool bFrozen = false;
    FRandomStream Random;

    bool IsAlive() const { return !Health.IsDead(); }
};

namespace FoundationMobs
{
    // foundation.Mobs: 1 (the default) mobs exist (foundation.Mobs.Summon, hits, drops); 0 none are spawned, stepped
    // or drawn and Resolve sees training targets only, as before.
    UNNAMED_API bool IsOn();

    UNNAMED_API const FFoundationMobKind& GetKind(EFoundationMobType Type);
    // The type of an entity id ("cow"); false for an unknown id.
    UNNAMED_API bool FindType(FName Id, EFoundationMobType& OutType);

    // Mob ids start here, apart from the training targets' (Resolve and Attack tell them apart by id).
    constexpr int32 FirstId = 1 << 20;

    // Physics, cm per step. [mobs.base_attributes] step height 0.6 blocks. Provisional (Codex T6 section C; the scout's
    // probe reads the gravity and jump_strength attributes 0.08 and 0.42): gravity 0.08 blocks per tick squared, then
    // vertical drag 0.98; horizontal retention 0.546 on the ground (block friction 0.6 x 0.91) and 0.91 in the air,
    // applied after the move (the scout's kb_probe track, O5).
    constexpr double GravityCm = 8, VerticalDrag = .98, GroundRetention = .546, AirRetention = .91;
    constexpr double JumpCm = 42, StepCm = 60;
    // Black-box (C:\dev\sandbox\mobs-20a\speed_probe, a 26.3 server, cow on grass): a walking mob accelerates by
    // (attribute x modifier)^2 blocks per step on the ground (0.04 strolling, 0.16 panicking; plateaus 0.0881 and 0.352
    // blocks per tick). Provisional (wiki "Entity", not measured): AirAccelCm per step in the air.
    constexpr double WalkAccelCm = 100, AirAccelCm = 2;
    // Below this the mob is falling past the void and is removed.
    constexpr int32 VoidDepthCells = 64;

    // [movement.knockback_resistance, extra.motion, future.combat] a player's hit: power 0.4 blocks per tick times
    // (1 - resistance).
    constexpr double KnockbackCm = 40;
    // [hurt.cooldown] the hurt animation's length; [mobs.hurt_overlay] the red overlay while HurtTime or DeathTime > 0.
    constexpr int32 HurtTicks = 10;
    // Provisional (no reference; Codex T6 section C): the corpse is removed and its loot and experience drop at
    // DeathTime 20.
    constexpr int32 RemoveAtDeathTime = 20;

    // AI [survival.r10.ai_rules, survival.r10.ai_defaults]. The stroll goal starts when Random.RandHelper(StrollChance)
    // is 0 (a random divisor, not a period) toward a point within 10 blocks horizontally and 7 vertically; panic runs
    // at the kind's modifier toward a point within 5 / 4, until its navigation finishes. Look-at-player: a player
    // within 6 blocks, chance 0.02 per step. Head: pitch limit 40, yaw limit 75 from the body, 10 degrees per step.
    constexpr int32 StrollChance = 120;
    constexpr int32 StrollRangeCells = 10, StrollRangeUpCells = 7, PanicRangeCells = 5, PanicRangeUpCells = 4;
    constexpr double LookRangeCm = 600, LookChance = .02;
    constexpr float HeadPitchLimit = 40, HeadYawLimit = 75, HeadTurnDegrees = 10;
    // Provisional (the speed probe: a hit cow ran 2-3 legs, stopping 40-62 steps after the hit): panic picks a new
    // target when its navigation finishes within PanicMemoryTicks of the last hit.
    constexpr int32 PanicMemoryTicks = 40;
    // Provisional: a waypoint or the target is reached within ArriveCm horizontally; navigation gives up after
    // GiveUpTicks without StuckCm of progress, or after MaxMoveTicks; looking lasts 40-79 steps; the body turns at most
    // BodyTurnDegrees per step toward its heading.
    constexpr double ArriveCm = 50, StuckCm = 1;
    constexpr int32 GiveUpTicks = 40, MaxMoveTicks = 400, LookMinTicks = 40, LookRandomTicks = 40;
    constexpr float BodyTurnDegrees = 90;
    // [survival.r10.ai_rules] tempt: a player within TemptRangeCm (the tempt_range attribute 10, provisional: Codex T6
    // section C) holding one of the kind's breeding foods [survival.r10.ai_defaults] in either hand; navigation stops
    // within TemptStopCm. Provisional: a tempted mob plans again at most every TemptRepathTicks when its player's cell
    // changed.
    constexpr double TemptRangeCm = 1000, TemptStopCm = 250;
    constexpr int32 TemptRepathTicks = 10;
    // The scout's gate (facts section 6): at most PathsPerStep A* searches per step over all mobs; the rest steer
    // straight and plan on a later step.
    constexpr int32 PathsPerStep = 4;
    // [survival.r10.idle_and_sun] sheep: grazing starts on Random.RandHelper(GrazeChance) == 0 (an adult) standing in
    // short grass or on a grass block, lasts GrazeLengthTicks and eats when GrazeEatTicks are left; the head drops up
    // to GrazeDropPixels. Chicken: a falling chicken's vy x ChickenFallFactor before its move; the flap speed changes
    // by FlapGround or FlapAir per step (0-1); an egg every EggMinTicks + [0, EggRandomTicks) steps
    // [survival.r10.egg_initial: uniform 6000-11999].
    constexpr int32 GrazeChance = 1000, GrazeLengthTicks = 40, GrazeEatTicks = 4;
    constexpr float GrazeDropPixels = 9;
    constexpr double ChickenFallFactor = .6;
    constexpr float FlapGround = -.3f, FlapAir = 1.2f, FlapDecay = .9f;
    constexpr int32 EggMinTicks = 6000, EggRandomTicks = 6000;

    // [survival.r10.model_motion] legs swing by cos(position x 0.6662) x 1.4 x speed radians, opposite legs + pi.
    constexpr float LegPhase = .6662f, LegAmplitude = 1.4f;
    // Provisional (no measured calibration, S R10): the walk speed input approaches min(1, blocks moved per step x
    // WalkSpeedScale) by WalkSpeedSmoothing each step; the position advances by the speed.
    constexpr float WalkSpeedScale = 4, WalkSpeedSmoothing = .4f;

    // [survival.r5.play_calls] steps at volume 0.15; pitch 1 + 0.2 (U - V). Provisional (wiki "Cow", Sounds; no
    // interval anywhere): an ambient sound is tried every AmbientMinTicks plus a random 0-AmbientRandomTicks steps; a
    // step sound every StepSoundCm walked on the ground.
    constexpr float StepVolume = .15f;
    constexpr int32 AmbientMinTicks = 80, AmbientRandomTicks = 120;
    constexpr double StepSoundCm = 100;

    // The box of a mob standing at a world position (its bottom centre).
    UNNAMED_API FBox GetBox(EFoundationMobType Type, const FVector& WorldCm);
    // The world region one step's motion can touch, the step-up included.
    UNNAMED_API FBox GetStepRegion(const FFoundationMob& Mob, const FVector& OriginCm);
    // [extra.motion] a grounded mob's velocity after a hit pushing it along Away (horizontal, any length) with Power
    // cm per step: half the old horizontal velocity plus the impulse; vertically min(Power, old / 2 + Power). An
    // airborne mob keeps its vertical velocity.
    UNNAMED_API void ApplyKnockback(FVector& VelocityCm, const FVector2D& Away, bool bGrounded, double PowerCm);
    // An accepted hit from an attacker at AttackerWorldCm: the hurt animation, knockback on a fresh hit, the panic
    // goal's trigger, and the killer when it died.
    UNNAMED_API void OnHurt(FFoundationMob& Mob, const FVector& OriginCm, const FVector& AttackerWorldCm,
        bool bFreshHit, bool bByPlayer, int32 Player);
    // [survival.r10.ai_defaults] the kind's breeding foods, which tempt it (hardcoded: the item tags are not in a
    // reference yet).
    UNNAMED_API bool IsTemptItem(EFoundationMobType Type, FName Item);
    // The AI's choice for this step by priority [survival.r10.ai_defaults]: panic, tempt, grazing (a sheep that
    // bCanGraze where it stands), stroll; and the head's look. Players: the live players.
    UNNAMED_API void Think(
        FFoundationMob& Mob, const FVector& OriginCm, TConstArrayView<FFoundationMobPlayer> Players, bool bCanGraze);
    // Plans Mob.Path toward Mob.TargetCm with A* over the cells Query describes; a random target first settles on the
    // stand cell of its column nearest the drawn height. No stand cell there: navigation is done.
    UNNAMED_API void PlanPath(FFoundationMob& Mob, TFunctionRef<FFoundationPathCell(const FIntVector&)> Query);
    // The steering direction toward the next waypoint, then the target (unit, or zero when not moving), and whether to
    // jump.
    UNNAMED_API FVector2D Steer(FFoundationMob& Mob, const FVector& OriginCm, bool& bOutJump);
    // One step of motion against world boxes (cm): input acceleration, the move with a 0.6-block step-up, gravity and
    // the drags. Boxes from FFoundationAuthority::GatherEntityCollision over GetStepRegion.
    UNNAMED_API void Move(
        FFoundationMob& Mob, const FVector& OriginCm, const TArray<FBox>& Boxes, const FVector2D& InputCm, bool bJump);
    // The walk animation and the head's turn after the move.
    UNNAMED_API void Animate(FFoundationMob& Mob);
    // [survival.r10.death_roll] the roll (degrees) at a fractional death time.
    UNNAMED_API float GetDeathRoll(float DeathTime, float MaxDegrees);
    // [survival.r10.model_motion] a quadruped leg's pitch (radians): legs 0-3 right hind, left hind, right front, left
    // front.
    UNNAMED_API float GetLegPitch(int32 Leg, float Position, float Speed);
    // [survival.r10.model_motion] a biped's leg (0 right, 1 left), the chicken's.
    UNNAMED_API float GetBipedLegPitch(int32 Leg, float Position, float Speed);
    // [survival.r10.idle_and_sun] the chicken's wing roll (radians, the right wing's; the left's is its negative):
    // (sin(flap) + 1) x flap speed.
    UNNAMED_API float GetWingRoll(float Flap, float FlapSpeed);
    // [survival.r10.idle_and_sun] a grazing sheep's head at a fractional countdown (GrazeTicks - Alpha): how far it has
    // dropped (0-1, of GrazeDropPixels) and its pitch (radians; Look outside the eating middle when not grazing).
    UNNAMED_API float GetGrazeDrop(float Ticks);
    UNNAMED_API float GetGrazePitch(float Ticks, float LookRadians);
    // [survival.r5.play_calls] 1 + 0.2 (U - V).
    UNNAMED_API float GetSoundPitch(float U, float V);
}
