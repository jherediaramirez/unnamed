#pragma once

#include "CoreMinimal.h"
#include "Math/RandomStream.h"

// The bow and the arrow entity (survival step 19c): the draw, the shot, the flight, the hits and the pickup rule. Pure
// rules here; FFoundationAuthority owns the arrows (ShootArrow, StepArrows). Distances are centimetres, velocities
// centimetres per reference step, as the item entities'.

// Who may pick a stuck arrow up (26.3's AbstractArrow.Pickup; survival2.r18.arrow_rule): a player's arrow is Allowed
// (CreativeOnly from a creative player), a skeleton's Disallowed.
enum class EFoundationArrowPickup : uint8
{
    Disallowed,
    Allowed,
    CreativeOnly
};

// One arrow in flight or stuck in a block (session-only, like the item entities, orbs and mobs).
struct FFoundationArrow
{
    int32 Id = 0;
    // The block world's frame (world cm = OriginCm + LocalCm), as FFoundationItemEntity.
    FVector LocalCm = FVector::ZeroVector, PreviousLocalCm = FVector::ZeroVector;
    FVector VelocityCm = FVector::ZeroVector;
    // Degrees, from the velocity (a stuck arrow keeps its last).
    float Yaw = 0, Pitch = 0;
    // The shooter: a player id or a mob id; INDEX_NONE for none. The attacker name for the death message.
    int32 OwnerId = INDEX_NONE;
    bool bOwnerIsMob = false;
    FString Attacker;
    EFoundationArrowPickup Pickup = EFoundationArrowPickup::Allowed;
    bool bCrit = false, bInGround = false;
    // Steps since it was shot, and since it stuck (removed at FoundationArrows::DespawnTicks).
    int32 Age = 0, InGroundTicks = 0;
};

// One shot (the bow's release; T6 20d's skeleton). The arrow leaves FromCm (world cm) along Direction at SpeedBlocks
// blocks per tick, spread by Inaccuracy (FoundationArrows::Spread).
struct FFoundationArrowShot
{
    FVector FromCm = FVector::ZeroVector;
    FVector Direction = FVector::ForwardVector;
    float SpeedBlocks = 3.f;
    float Inaccuracy = 1.f;
    bool bCrit = false;
    int32 OwnerId = INDEX_NONE;
    bool bOwnerIsMob = false;
    EFoundationArrowPickup Pickup = EFoundationArrowPickup::Allowed;
    // The death message's attacker (a mob's en_us name, "Skeleton"); empty for the bow's player.
    FString Attacker;
};

namespace FoundationBow
{
    // foundation.Items.Bow: 1 a bow draws and shoots (step 19c); 0 it passes, and no arrow spawns.
    UNNAMED_API bool IsOn();
    // A full draw: 20 ticks (bow.draw_curve).
    constexpr int32 FullDrawTicks = 20;
    // A bow's use duration: as the shield's, 72000 ticks (minecraft.wiki "Bow"; provisional).
    constexpr int32 UseDuration = 72000;
    // Below it a release shoots nothing (minecraft.wiki "Bow"; provisional, R24).
    constexpr float MinPower = .1f;
    // f = t / 20, min((f^2 + 2f) / 3, 1) (bow.draw_curve; VALUES "Draw curve and launch speed").
    UNNAMED_API float GetPower(int32 Ticks);
    // The launch speed: power x 3 blocks per tick (bow.flight).
    constexpr float FullSpeedBlocks = 3.f;
    // The bow's FOV target: 1 - 0.15 x min(t / 20, 1)^2 (survival2.r18.fov_rule).
    UNNAMED_API float GetFovTarget(int32 Ticks);
    // entity.arrow.shoot's pitch: 1 / (U x 0.4 + 1.2) + power x 0.5 (survival.r5.rules).
    UNNAMED_API float GetShootPitch(float Power, float U);
}

namespace FoundationArrows
{
    constexpr double BlockCm = 100;
    // Air drag 0.99, then gravity 0.05 blocks per tick (arrows.flight); water drag 0.6 (minecraft.wiki "Arrow";
    // provisional, R24).
    constexpr float AirDrag = .99f, WaterDrag = .6f;
    constexpr double GravityCm = 5;
    // The entity's box: 0.5 x 0.5 blocks (V:5370), centred on LocalCm.
    constexpr double HalfSizeCm = 25;
    // A stuck arrow despawns 1,200 ticks after it stuck (survival2.r18.arrow_rule).
    constexpr int32 DespawnTicks = 1200;
    // A stuck arrow shakes for 7 steps (vanilla's shakeTime) and cannot be picked up until it stops (QC R40-5).
    constexpr int32 ShakeTicks = 7;
    // Its shooter is safe from it for this many steps (minecraft.wiki "Arrow"; provisional, R24).
    constexpr int32 OwnerGraceTicks = 5;
    // Entity boxes grow by this much for the hit test (minecraft.wiki "Arrow"; provisional).
    constexpr double HitInflateCm = 30;
    // The spread's scale per unit of inaccuracy, a triangular offset per axis of the unit direction (minecraft.wiki
    // "Arrow"; provisional, R24).
    constexpr double SpreadScale = .0172275;
    // entity.arrow.hit (volume 1, pitch 1.2 / (U x 0.2 + 0.9)) and entity.arrow.hit_player (0.18, 0.45)
    // (survival.r5.rules).
    constexpr float HitPlayerVolume = .18f, HitPlayerPitch = .45f;
    UNNAMED_API float GetHitPitch(float U);

    // ceil(speed x 2) for a base damage of 2, plus a uniform 0..floor(damage / 2) + 1 on a critical
    // (survival2.r18.arrow_rule).
    UNNAMED_API int32 GetDamage(float SpeedBlocks, bool bCrit, FRandomStream& Random);
    // One step's drag then gravity on a velocity in cm per step (arrows.flight: drag before gravity).
    UNNAMED_API void Integrate(FVector& VelocityCm, bool bInWater);
    // The launch velocity in cm per step: the unit direction plus a triangular offset of SpreadScale x Inaccuracy per
    // axis, normalized again, times the speed.
    UNNAMED_API FVector Launch(const FVector& Direction, float SpeedBlocks, float Inaccuracy, FRandomStream& Random);
    // Yaw and pitch (degrees) facing along a velocity.
    UNNAMED_API void Face(const FVector& VelocityCm, float& OutYaw, float& OutPitch);
    // The first time (0..1) the segment From..To enters any box, and that box's index; false when it enters none.
    // A segment starting inside a box hits it at 0.
    UNNAMED_API bool Sweep(const FVector& From, const FVector& To, TConstArrayView<FBox> Boxes, double& OutTime,
        int32* OutIndex = nullptr);
}
