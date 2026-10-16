# Resources

Source material that lives outside the engine: Blender files, exports, plans and reports. It is gathered here so
nothing has to be hunted for across `C:\dev`. The engine never reads this folder; the import scripts read the copies
under `Art/`. Copied 2026-10-16. The originals stay where they were, and the plans' paths point there:
`C:\dev\output\<name>` is `Resources\PlayerBody\<name>` here, and `C:\dev\projects\better-steve` is
`Resources\PlayerBody\better-steve`.

Blender, glTF, FBX, PNG, JPG and MP4 files are stored with Git LFS (`.gitattributes`). Blender autosaves (`*.blend1`)
are not kept.

## PlayerBody: the full-body first/third-person player

**Start here:** [FP-BODY-M1-BRIEF.md](PlayerBody/FP-BODY-M1-BRIEF.md), the brief for the next step (M1, the body in
the game), then [the plan](PlayerBody/unnamed-fp-tp-integration-plan-20261015/PLAN.md).

**Where it stands (2026-10-16):** M0, the asset proof (Blender export, Unreal import into the sandbox
`C:\dev\sandbox\fpbody-m0`), is done and every gate passed. M1a, the body in the game in both views, is done
(`Docs/Records/2026-10-16-Player-Body-M1.md`; the engine's copy is `Art/Characters/UnnamedPlayer/`). M1b (the strike
through the real hand) is next. M2-M4 come after: the rest of the library, new clips, polish.

| Folder | What | Status |
|---|---|---|
| `unnamed-fp-tp-integration-plan-20261015/` | `PLAN.md`: the M0-M4 plan, Jesus's decisions, architecture, acceptance tables. `POSE-CLIP-INVESTIGATION-PROMPT.md` | **The plan** |
| `unnamed-player-motion-polish-v002-20261015/` | `blend/UP_Player_arm_polish_v002.blend` = **the accepted export source** (SHA-256 `26E585D5…4D22C7`). `UP_Player_master_v002.blend` is experimental (not a source). The integration prompt and context, scripts, validation, source provenance | **Source .blend** |
| `unnamed-player-export-v002c-20261016/` | **The accepted export**: `fbx/SK_UnnamedPlayer.fbx`, `fbx/anims/A_UP_*.fbx` (66 clips), `SM_UP_Pickaxe.fbx`, skins, `UP_manifest_v002c.json`; the export script; `REPORT.md` and `evidence/final_20261016_015709/` (M0's final report and hashes) | **Accepted export, M0 proof** |
| `unnamed-player-blender-v001-20261015/` | The builder package: `README.md`, `HANDOFF.md`, `blend/UP_Player_master_v001.blend` (baseline) and `UP_Player_review_v001.blend` (press Space to watch every clip), scripts (`run_all.ps1` rebuilds), validation, v001 exports (old strikes: don't import) | Builder, baseline |
| `unnamed-player-review-20261015/` | `minecraft_steve_rigged.glb`: the original model, "Minecraft Steve (Rigged)" by Bully Magraptor, **CC-BY-4.0, credit required** | Original source |
| `unnamed-player-blender-handoff-20261015/` | The first Blender handoff brief | History |
| `unnamed-player-export-v002a-20261015/`, `…-v002b-20261015/` | `REPORT.md` of the two earlier export runs (extra root bone; pose clips) | History |
| `up-pose-clip-investigation-20261016/` | Why the single-frame pose clips imported oddly, and the fix M0 used: `REPORT.md`, scripts, evidence (the five raw dumps over 2 MB stay in `C:\dev\output`) | Done |
| `better-steve/` | **Better Steve**, the game's current F5 body (`Art/Characters/BetterSteve/`): `foundry/BetterSteve.blend`, the build/animation scripts, exports, previews, handoff docs. The original folder `C:\dev\projects\better-steve` is not a git repo, so this copy is its only backup | Current body |

Not copied, still on disk: the Unnamed Player preview sheets and videos (`previews/`, about 325 MB, regenerable by the
builder scripts) and run logs; the M0 sandbox project `C:\dev\sandbox\fpbody-m0` (Unreal assets for the proof only);
the older prototype `C:\dev\output\perfect-steve-rigged\` (September) and `C:\dev\projects\lucid-body\art\*.blend`
(earlier arm/Steve experiments).

Blender 5.2.2: `C:\dev\tools\blender-5.2.2\blender.exe`.
