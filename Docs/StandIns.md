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
| `/Game/Gui/Container/gui/sprites/recipe_book/button` | `assets/minecraft/textures/gui/sprites/recipe_book/button.png` | The recipe-book button (inert) | Survival 5b |
| `/Game/Gui/Container/gui/sprites/recipe_book/button_highlighted` | `assets/minecraft/textures/gui/sprites/recipe_book/button_highlighted.png` | The recipe-book button, hovered | Survival 5b |
| `/Game/Entities/T_ExperienceOrb` | `assets/minecraft/textures/entity/experience/experience_orb.png` (via `Reference/minecraft-entities-26.3`) | `M_ExperienceOrb`, the XP orbs' quads (`FFoundationExperienceOrbView`; 16 x 16 icons in 4 columns, `xp.value_icons`) | Survival 12 |
| The 6 `stand_ins/chest_normal_*.png` tiles of `Reference/minecraft-blocks-26.3` (layers of `/Game/Blocks/T_BlockTextures`) | `assets/minecraft/textures/entity/chest/normal.png` | `chest` block's stand-in box and its item icon | Survival B1 |
| The 6 `stand_ins/chest_trapped_*.png` tiles of `Reference/minecraft-blocks-26.3` (layers of `/Game/Blocks/T_BlockTextures`) | `assets/minecraft/textures/entity/chest/trapped.png` | `trapped_chest` block's stand-in box and its item icon | Survival B1 |
| The 6 `stand_ins/chest_ender_*.png` tiles of `Reference/minecraft-blocks-26.3` (layers of `/Game/Blocks/T_BlockTextures`) | `assets/minecraft/textures/entity/chest/ender.png` | `ender_chest` block's stand-in box and its item icon | Survival B1 |
