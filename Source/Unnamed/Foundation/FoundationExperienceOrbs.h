#pragma once

#include "CoreMinimal.h"

// An experience orb in the world (survival step 12; scout AC's plan row 12). Like the item entities, the authority owns
// the list and steps it once per reference step; the view only draws it. Positions are cm from the block world's
// origin.
struct FFoundationExperienceOrb
{
    int32 Id = 0;
    // The points one pickup gives, and how many such orbs this one stands for (same-value orbs merge into a count).
    int32 Value = 0, Count = 1;
    // The bottom centre of its box now and one step ago (the view draws between the two).
    FVector LocalCm = FVector::ZeroVector, PreviousLocalCm = FVector::ZeroVector;
    // Cm per reference step.
    FVector VelocityCm = FVector::ZeroVector;
    // Steps lived; does not count while frozen.
    int32 Age = 0;
    // Steps run (the entity's tickCount, R23): like Age, but a merge never resets it; the merge scan's phase.
    int32 Ticks = 0;
    bool bOnGround = false;
    // Spawned since the last step's merge scan (Codex review 8 #1: a new orb may merge whatever the ids).
    bool bNew = false;
    // Its column is not loaded: it neither steps nor is picked up nor drawn.
    bool bFrozen = false;
};

// An orb pickup that changed the level: from From to To at the reference step Step; Serial counts the gains up.
struct FFoundationLevelGain
{
    int64 Step = 0;
    int32 From = 0, To = 0, Serial = 0;
};

// The orbs' rules. Sourced from the value report (C:\dev\projects\value-holder\MINECRAFT-26.3-VALUES.md, section "XP";
// evidence ids in brackets). Provisional (not in the report; the program's provisional-values list): marked so below.
namespace FoundationExperienceOrbs
{
    // foundation.Items.Xp: 1 (the default) ores pay orbs that fly to the player and fill the XP bar; 0 no orbs spawn,
    // as before (orbs already in the world stay, fly and pay, as drops stay at foundation.Items.Entities 0).
    UNNAMED_API bool IsOn();

    // [xp.free_flight; catalog.entities] a 0.5 x 0.5 box, gravity 0.03 blocks per tick squared, air drag 0.98.
    constexpr double BoxCm = 50, GravityCm = 3;
    // [xp.attraction, xp.attraction_parameters] an orb follows a player within 8 blocks of it (7.99 acquires, 8.00
    // not), accelerating by normalize(delta) x (1 - |delta| / 8)^2 x 0.1 blocks per tick toward the player's position
    // plus half its eye height.
    constexpr double FollowCm = 800, AttractionCm = 10;
    // [xp.free_flight] the 20-tick scan is the merge scan; R23 (survival2.r23.xp_rules): it runs when the orb's
    // tickCount % MergeTicks == 1 (its steps 1, 21, 41, ...). Newly awarded XP searches a RewardSearchCm cube around
    // its position instead of the grown merge box.
    constexpr int32 MergeTicks = 20;
    constexpr double RewardSearchCm = 100;
    // [xp.value_icons] the icons' lower thresholds (icon 0 from 1 point to icon 10 from 2477).
    constexpr int32 IconThresholds[] = {1, 3, 7, 17, 37, 73, 149, 307, 617, 1237, 2477};
    // [xp.value_icons, xp.value] the look: a 0.3-block billboard from a 16 x 16 tile of a 64 x 64 texture, vertex alpha
    // 128. [survival2.r23.xp_quad] its edges sit 0.025 and 0.325 blocks above the orb (moved up 0.1 with its vertices
    // from -0.25 to 0.75 of its size), so its centre is 17.5 cm up (QC RA2-7).
    constexpr double QuadCm = 30, QuadUpCm = 17.5;

    // Provisional (minecraft.wiki "Experience orb", not in the report): orbs despawn at 6,000 ticks of age; a player
    // takes one orb every 2 ticks; an orb merges with same-value orbs whose box meets its own grown 0.5 blocks on every
    // axis (its count adds), every MergeTicks of its own age.
    constexpr int32 DespawnAge = 6000, PickupEveryTicks = 2;
    constexpr double MergeGrowCm = 50;
    // Codex review 8 #1 (the Chinese minecraft.wiki's "Experience orb", Merging; the English page and the report are
    // silent): of two orbs in reach, the seeker takes in the other only when their ids differ by a multiple of
    // MergeIdPeriod, or when the seeker spawned this step and a NewMergeChance roll passes. An orb also scans in the
    // step it spawned.
    constexpr int32 MergeIdPeriod = 40;
    constexpr double NewMergeChance = .025;
    // R23 (survival2.r23.xp_rules; replaced Codex review 8 #2's provisional 0.5 above 6 cm): a downward landing whose
    // pre-move vertical speed is below -gravity (faster than BounceMinCm = GravityCm per step) leaves the ground at
    // BounceFactor times that speed; slower (an orb resting on the floor lands at exactly the gravity), it stays down.
    constexpr double BounceFactor = .4, BounceMinCm = GravityCm;
    // [survival.r5.rules] entity.player.levelup plays at volume LevelUpVolume x min(level / LevelUpFullLevel, 1), pitch
    // 1, on a positive level gain reaching a multiple of 5 (the level counted is the first multiple reached), and
    // only more than LevelUpGapTicks after the last one.
    constexpr float LevelUpVolume = .75f;
    constexpr int32 LevelUpFullLevel = 30, LevelUpGapTicks = 100;
    // [survival.r5.play_calls] entity.experience_orb.pickup plays at volume 0.1; its pitch is GetPickupPitch's.
    constexpr float PickupVolume = .1f;
    // Provisional (estimates, P3): a new orb takes the block drop's spawn spread and speed (FoundationItemEntities),
    // and one step runs gravity, the attraction, the move, then the drag (the item entities' ground friction included).

    // Whether Seeker takes in Other (the caller checks the value and the reach).
    UNNAMED_API bool CanMerge(const FFoundationExperienceOrb& Seeker, const FFoundationExperienceOrb& Other);
    // The level-up sound's volume when the level goes from LastLevel to Level at Step, the last one having played at
    // LastSoundStep (negative: never); 0 for no sound. A gain passing several levels plays the first multiple of 5.
    UNNAMED_API float GetLevelUpVolume(int32 LastLevel, int32 Level, int64 Step, int64 LastSoundStep);
    // The level-up sounds of a frame, in order (Codex review 14, finding 2; review 27, finding 2): each orb pickup that
    // changed the level (Gains, From -> To at its reference Step, Serial counting up; those with Serial <= LastSerial
    // were judged already) is judged at its own step, then a level the steps did not make (the lab panel's: Level
    // differing from the last judged one) at FrameStep. LastLevel (-1 before the first frame: no sound), LastSoundStep
    // and LastSerial carry over between frames. Returns the volumes to play.
    UNNAMED_API TArray<float> GetLevelUpVolumes(TConstArrayView<FFoundationLevelGain> Gains, int32 Level,
        int64 FrameStep, int32& LastLevel, int64& LastSoundStep, int32& LastSerial);
    // [survival.r5.rules] the pickup sound's pitch, 0.9 + 0.35 x (U - V) for two uniform draws in [0, 1).
    UNNAMED_API float GetPickupPitch(float U, float V);
    // The icon (0 to 10) for an orb's value.
    UNNAMED_API int32 GetIcon(int32 Value);
    // Provisional (wiki): a total splits greedily into orbs, each the largest icon threshold not above what is left.
    UNNAMED_API int32 GetSplitValue(int32 Total);
    // [xp.value] the colour pulse at an age in ticks (fractional between steps): R = floor((sin(t/2) + 1) x 0.5 x 255),
    // G = 255, B = floor((sin(t/2 + 4 pi/3) + 1) x 0.1 x 255).
    UNNAMED_API FColor GetColor(double AgeTicks);
    // [xp.ore_drops, xp.ore] the points a block pays when broken by a player who can harvest it, as an inclusive
    // range; false for a block that pays none (iron, copper and gold ore included).
    UNNAMED_API bool GetBlockExperience(FName Block, int32& OutMin, int32& OutMax);
    // The box of an orb standing at a world position (its bottom centre), and what it merges from.
    UNNAMED_API FBox GetBox(const FVector& WorldCm);
    UNNAMED_API FBox GetMergeBox(const FVector& WorldCm);
    // [xp.attraction] the step's acceleration (cm per step) toward a target point; zero at FollowCm or farther.
    UNNAMED_API FVector GetAttraction(const FVector& OrbWorldCm, const FVector& TargetWorldCm);
    // The world region one step's motion can touch (for the collision query).
    UNNAMED_API FBox GetStepRegion(const FFoundationExperienceOrb& Orb, const FVector& OriginCm, const FVector& PushCm);
    // One step of motion against world boxes (cm), with this step's attraction.
    UNNAMED_API void Move(
        FFoundationExperienceOrb& Orb, const FVector& OriginCm, const TArray<FBox>& Boxes, const FVector& PushCm);
}
