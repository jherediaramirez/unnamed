#pragma once

#include "CoreMinimal.h"
#include "Foundation/FoundationVitals.h"

// Death (survival step 11a): the death screen's switch and the drop rules. Sourced from the survival findings
// (C:\dev\projects\value-holder\SURVIVAL-FINDINGS-26.3.md, section "R6 Hurt and death") and the value report
// (C:\dev\projects\value-holder\MINECRAFT-26.3-VALUES.md); evidence ids in brackets. Provisional values are marked
// and listed in the program's provisional-values list.
namespace FoundationDeath
{
    // foundation.Death.Screen: 1 (the default) death holds the death screen until RequestRespawn, drops the inventory
    // and experience orbs; 0 respawns RespawnTicks after death with nothing dropped, as before. Defined with the
    // pawn's other switches.
    UNNAMED_API bool IsScreenEnabled();
    // [survival.r6.death_screen] the death screen's buttons are visible at once and enabled from death tick 20
    // (FFoundationVitals::RespawnTicks).
    inline bool AreButtonsEnabled(int32 DeathTicks)
    {
        return DeathTicks >= 20;
    }

    // [survival.r6.death_items] every dropped stack waits 40 ticks before it can be picked up and leaves at 0.2 blocks
    // per tick upward; [items.drop.random_scatter] sideways at 0.5 x U blocks per tick in a random direction.
    // Velocities are cm per reference step (one step a tick, a block 100 cm).
    constexpr int32 DropPickupDelay = 40;
    constexpr double DropUpCm = 20, DropScatterCm = 50;
    // Provisional: the drop leaves 0.3 blocks below the eye, as a thrown item [items.throw] (minecraft.wiki "Item
    // (entity)"); the death capture recorded velocities, not positions.
    constexpr double DropBelowEyeCm = 30;
    // The standing eye (UFoundationProfile::StandingEyesCm) for a player without facts yet.
    constexpr double DefaultEyeCm = 162;

    // [survival.r6.death_xp] the experience a death drops: min(7 x level, 100) (0 at level 0).
    inline int32 GetExperienceDrop(int32 Level)
    {
        return int32(FMath::Clamp(int64(Level) * 7, int64(0), int64(100)));
    }
}

// Hurt and death camera (survival step 11b). Pure functions; the pawn applies them when foundation.Death.Screen is 1.
namespace FoundationHurt
{
    // [hurt.camera_tilt] the roll in degrees: -14 x sin((hurtTime / hurtDuration)^4 x pi) x the Damage Tilt preference.
    // hurtTime counts down from hurtDuration (10) at the hit. Generic damage has no direction (hurtDir 0), so the tilt
    // is a plain roll about the view axis.
    inline double GetHurtTiltDegrees(double HurtTime, double HurtDuration, double Strength)
    {
        if (HurtDuration <= 0) return 0;
        return -14. * FMath::Sin(FMath::Pow(HurtTime / HurtDuration, 4.) * UE_DOUBLE_PI) * Strength;
    }

    // [hurt.camera] the death roll in degrees: 40 - 8000 / (min(deathTime, 20) + 200).
    inline double GetDeathRollDegrees(double DeathTime)
    {
        return 40. - 8000. / (FMath::Min(DeathTime, 20.) + 200.);
    }

    // [hurt.cooldown] the vitals' hurt animation: the cooldown starts at 20 and the animation is its first 10, so a
    // fresh hit is 10 and it counts down to 0.
    inline int32 GetHurtTime(const FFoundationVitals& Vitals)
    {
        return FMath::Max(0, Vitals.DamageCooldown - 10);
    }
}
