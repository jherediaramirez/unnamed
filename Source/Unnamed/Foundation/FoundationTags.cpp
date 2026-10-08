#include "Foundation/FoundationTags.h"

namespace FoundationTags
{
    UE_DEFINE_GAMEPLAY_TAG_COMMENT(BlockMineablePickaxe, "Block.Mineable.Pickaxe", "Mined efficiently by pickaxes");
    UE_DEFINE_GAMEPLAY_TAG_COMMENT(
        BlockIncorrectForIronTool, "Block.IncorrectForTool.Iron", "Iron-tier tools cannot harvest these");
    UE_DEFINE_GAMEPLAY_TAG_COMMENT(
        BlockPreventsNearbyLeafDecay, "Block.PreventsNearbyLeafDecay", "Keeps leaves within 6 blocks alive");
    UE_DEFINE_GAMEPLAY_TAG_COMMENT(BlockSnow, "Block.Snow", "Makes the block below snowy");
    UE_DEFINE_GAMEPLAY_TAG_COMMENT(ItemBlock, "Item.Block", "Places a block");
    UE_DEFINE_GAMEPLAY_TAG_COMMENT(ItemToolPickaxe, "Item.Tool.Pickaxe", "Pickaxe-class mining tool");
    UE_DEFINE_GAMEPLAY_TAG_COMMENT(ItemWeaponSword, "Item.Weapon.Sword", "Sword-class melee weapon");
}
