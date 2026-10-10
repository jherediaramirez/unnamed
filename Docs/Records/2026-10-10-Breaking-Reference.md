# Breaking: the block reference's sounds, cracks and particle textures

Break lane `feature/voxel-break`. These are the vanilla facts and assets the breaking prototypes need ([ideas](2026-10-10-Breaking-Feel-Ideas.md), Jesus's answers Q1 (a), Q2 (a), Q3 (a) and Q7 (a)). They were added to `Reference/minecraft-blocks-26.3` through its own tools only. No runtime code changes here.

## What changed

- **`tools/FactsDump.java`:** it also writes `facts/block_sounds.csv`. Each imported block's sound type (volume, pitch, and the break, hit, place, step and fall events) comes from the public `BlockState.getSoundType()`. The run throws unless every state shares its default state's type, and all 618 blocks do.
- **`tools/build_reference.py`:**
  - **Sounds:** it copies `sounds.json` and the 762 break and hit oggs of the imported blocks' sound types from asset index 34. Each ogg is checked against its object's SHA-1 and size.
  - **Crack and particle textures:** it copies `destroy_stage_0..9.png` and `shelf_mushroom_particle.png` from the pinned jar, and writes `block_particles.json`: every imported state's model `particle` texture.
  - **Failure handling:** the index, `sounds.json`, the sound files and the particle resolution are validated before the first write.
  - **Id check:** the ids in `block_sounds.csv` must match `block_state_facts.json`, in order.
- **`tools/verify_reference.py`:**
  - **Sound files:** checked against the asset index, and the set on disk must equal the set the facts require. A deleted or extra ogg fails.
  - **Without the index:** the run prints `SOUNDS NOT CONTENT-CHECKED`.
  - **New facts:** `block_sounds.csv` and `block_particles.json` get content checks.
- **The asset index pin:**
  - The official index 34 has SHA-1 `abfaa525…`.
  - Modrinth's local copy differs from it in 142 lang entries only, so the tools pin the SHA-1 of the non-lang entries (`9ba09e4f…`).
  - Both are in the README.
- **`shelf_mushroom_particle`:** it is the one particle texture missing from `textures.json`. It is recorded under its true name, with its PNG copied, but not appended, so no texture, layer or model index moves and `DA_Blocks`'s content is untouched. `block_particles.json` lists it under `textures_outside_textures_json`, so the importer can choose a fallback.
- **`DA_Blocks`:** `Scripts/Editor/RestampBlockDigest.py` changes only `SourceSha256` (Q2 (a)), from `095d5c7b…` to `3d9371d9…`, the hash of the new `SHA256SUMS.txt`. Its T3D export before and after differs in that one line, and `ImportBlocks.py` then reports `BLOCK_IMPORT PASS … existing assets were not rebuilt`.

## Numbers

| | Before | After |
|---|---|---|
| Files hashed | 1,501 | 2,277: 776 new, 0 removed; 4 changed (README and the three tools) |
| New files | | 762 oggs (6,855,474 bytes), 10 destroy stages (1,412), `sounds.json` (635,867), `block_sounds.csv` (73,672), `block_particles.json` (42,767), `shelf_mushroom_particle.png` (200) |
| Verifier | 1,501 hashed, 1,482 compared with the jar | 2,277 hashed, 1,482 compared with the jar, 763 with the asset index |

## Verification

- **Diff proof:** every earlier file is byte-identical except `SHA256SUMS.txt`, `README.md` and the three tools. That includes `textures.json`, the manifest, models, states, `items.json` and the facts.
- **Regeneration proof:** the repo's final tools, run again into an empty folder, give all 2,278 files byte-identical, `SHA256SUMS.txt` included.
- **Mutations, on a copy:**
  - deleting an ogg fails;
  - changing an ogg's bytes fails;
  - a missing index prints the not-checked line;
  - dropping a `block_sounds.csv` row fails.
- **Suites, after the review fixes:**
  - both reference verifiers report REFERENCE OK;
  - `source-check.mjs` PASS;
  - `npm test` 57/57;
  - every native suite 162/162.
- **Pre-commit review** (diff only, Sonnet 5.5): items 1-4, 6 and 7 were fixed (above). The resolver note (5) and the unused official pin (8) need no change.
- **Evidence:**
  - `Tests/Evidence/2026-10-10/Break-Restamp-20261010-094723/` (T3D before, first stamp);
  - `Tests/Evidence/2026-10-10/Break-Restamp-20261010-095614/` (final stamp, import and native logs);
  - `C:\dev\sandbox\break-ref\` (diff-proof, diff-proof-2, regen-proof-2, mutations).

## Notes

- The item reference is unaffected: its inputs (`block_facts.csv`, `creative_tabs.json`, `block_manifest.json`) are byte-identical.
- No world hash, golden or saved world depends on these files.
- **Known issue, the research inputs live in disposable sandbox folders.**
  - `C:\dev\sandbox\mc-26.3-blocks\out` predates the torch, so a rebuild needs `--research C:/dev/sandbox/t2work/research`, which is byte-identical to the reference's own `research/` files.
  - **When:** before anyone deletes `C:\dev\sandbox\t2work` or rebuilds this reference again.
  - **Fix:** keep that folder, or teach `build_reference.py` to read its research inputs from the reference's own `research/` copy.
- **Proof 3:** after the review fixes, the final `build_reference.py` ran end to end into an empty folder, from facts whose `FactsDump.java` source is byte-identical to the final one. All 2,278 files are byte-identical with the repo (`C:\dev\sandbox\break-ref\regen3`).
- The fresh `creative_tabs.json` differs only in Java identity hashes, so the accepted file is kept, as in torch T2.
