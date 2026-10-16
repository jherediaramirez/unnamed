# minecraft-entities-26.3

Entity textures, loot tables and mob facts from the Java 26.3 client jar, used as stand-ins until the project's own art replaces them (Jesus,
2026-10-14; each one has a row in `Docs/StandIns.md`). Hash-checked: never edited by hand.

- `assets/minecraft/textures/<name>`: the jar's `assets/minecraft/textures/<name>`, byte for byte.
- `textures.json`: each texture's name, SHA-1, size and pixel size, in `tools/build_reference.py`'s `TEXTURES` order.
- `data/minecraft/loot_table/<name>`: the jar's loot table, byte for byte (the client jar carries `data/`), in `build_reference.py`'s `LOOT` order.
- `data/minecraft/recipe/<name>`: the jar's recipe, byte for byte, in `build_reference.py`'s `RECIPES` order.
- `facts/`: produced by their tools and only hashed by `build_reference.py` (it copies them in from `--facts <dir>`).
- `SHA256SUMS.txt`: every file's SHA-256.

| Texture | Used by | Step |
|---|---|---|
| `entity/experience/experience_orb.png` | `/Game/Entities/T_ExperienceOrb` (XP orbs, value report `xp.value_icons`) | survival 12 |
| `entity/cow/cow_{temperate,cold,warm}.png` | /Game/Entities/DA_Mobs (survival T6 20a, ImportMobs.py) | survival 20a |
| `entity/pig/pig_{temperate,cold,warm}.png` | /Game/Entities/DA_Mobs (survival T6 20a, ImportMobs.py) | survival 20a |
| `entity/sheep/sheep.png`, `sheep_wool.png`, `sheep_wool_undercoat.png` (64 x 32) | /Game/Entities/DA_Mobs (survival T6 20a, ImportMobs.py) | survival 20a |
| `entity/chicken/chicken_{temperate,cold,warm}.png` | /Game/Entities/DA_Mobs (survival T6 20a, ImportMobs.py) | survival 20a |
| `entity/{zombie/zombie,skeleton/skeleton,creeper/creeper,spider/spider,spider/spider_eyes}.png` | /Game/Entities/DA_Mobs (survival T6 20a, ImportMobs.py) | survival 20a |
| `data/minecraft/loot_table/entities/{cow,pig,sheep,chicken,zombie,skeleton,creeper,spider}.json` and `entities/sheep/<16 colours>.json` | /Game/Entities/DA_Mobs (survival T6 20a, ImportMobs.py) | survival 20a |
| `data/minecraft/recipe/cooked_{beef,porkchop,mutton,chicken}.json` | The meats' furnace results, for the entity tables' `furnace_smelt` (ImportMobs.py) | survival 20a |
| `data/minecraft/loot_table/shearing/sheep.json` and `shearing/sheep/<16 colours>.json` | The shears' sheep drops: /Game/Entities/DA_Mobs (survival T6 20e1, ImportMobs.py) | survival 20e1 |
| `entity/projectiles/arrow.png` | The arrow entity (32 x 32): /Game/Entities/DA_Projectiles (survival T2 19c, ImportProjectiles.py) | survival 19c |
| `facts/entity_models.json` | The jar's baked model parts (path, pivot, rotation, scale) and cubes (box, faces with normals and vertex x y z u v) for the layers of cow, pig, sheep (main, wool, undercoat), chicken, zombie, skeleton, creeper, spider (and their saddle/armor layers), then (survival T2 19c, `APPEND`, after the sorted layers so none moves) the arrow's `arrow#main`, then (survival T6 20e1, `APPEND`) the cold and warm farm animal layers `cold_cow#main`, `warm_cow#main`, `cold_pig#main`, `cold_chicken#main`. Dumped by `tools/EntityModelFacts.java` from the public API only (`LayerDefinitions.createRoots`, `bakeRoot`, `ModelPart.visit`). /Game/Entities/DA_Mobs (survival T6 20a, ImportMobs.py) | survival 20a |
| `facts/mob_attributes.json` | Per mob, Health and each attribute's base value as a number (null when the mob has no such attribute), read by `tools/mob_probe.py` from a headless 26.3 server through console commands (server jar SHA-1 recorded in the file). /Game/Entities/DA_Mobs (survival T6 20a, ImportMobs.py) | survival 20a |

Later steps (mob textures, survival track T6) append to `TEXTURES`; entries are never reordered or removed. To rebuild,
move this folder away (keep `tools/` and this README), refresh `facts/` with the two tools if needed (`EntityModelFacts.java` compiled against the jar and libraries, `mob_probe.py <dir>`), then run `tools/build_reference.py --facts <dir>` (the dir holds `entity_models.json` and `mob_attributes.json`). `LOOT` and `RECIPES` are append-only like `TEXTURES`. Verify with:

```powershell
uv run --no-project --python 3.13 --with pillow python -I Reference/minecraft-entities-26.3/tools/verify_reference.py
```
