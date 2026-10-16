# Line 1 closing record: T1 Containers (furnace, barrel, chest)

Lane `feature/survival-containers`, worktree `C:\dev\projects\Unnamed-inv`. Under the feature freeze (`C:\dev\tools\shift-managers\survival\FINAL-SCOPE.md`: "Line 1, SM-Containers: 18-chest → 8b2b furnace particles → LINE DONE"), line 1 ends with 8b2b (a01c2f02). There is no next-step handoff. Bugs go to the QC pass (QC-KNOWN-BUGS.md), and the chest lid comes after QC.

## What line 1 delivered

Run 2's steps:

| Step | Commit | Record | What |
| --- | --- | --- | --- |
| 9 | f862db54 | [2026-10-15-Inventory-9.md](2026-10-15-Inventory-9.md) | The barrel: 27 slots on the block-entity store, the 3-row generic_54 screen, `open` state and sounds by viewers (R16), Codex 29 #1. |
| 18-chest | 29b1fcc6 | [2026-10-15-Inventory-18-chest.md](2026-10-15-Inventory-18-chest.md) | Chests and trapped chests: pairing (facing, sneak = single), the double chest's 54 slots (6 rows), blocked above, one open/close sound per chest; drawn as boxes, no lid. |
| 8b2b | a01c2f02 | [2026-10-15-Inventory-8b2b.md](2026-10-15-Inventory-8b2b.md) | R15's furnace, smoker and blast furnace smoke and flame from the one ambience pass; new particle assets (stand-ins). |

Before run 2, the track built on this lane:
- [8a1](2026-10-15-Inventory-8a1.md): the furnace's store, tick and fuel.
- [8a2](2026-10-15-Inventory-8a2.md): its screen and the cooking outputs.
- [8b1](2026-10-15-Inventory-8b1.md): the lit state, the smoker and blast furnace, the sounds.
- [8b2a](2026-10-15-Inventory-8b2a.md): Codex 22 #2/#3, R23's fit and reach, and the pause clock.

Each step merged feature/survival into the lane before the next. The last merge was 8a5e87ea (feature/survival ad045c40).

## Open known issues (with "when")

- **The chest lid is deferred.** Both chests draw B1's stand-in box; a double chest draws two boxes. When: after QC, with T5 B4's render-shape change, R16's lid curve and R17's model layers.
- **Furnace particles look provisional.** They are small and faint, and the smoker's barely shows at 2 blocks. Their size, grey, flight and frame timing are provisional, and the smoke is lit by its quad's own normal. When: value-holder values or a black-box capture (provisional-values.md, T1 8b2b), or a look call in QC.
- **The furnace ambience scheduling is provisional:** 1 in 50 per lit block within 16 blocks, where vanilla's is client-spatial (R15). The sound's reach and place are provisional too. When: a scheduling model from value-holder.
- **A double chest's menu reach checks only the opened half;** vanilla checks both. When: a container pass, if a measurement shows it matters.
- **Gaps left for their own steps:** vanilla's 5-tick opener recheck (barrel and chest); spectators never view; the ender chest; cats on chests; the trapped chest's redstone; the chest both-sides tie (provisional). When: their own steps.
- **Carried:** the creative screen pausing, the focus loss under a survival screen, the missing recipe and fuel items (rabbit, cod, salmon and the rest: see the furnace section of Docs/Items.md), Codex 18 #2 (multiplayer instance ids). When: the QC pass or their own steps.

## Evidence

- 8b2b final run: `Tests/Evidence/2026-10-15/Checks-final-20261015-190240/summary.txt` (GREEN, final, smoke flight; native 506/506, hashes 9/9).
- 8b2b tour shots: `Tests/Evidence/2026-10-15/Playground-tour/Runtime-20261015-191647/runtime-60-tour-24i-furnace-particles.png` and `-off.png`.
- Earlier steps: each record's Evidence section. Sandbox folders:
  - `C:\dev\sandbox\inv8b2\` (8b2a, 8b2b);
  - `C:\dev\sandbox\inv9\` (9);
  - `C:\dev\sandbox\inv18\` (18-chest).

## Open questions (for the SM)

1. **Push:** the track branch stays local. Pushing is Jesus's call.
2. **The merge:** the lane's merge into feature/survival waits for SM-Containers' go: `RunChecks -Profile merge -Flight smoke` on the final HEAD.
