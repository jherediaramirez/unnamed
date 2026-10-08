#pragma once

#include "NativeGameplayTags.h"

// Gameplay tags the interaction code uses directly. Rules match tags, never item or block names. The other
// vanilla block tags (Block.*) are data: Config/Tags/MinecraftBlockTags.ini, written by ImportBlocks.py, which
// skips the ones declared here.
namespace FoundationTags
{
    // Blocks a pickaxe mines efficiently (#minecraft:mineable/pickaxe).
    UE_DECLARE_GAMEPLAY_TAG_EXTERN(BlockMineablePickaxe);
    // Blocks an iron-tier tool cannot harvest (#minecraft:incorrect_for_iron_tool).
    UE_DECLARE_GAMEPLAY_TAG_EXTERN(BlockIncorrectForIronTool);
    // Logs and wood keep nearby leaves alive (#minecraft:prevents_nearby_leaf_decay).
    UE_DECLARE_GAMEPLAY_TAG_EXTERN(BlockPreventsNearbyLeafDecay);
    // Makes the block below snowy (#minecraft:snow).
    UE_DECLARE_GAMEPLAY_TAG_EXTERN(BlockSnow);
    // Item identities; capabilities stay separate fields, so one item may carry several.
    UE_DECLARE_GAMEPLAY_TAG_EXTERN(ItemBlock);
    UE_DECLARE_GAMEPLAY_TAG_EXTERN(ItemToolPickaxe);
    UE_DECLARE_GAMEPLAY_TAG_EXTERN(ItemWeaponSword);
}
