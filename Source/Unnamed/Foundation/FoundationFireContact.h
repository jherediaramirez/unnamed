#pragma once

#include "CoreMinimal.h"

struct FFoundationBlockWorld;

// Fire block contact (survival step 16c): what a body box touches among the fire blocks of the authority's world.
namespace FoundationFireContact
{
    struct FContact
    {
        // The in_fire damage of the hottest fire cell touched: fire 1, soul fire 2 (minecraft.wiki "Soul Fire"), 0
        // none.
        int32 FireDamage = 0;
        // The body reaches a lava cauldron's lava (minecraft.wiki "Cauldron": it burns entities inside).
        bool bLavaCauldron = false;
    };

    // Body is in world cm. Like the fluid tests it is shrunk by 0.1 cm first, so a body only beside a cell touches
    // nothing. A fire cell counts whole (provisional: no outline test measured); a lava cauldron's lava is the box x
    // and y 2/16-14/16, z 4/16 (the floor) to 15/16 (the lava's top) of its cell (provisional, from its model and
    // shape).
    UNNAMED_API FContact Get(const FFoundationBlockWorld& World, const FBox& Body);

    // foundation.FireBlocks: 1 fire and soul fire hurt and ignite the player (with foundation.Fire 1); 0 as step 16a.
    UNNAMED_API bool IsOn();
}
