# Stand-ins

Java Edition art the game ships as a stand-in, to be replaced later (Jesus's rule: any Java 26.3 texture may stand in). One row per asset: where it came from in the 26.3 client jar, where it is used, and the step that brought it in. Several lanes may add rows; on a merge keep both sides' rows.

The creative screen's existing textures from `Reference/minecraft-menu` (`gui/container/creative_inventory/*`, `gui/sprites/container/creative_inventory/*`, the slot highlight, imported by `ImportBlocks.py`) are stand-ins too; they have no per-file rows yet.

| Asset | Jar path | Where used | Step |
| --- | --- | --- | --- |
| The 6 `stand_ins/chest_normal_*.png` tiles of `Reference/minecraft-blocks-26.3` (layers of `/Game/Blocks/T_BlockTextures`) | `assets/minecraft/textures/entity/chest/normal.png` | `chest` block's stand-in box and its item icon | Survival B1 |
| The 6 `stand_ins/chest_trapped_*.png` tiles of `Reference/minecraft-blocks-26.3` (layers of `/Game/Blocks/T_BlockTextures`) | `assets/minecraft/textures/entity/chest/trapped.png` | `trapped_chest` block's stand-in box and its item icon | Survival B1 |
| The 6 `stand_ins/chest_ender_*.png` tiles of `Reference/minecraft-blocks-26.3` (layers of `/Game/Blocks/T_BlockTextures`) | `assets/minecraft/textures/entity/chest/ender.png` | `ender_chest` block's stand-in box and its item icon | Survival B1 |
