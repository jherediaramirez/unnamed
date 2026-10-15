# Stand-ins

Java Edition art the game ships as a stand-in, to be replaced later (Jesus's rule: any Java 26.3 texture may stand in). One row per asset: where it came from in the 26.3 client jar, where it is used, and the step that brought it in. Several lanes may add rows; on a merge keep both sides' rows.

The creative screen's existing textures from `Reference/minecraft-menu` (`gui/container/creative_inventory/*`, `gui/sprites/container/creative_inventory/*`, the slot highlight, imported by `ImportBlocks.py`) are stand-ins too; they have no per-file rows yet.

| Asset | Jar path | Where used | Step |
| --- | --- | --- | --- |
| `/Game/Gui/Container/gui/container/inventory` | `assets/minecraft/textures/gui/container/inventory.png` | The survival inventory screen's background (`SHudContainerScreen`) | Survival 5b |
| `/Game/Gui/Container/gui/sprites/container/slot/helmet` | `assets/minecraft/textures/gui/sprites/container/slot/helmet.png` | The empty head slot | Survival 5b |
| `/Game/Gui/Container/gui/sprites/container/slot/chestplate` | `assets/minecraft/textures/gui/sprites/container/slot/chestplate.png` | The empty chest slot | Survival 5b |
| `/Game/Gui/Container/gui/sprites/container/slot/leggings` | `assets/minecraft/textures/gui/sprites/container/slot/leggings.png` | The empty legs slot | Survival 5b |
| `/Game/Gui/Container/gui/sprites/container/slot/boots` | `assets/minecraft/textures/gui/sprites/container/slot/boots.png` | The empty feet slot | Survival 5b |
| `/Game/Gui/Container/gui/sprites/container/slot/shield` | `assets/minecraft/textures/gui/sprites/container/slot/shield.png` | The empty offhand slot | Survival 5b |
| `/Game/Gui/Container/gui/container/crafting_table` | `assets/minecraft/textures/gui/container/crafting_table.png` | The crafting table screen's background (`SHudContainerScreen`, `Kind` Crafting) | Survival 7 |
| `/Game/Gui/Container/gui/container/furnace` | `assets/minecraft/textures/gui/container/furnace.png` | The furnace screen's background (`SHudContainerScreen`, `Kind` Furnace) | T1 8a2 |
| `/Game/Gui/Container/gui/sprites/container/furnace/lit_progress` | `assets/minecraft/textures/gui/sprites/container/furnace/lit_progress.png` | The furnace screen's flame (fuel left) | T1 8a2 |
| `/Game/Gui/Container/gui/sprites/container/furnace/burn_progress` | `assets/minecraft/textures/gui/sprites/container/furnace/burn_progress.png` | The furnace screen's arrow (cooking progress) | T1 8a2 |
| `/Game/Gui/Container/gui/sprites/recipe_book/button` | `assets/minecraft/textures/gui/sprites/recipe_book/button.png` | The recipe-book button (inert) | Survival 5b |
| `/Game/Gui/Container/gui/sprites/recipe_book/button_highlighted` | `assets/minecraft/textures/gui/sprites/recipe_book/button_highlighted.png` | The recipe-book button, hovered | Survival 5b |
| `/Game/Entities/T_ExperienceOrb` | `assets/minecraft/textures/entity/experience/experience_orb.png` (via `Reference/minecraft-entities-26.3`) | `M_ExperienceOrb`, the XP orbs' quads (`FFoundationExperienceOrbView`; 16 x 16 icons in 4 columns, `xp.value_icons`) | Survival 12 |
| The 6 `stand_ins/chest_normal_*.png` tiles of `Reference/minecraft-blocks-26.3` (layers of `/Game/Blocks/T_BlockTextures`) | `assets/minecraft/textures/entity/chest/normal.png` | `chest` block's stand-in box and its item icon | Survival B1 |
| The 6 `stand_ins/chest_trapped_*.png` tiles of `Reference/minecraft-blocks-26.3` (layers of `/Game/Blocks/T_BlockTextures`) | `assets/minecraft/textures/entity/chest/trapped.png` | `trapped_chest` block's stand-in box and its item icon | Survival B1 |
| The 6 `stand_ins/chest_ender_*.png` tiles of `Reference/minecraft-blocks-26.3` (layers of `/Game/Blocks/T_BlockTextures`) | `assets/minecraft/textures/entity/chest/ender.png` | `ender_chest` block's stand-in box and its item icon | Survival B1 |
| `/Game/Entities/T_Mob_Cow` | `assets/minecraft/textures/entity/cow/cow_temperate.png` | Model skin of the cow (`M_Mob`'s Skin through `DA_Mobs`, `FFoundationMobView`) | Survival 20a |
| `/Game/Entities/T_Mob_Pig` | `assets/minecraft/textures/entity/pig/pig_temperate.png` | Model skin of the pig (drawn from 20b) (`M_Mob`'s Skin through `DA_Mobs`, `FFoundationMobView`) | Survival 20a |
| `/Game/Entities/T_Mob_Sheep` | `assets/minecraft/textures/entity/sheep/sheep.png` | Model skin of the sheep (drawn from 20b) (`M_Mob`'s Skin through `DA_Mobs`, `FFoundationMobView`) | Survival 20a |
| `/Game/Entities/T_Mob_Chicken` | `assets/minecraft/textures/entity/chicken/chicken_temperate.png` | Model skin of the chicken (drawn from 20b) (`M_Mob`'s Skin through `DA_Mobs`, `FFoundationMobView`) | Survival 20a |
| `/Game/Entities/T_Mob_Zombie` | `assets/minecraft/textures/entity/zombie/zombie.png` | Model skin of the zombie (drawn from 20c) (`M_Mob`'s Skin through `DA_Mobs`, `FFoundationMobView`) | Survival 20a |
| `/Game/Entities/T_Mob_Skeleton` | `assets/minecraft/textures/entity/skeleton/skeleton.png` | Model skin of the skeleton (drawn from 20d) (`M_Mob`'s Skin through `DA_Mobs`, `FFoundationMobView`) | Survival 20a |
| `/Game/Entities/T_Mob_Creeper` | `assets/minecraft/textures/entity/creeper/creeper.png` | Model skin of the creeper (drawn from 20d) (`M_Mob`'s Skin through `DA_Mobs`, `FFoundationMobView`) | Survival 20a |
| `/Game/Entities/T_Mob_Spider` | `assets/minecraft/textures/entity/spider/spider.png` | Model skin of the spider (drawn from 20c) (`M_Mob`'s Skin through `DA_Mobs`, `FFoundationMobView`) | Survival 20a |
| `/Game/Entities/T_Arrow` | `assets/minecraft/textures/entity/projectiles/arrow.png` (via `Reference/minecraft-entities-26.3`) | Skin of the arrow entity's `arrow#main` model (`M_Mob`'s Skin through `DA_Projectiles`, `Scripts/Editor/ImportProjectiles.py`) | T2 19c |
| `/Game/Entities/T_Mob_SheepWool` | `assets/minecraft/textures/entity/sheep/sheep_wool.png` | The sheep's wool layer (`sheep#wool`, white) over its main layer (`M_Mob`'s Skin through `DA_Mobs`, `FFoundationMobView`) | Survival 20b |
| The other mob textures of `Reference/minecraft-entities-26.3` (not imported yet): `cow/cow_{cold,warm}`, `pig/pig_{cold,warm}`, `chicken/chicken_{cold,warm}`, `sheep/sheep_wool`, `sheep/sheep_wool_undercoat`, `spider/spider_eyes` | `assets/minecraft/textures/entity/<same path>.png` | none yet: the farm-animal variants (20e), the sheep's wool layers (20b), the spider's eyes (20c) | Survival 20a |
| The 10 `/Game/Items/Gui/item/<id>` sprites and their `T_ItemTextures` layers 104-113 (charcoal, brick, lime_dye, green_dye, dried_kelp, baked_potato, nether_brick, resin_brick, iron_nugget, copper_nugget; via `Reference/minecraft-items-26.3`) | `assets/minecraft/textures/item/<id>.png` | The cooking outputs' item icons and held/dropped items | T1 8a2 |
| `/Game/Gui/Container/gui/container/smoker` | `assets/minecraft/textures/gui/container/smoker.png` | The smoker screen's background (`SHudContainerScreen`, `Kind` Smoker) | T1 8b1 |
| `/Game/Gui/Container/gui/sprites/container/smoker/lit_progress` | `assets/minecraft/textures/gui/sprites/container/smoker/lit_progress.png` | The smoker screen's flame (fuel left) | T1 8b1 |
| `/Game/Gui/Container/gui/sprites/container/smoker/burn_progress` | `assets/minecraft/textures/gui/sprites/container/smoker/burn_progress.png` | The smoker screen's arrow (cooking progress) | T1 8b1 |
| `/Game/Gui/Container/gui/container/blast_furnace` | `assets/minecraft/textures/gui/container/blast_furnace.png` | The blast furnace screen's background (`SHudContainerScreen`, `Kind` BlastFurnace) | T1 8b1 |
| `/Game/Gui/Container/gui/sprites/container/blast_furnace/lit_progress` | `assets/minecraft/textures/gui/sprites/container/blast_furnace/lit_progress.png` | The blast furnace screen's flame (fuel left) | T1 8b1 |
| `/Game/Gui/Container/gui/sprites/container/blast_furnace/burn_progress` | `assets/minecraft/textures/gui/sprites/container/blast_furnace/burn_progress.png` | The blast furnace screen's arrow (cooking progress) | T1 8b1 |
| `/Game/Gui/Container/gui/container/generic_54` | `assets/minecraft/textures/gui/container/generic_54.png` | The barrel screen's background, its top 3 rows and player part (`SHudContainerScreen`, `Kind` Barrel; 18-chest will reuse it) | T1 9 |
| `/Game/Gui/Container/misc/pumpkinblur` | `assets/minecraft/textures/misc/pumpkinblur.png` (via `Reference/minecraft-hud-26.3`) | The carved pumpkin worn on the head: drawn over the whole first-person view behind the HUD (`HudDrawList`, `FHudSnapshot::CameraOverlay`) | T2 19a2 |
| `/Game/Items/Gui/item/shield` | `assets/minecraft/textures/entity/shield/shield_base_nopattern.png` (via `Reference/minecraft-items-26.3`), the plate's front face scaled to 9 x 16 (`Scripts/Editor/CreateShieldSprite.py`, `Art/StandIns/item/shield.png`) | The shield's GUI sprite (hotbar, inventory, creative); 26.3 draws its 3D special model there | T2 19b |
| `/Game/Items/Gui/shield/shield_base_nopattern` | `assets/minecraft/textures/entity/shield/shield_base_nopattern.png` (via `Reference/minecraft-items-26.3`) | The shield's held box model (the special model's stand-in geometry) | T2 19b |
