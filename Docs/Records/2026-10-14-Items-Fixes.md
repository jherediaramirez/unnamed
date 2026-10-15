# Items fixes: push-out, Codex review 4 (#2-#5), the screen's throws (survival T2; record)

Lane `feature/survival-entities` (worktree `C:\dev\projects\Unnamed-ent`), after step 12 (`04f3079`) and the merge of `feature/survival` (`9fd38bf`). Set by SM-Entities on October 14 ([the handoff](2026-10-14-Items-Handoff-Fixes.md)).

## 1. What was built

- **S8a-b, push-out** (`FoundationItemEntities::MoveBox`, `IsInside`; `FFoundationAuthority::GatherEntityCollision`): a drop or orb whose body is inside a solid box (more than touching) moves through blocks instead of its normal step. While its centre is in a box it heads for the nearest unobstructed side of the centre's cell (-X, +X, -Y, +Y; the first on a tie), or up when every side is blocked, even over open space below (minecraft.wiki "Item (entity)", Behavior). Once the centre is clear it keeps that heading until the body is; then the normal step resumes. When a body is inside, the gather widens by two blocks so the neighbour cells are known. The value report and the survival findings have nothing on it (grep `push`, `closest`, `inside`).
- **Codex review 4:**
  - #2: two stacks merge only when the sum fits the max stack (40 and 40 stay apart).
  - #3: on a tie the seeker moves into the other (`B.Count >= A.Count ? B : A`).
  - #4: at `foundation.Items.Entities 0` every stack draws 1 copy (`FFoundationItemEntityView::Update`).
  - #5: `Drop` calls `CheckHand` first (dead: `Dead`; a changed hand: `ItemChanged`).
- **T1's container throws** (`FFoundationAuthority::Throw`, factored out of `Drop` unchanged): `ContainerClick`'s `Thrown` and `ContainerClose`'s leftover are thrown and the trial inventory commits; at switch 0 both stay refused with `DropDeferred`. A failed spawn is refused with `NoSupply`, as Q's.
- **Probe 13d:** the orb pickup sound resolves to a file in the sound log (the sound batch's `entity.experience_orb.pickup`).
- **Docs:** `Docs/Items.md` (the S8b bullet, known issues, the screen's gestures, Tests).

## 2. Tests

- **Native:**
  - `Unnamed.Foundation.Items.Entities.PushOut` is new: the nearest side (-X), the next nearest with -X walled (-Y), the top when every side is walled, a block over the edge only (out the short way), and an orb.
  - `.Merge`: "40 and 40 stay" (was 64 + 16) and "32 and 32 make one 64"; the tie keeps B, at B's place, with the same delay and age.
  - `.Throw`: dead → `Dead`, a stale command → `ItemChanged`, nothing thrown.
  - `Unnamed.Foundation.Inventory.Authority` (T1's): the two `DropDeferred` asserts are unchanged and now run with `bItemEntities = false` set explicitly (it was the struct default). Added: a switch-1 right click outside throws one (delay 40, cursor 8); after the switch-0 close, the cursor is kept and nothing is thrown; a switch-1 close throws the cursor's 3.
- **Probe:**
  - 13b's new case "copies off": the 64-stack draws 1 copy at switch 0.
  - 13d: "the orb pickup sound resolves and plays" (logged 2, with a file 2, pickups 2).
- **Mutations** (`C:\dev\sandbox\items-xp\mutations-fixes.ps1`, log `logs\mutations-fixes.txt`; plus `mf-pushout-edge` and the runtime `mf-copies-run`): all 11 killed.
  - The push-out off, the farthest side, no top exit, the edge's deepest way.
  - Partial merges, the tie's direction.
  - `Drop` without `CheckHand`.
  - The click never throwing, the close never throwing, the close ignoring the switch.
  - The copies at switch 0 (the interaction run failed only "copies 5").
- **Review A** (Sonnet 5.5, diff only, `C:\dev\sandbox\items-xp\review\fixes.diff`): nothing high.
  - Fixed, mediums 1 and 2: a centre-clear overlap now always leaves by the shallowest way (it had trusted any velocity, so a moving item with a block placed on its edge could cross the block). The edge case was added.
  - Left, documented: medium 3 (FX-a) and medium 4 (FX-b).
  - Lows: the sound log is cleared in the fly-in case's setup, outside the hunk the reviewer saw; the lab's switch defaults to off.
- **RunChecks final -Flight smoke:** GREEN, `C:\dev\projects\Unnamed-ent\Tests\Evidence\2026-10-14\Checks-final-20261014-185747\summary.txt`.
  - native 365 of 365; voxel 207; hashes 9 of 9; runtime-all 159; interaction 114; tour 29; create-world 38; flight PASS; verifiers PASS.

### Provisional values shipped

| Value | Number | Source | Where |
|---|---|---|---|
| Push-out speed | 0.1 blocks per tick (10 cm per step), constant while inside | estimate (the wiki and the report give none) | `FoundationItemEntities.h`, `PushOutCm` |
| Push-out side order | nearest face of the centre's cell; ties -X, +X, -Y, +Y; up only when all four are blocked; no gravity or drag while inside | wiki (sides, then top) + estimate (nearest, order, motion) | `FoundationItemEntities.cpp`, `PushOut` |
| Centre clear, body still in a box | out of the first overlapping box by its shallowest way: -X, +X, -Y, +Y or up | estimate | `FoundationItemEntities.cpp`, `PushOut` |

## 3. Known issues

| # | Issue | When |
|---|---|---|
| S8a-a | Drops and orbs sink through water. | Fluids for entities |
| FX-a | The push-out's speed, order and motion are estimates (section 2's table). A neighbour cell with any collision box (a slab, a fence) counts as obstructed, so the body may go up where vanilla might leave by that side (review A, medium 3: left, as documented). | Value-holder requests / vanilla captures |
| FX-b | The push-out derives the block grid's origin from the body's box standing on `LocalCm` (true for items and orbs today, both bottom-centred); a body type centred otherwise would need the origin passed in (review A, medium 4). | A new entity type on `MoveBox` (T6 mobs) |
| S8b-c, XP-c | As before (throw offsets, copy offsets, orb values). | Value-holder requests |
| XP-e | The entities reference's verifier crashes without a FAIL line when `textures.json` is missing. | T6's first change to that reference |
