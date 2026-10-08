# Java 26.3 HUD reference

Location: `C:\dev\projects\Unnamed\Reference\minecraft-hud-26.3`

For a new implementation chat, copy [the simplified start-here prompt](START-HERE-PROMPT.txt). It lists the reading order, each file's purpose and the first implementation step.

Start with [the visual PNG catalog](catalog.html) and [interactive placement study](layout.html). The study uses actual extracted sprites and source-derived positions; it is not a vanilla screenshot.

- [Copy-ready Unreal implementation prompt](IMPLEMENT-HUD-IN-UNREAL.txt)
- [Detailed research: layout, gameplay meaning, visibility and Unreal design](HUD-RESEARCH.md)
- [Every PNG explained](PNG-GUIDE.md)
- [CSV inventory](manifest.csv) and [full manifest/provenance](manifest.json)
- [Verification results](evidence/verification.json) and [fixed-reference coverage](evidence/reference-coverage.json)

Contains **194 original PNGs**, **29 JSON/MCMeta asset files**, and **3 Unicode font archives**. The `assets/minecraft` tree preserves native names. A supporting Respiration enchantment definition is under `data/minecraft`. This folder is research material; nothing has been imported into Unreal or wired into gameplay.

The core library includes health and absorption states, armor, food, air, hotbar/offhand, crosshair/attack indicator, XP/locator/mount-jump, mount hearts, effects and bosses. Supporting groups cover camera overlays, fonts, spectator, notifications, latency indicators, tooltips and the temporary mode switcher. Item models/skins and runtime text, rectangles and gameplay systems are explicitly distinguished from PNG resources in the research.

Vanilla 26.3 client SHA-1: `e877b6a07acd633fb3bb475002175cec036e7b87`. All extracted files have SHA-256 records. Assets supplied by official asset index 34 override client stubs where necessary. Source inspection is pinned in `evidence/source-map.json`; decompiled source is retained outside this project.

Verify the delivered library with:

```powershell
uv run --with pillow python 'C:\dev\projects\Unnamed\Reference\minecraft-hud-26.3\tools\verify_reference.py'
```

`tools/build_reference.py` records the extraction/catalog generation process and its exact local inputs. Use verification for a routine check; rebuilding requires the recorded local client/source research inputs and official asset access. The HTML files are self-contained apart from their relative PNG links and are intended to be opened manually in a browser that permits local files. Automated preview was blocked by the in-app browser's local-file security policy. JavaScript syntax and local links passed checks; rendered appearance and browser interactions remain unverified.

Minecraft art remains Mojang/Microsoft material. This local extraction does not grant a redistribution license. Preserve provenance; no decompiled Java is distributed in this folder.
