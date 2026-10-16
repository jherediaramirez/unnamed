#pragma once

#include "CoreMinimal.h"

// The player's game type (Java's GameType). Unrelated to Unreal's AGameMode class.
enum class EFoundationPlayerMode : uint8
{
    Survival,
    Creative,
    Adventure,
    Spectator
};

// The world's difficulty, in the reference's order. Peaceful's rules (no mob damage; with natural regeneration, a
// point of health every second and of food every half second) are the documented mechanic: the pinned sources leave
// that regeneration to the server player.
enum class EFoundationDifficulty : uint8
{
    Peaceful,
    Easy,
    Normal,
    Hard
};

// Damage types the lab can cause. Each carries its 26.3 data: armor bypass, invulnerability
// bypass, difficulty scaling and food exhaustion.
enum class EFoundationDamage : uint8
{
    Generic,
    MobAttack,
    Fall,
    Drown,
    Starve,
    Kill,
    // A player's melee hit: armor applies, no difficulty scaling (26.3 player_attack).
    PlayerAttack,
    // Lava contact (armor applies) and burning (bypasses armor), step 16.
    Lava,
    OnFire,
    // An arrow (step 19c): armor applies; 26.3's arrow damage type (exhaustion 0.1, scaled by difficulty only from a
    // living non-player: T6 20d's skeleton).
    Arrow,
    // Fire and soul fire block contact (armor applies), survival step 16c.
    InFire
};

// One reference tick of facts from movement and the lab world. Distances are centimeters.
struct FFoundationVitalsStep
{
    FVector DisplacementCm = FVector::ZeroVector;
    bool bGrounded = false, bSprinting = false;
    bool bJumped = false, bSprintJumped = false;
    bool bBodyInWater = false, bEyesInWater = false;
    // The step climbed (FFoundationSimulation::bClimbing): no fall builds, as in water.
    bool bClimbing = false;
    // The player's foundation.Fire switch. Other holders keep the existing hazards unless explicitly disabled.
    bool bFireEnabled = true;
    // The body touches lava (the pawn passes it only with foundation.Fire 1).
    bool bBodyInLava = false;
    // Fire block contact (step 16c, foundation.FireBlocks): the in_fire damage of the hottest fire cell the body
    // touches (fire 1, soul fire 2; 0 = none), and the body inside a lava cauldron's lava (hurts as lava does).
    int32 FireBlockDamage = 0;
    bool bLavaContact = false;
};

// Session-only survival state on the 20 Hz reference tick: health, absorption, armor, food,
// air, experience and attack recovery. Pure rules from the pinned Java 26.3 sources: the pawn
// feeds it movement and world facts, the HUD reads it. Floats mirror Java's arithmetic.
struct UNNAMED_API FFoundationVitals
{
    static constexpr int32 RespawnTicks = 20;
    // Lava ignites for 15 s [hurt.lava_ignite]; burning hurts every 20 ticks [hurt.burning].
    static constexpr int32 LavaFireTicks = 300;
    static constexpr int32 BurnIntervalTicks = 20;
    // Fire blocks ignite a player for 8 s, after 20 ticks of contact [survival2.r14.player_fire].
    static constexpr int32 FireBlockTicks = 160;
    static constexpr int32 PlayerFireGraceTicks = 20;

    // Lab configuration, kept across respawn. Armor and toughness are the attributes' base values (0 for a player);
    // the worn pieces add theirs (survival step 19a). Attack speed stands in for the held item's.
    EFoundationPlayerMode Mode = EFoundationPlayerMode::Survival;
    EFoundationDifficulty Difficulty = EFoundationDifficulty::Normal;
    bool bNaturalRegeneration = true;
    float MaxHealth = 20, ArmorToughness = 0, AttackSpeed = 4;
    int32 Armor = 0;
    // The worn armor pieces' modifiers, set by the holder every step (FFoundationAuthority::GetWornArmor).
    int32 WornArmor = 0;
    float WornToughness = 0;
    // Armor wear owed by the hurts since the holder last took it: each hurt armor applies to costs every worn piece
    // max(1, floor(damage / 4)) (minecraft.wiki "Armor", Durability; provisional).
    int32 PendingArmorWear = 0;
    // The part of PendingArmorWear owed by #minecraft:is_fire damage (lava): fire-resistant pieces skip it.
    int32 PendingFireArmorWear = 0;

    // Player state, restored by Respawn().
    float Health = 20, Absorption = 0, Saturation = 5, Exhaustion = 0, Progress = 0, LastHurt = 0;
    int32 Food = 20, FoodTimer = 0, Air = 300, MaxAir = 300, Level = 0, TotalExperience = 0;
    int32 AttackTicker = 0, DamageCooldown = 0, DeathTicks = -1;
    // Ticks since this spawn: the clock Peaceful's regeneration keeps.
    int32 Age = 0;
    double FallDistanceCm = 0;
    bool bEyesInWater = false;
    // Ticks left burning; lava sets at least LavaFireTicks, water clears it.
    int32 FireTicks = 0;
    // Fire block contact ticks left before a fire block ignites the player: vanilla's fire ticks from -20 up to 0
    // [survival2.r14.player_fire], kept apart so FireTicks stays 0 = not burning. Water restores it; once a fire has
    // burned out it stays 0 until then (provisional).
    int32 FireGraceTicks = PlayerFireGraceTicks;
    FName MainHandItem;

    // Session diagnostics, kept across respawn.
    int32 DamageEvents = 0, Deaths = 0;
    // Fire diagnostics the view plays sounds from: lava hits taken, burning hits taken, fires put out by water.
    int32 LavaHurts = 0, BurnHurts = 0, Extinguishes = 0;
    // Fire block (in_fire) hits taken (step 16c).
    int32 FireBlockHurts = 0;
    // The last death (step 11a), kept across respawn: its message as vanilla's chat and death screen show it, the
    // en_us.json key it came from, and the death screen's score (the experience points held at death).
    FString LastDamage = TEXT("none"), DeathMessage, DeathKey;
    int32 DeathScore = 0;

    // A death message's en_us.json key and its 26.3 English template (%1$s the victim, %2$s the attacker).
    struct FDeathText
    {
        const TCHAR* Key;
        const TCHAR* Template;
    };
    // The name death messages give the local player until players have names.
    static constexpr const TCHAR* PlayerName = TEXT("Player");
    // A damage type's own death message, as the rules table holds it.
    static FDeathText GetCauseText(EFoundationDamage Type);
    // The message a death by Type shows: falls farther than 5 blocks use death.fell.accident.generic; mob and
    // player attacks without an attacker name fall back to death.attack.generic.
    static FDeathText GetDeathText(EFoundationDamage Type, double FallDistanceCm, const FString& Attacker = FString());
    // Fills %1$s and %2$s (a plain string replace).
    static FString FormatDeathMessage(const FDeathText& Text, const FString& Victim, const FString& Attacker);

    // One reference tick: timers, movement exhaustion and falls, air, then food.
    void Step(const FFoundationVitalsStep& Facts);
    // Step's falls and fire alone (mobs, survival T6 20b2: no food, air or regeneration): landing damage (none when
    // bFallImmune), water putting the fire out, burning and lava.
    void StepHazards(const FFoundationVitalsStep& Facts, bool bFallImmune = false);
    // The whole hurt path: invulnerability, difficulty, damage cooldown, armor, absorption, exhaustion, death.
    // Attacker names the mob or player behind a mob or player attack in the death message (survival T6 20c).
    bool Hurt(EFoundationDamage Type, float Amount, const FString& Attacker = FString());
    // The difficulty's share of the hurt path: Amount as the current difficulty scales it (0 for a mob's hit on a
    // Peaceful player), and the rest of the path for a Damage already scaled (a shield's unblocked part).
    float ScaleDamage(EFoundationDamage Type, float Amount) const;
    bool HurtScaled(EFoundationDamage Type, float Damage, const FString& Attacker = FString());
    void Heal(float Amount);
    void Eat(int32 Nutrition, float SaturationGain);
    // Player exhaustion: ignored while invulnerable, capped at 40.
    void AddExhaustion(float Amount);
    void AddExperience(int32 Points);
    void AddLevels(int32 Levels);
    // The Absorption effect's start: the buffer becomes at least Amount. Its timer is not modelled.
    void GrantAbsorption(float Amount);
    // A different main-hand item restarts attack recovery.
    void UpdateMainHand(FName Item);
    void ResetAttackStrength() { AttackTicker = 0; }
    // Teleports must not turn the distance already fallen into landing damage.
    void ResetFall() { FallDistanceCm = 0; }
    // One reference tick for a non-player holder (the lab's training target): only the damage cooldown
    // counts down. Food, air, falls and regeneration stay the player's.
    void StepDamageCooldown()
    {
        if (DamageCooldown > 0) --DamageCooldown;
    }
    // A fresh spawn: lab configuration and diagnostics stay; experience is lost.
    void Respawn();

    bool IsDead() const { return DeathTicks >= 0; }
    bool IsOnFire() const { return FireTicks > 0; }
    // The first half of the 20-tick damage cooldown: a hit now deals only what exceeds the last one.
    bool IsInHurtWindow() const { return DamageCooldown > 10; }
    bool ShouldRespawn() const { return DeathTicks >= RespawnTicks; }
    // Creative and Spectator abilities.
    bool IsInvulnerable() const;
    // Sprint eligibility: more than six food points, or a mode that may fly.
    bool HasEnoughFood() const;
    bool IsHurt() const { return Health > 0 && Health < MaxHealth; }
    // Reference ticks for a full swing: 20 / attack speed.
    float GetAttackDelay() const;
    // The HUD's sample (no partial tick).
    float GetAttackStrength() const;
    // The same clock sampled PartialTicks ahead; an attack evaluates at 0.5.
    float GetAttackStrength(float PartialTicks) const;
    int32 GetExperienceForNextLevel() const;
    // The armor and armor_toughness attributes: base plus the worn pieces, within the attributes' ranges (0-30 and
    // 0-20; minecraft.wiki "Attribute").
    int32 GetArmor() const { return FMath::Clamp(Armor + WornArmor, 0, 30); }
    float GetArmorToughness() const { return FMath::Clamp(ArmorToughness + WornToughness, 0.f, 20.f); }
    FString Describe() const;

    static float GetDamageAfterArmor(float Damage, int32 ArmorPoints, float Toughness);
    static int32 GetFallDamage(double FallCm);

private:
    void AddFoodExhaustion(float Amount);
    void TickFood();
};
