# minecraft-entities-26.3

Entity textures from the Java 26.3 client jar, used as stand-ins until the project's own art replaces them (Jesus,
2026-10-14; each one has a row in `Docs/StandIns.md`). Hash-checked: never edited by hand.

- `assets/minecraft/textures/<name>`: the jar's `assets/minecraft/textures/<name>`, byte for byte.
- `textures.json`: each texture's name, SHA-1, size and pixel size, in `tools/build_reference.py`'s `TEXTURES` order.
- `SHA256SUMS.txt`: every file's SHA-256.

| Texture | Used by | Step |
|---|---|---|
| `entity/experience/experience_orb.png` | `/Game/Entities/T_ExperienceOrb` (XP orbs, value report `xp.value_icons`) | survival 12 |

Later steps (mob textures, survival track T6) append to `TEXTURES`; entries are never reordered or removed. To rebuild,
move this folder away (keep `tools/` and this README), then run `tools/build_reference.py`. Verify with:

```powershell
uv run --no-project --python 3.13 --with pillow python -I Reference/minecraft-entities-26.3/tools/verify_reference.py
```
