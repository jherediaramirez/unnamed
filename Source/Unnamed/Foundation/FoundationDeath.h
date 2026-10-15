#pragma once

#include "CoreMinimal.h"

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
