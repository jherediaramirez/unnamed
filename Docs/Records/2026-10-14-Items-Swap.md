# Items swap: orb merge, bounce and sounds, the S8a value rows (survival T2; record)

Lane `feature/survival-entities` (worktree `C:\dev\projects\Unnamed-ent`), after the items fixes (`24135e3`) and the merge of `feature/survival` `079fe3e` (`229528f`). Set by SM-Entities on October 14 ([the handoff](2026-10-14-Items-Handoff-Swap.md)).

## 1. What was built

- **Codex review 8** (`C:\dev\tools\shift-managers\survival\codex\reviews\8-T2-12\report.md`):
  - **#1, the orb merge:** an orb in another's reach takes it in only when their ids differ by a multiple of 40, or when the seeker spawned this step and a 2.5% roll passes (the Chinese minecraft.wiki's "Experience orb", Merging). The roll is a pure function of the two ids (`FoundationExperienceOrbs::CanMerge`), so a replay merges the same orbs. A new orb (`FFoundationExperienceOrb::bNew`, set by `SpawnExperience`) also scans in the step it spawned; the flag clears after that scan (a frozen orb keeps it until it steps). The 0.5 growth on every axis stays.
  - **#2, the bounce:** an orb landing faster than 6 cm per step leaves the ground at half its landing speed (`FoundationExperienceOrbs::Move`, after `MoveBox`; the items' `MoveBox` is unchanged). Both numbers are provisional: the wiki's 1.21.4 history says orbs are bouncy and gives no factor, so the items' -0.5 is borrowed, and the threshold keeps a resting orb from jittering.
  - **#3, the level-up sound** (`survival.r5.rules`): volume 0.75 x min(level / 30, 1), pitch 1, on a positive gain reaching a multiple of 5 (a gain passing several counts the highest multiple reached), only more than 100 steps after the last one. The rule is the plain helper `GetLevelUpVolume`; the view times it with the local player's authority step (`Facts.Step`).
  - **#4, the pickup sound:** pitch 0.9 + 0.35 x (U - V) (`survival.r5.rules`, `GetPickupPitch`); the volume 0.1 is now sourced (`survival.r5.play_calls`: 50 measured calls, all at 0.1).
  - **#5, switch 0:** `foundation.Items.Xp`'s help text, the header and `Docs/Items.md` say that orbs already in the world stay, fly and pay, as drops stay at `foundation.Items.Entities 0`.
- **S8a's provisional rows** (`value-swaps.md` lines 57-58), checked against the value report:
  - Ground friction: `items.ground_friction` gives stone about 0.588 retention per tick, which is today's 0.6 x 0.98. Citation only; no literal moved.
  - Ground bounce -0.5 (`items.physics`): the report applies it after the collision, which has already zeroed a landing's vertical speed, so it adds no step. A comment cites it; item motion is unchanged.
  - Item size: the 0.25-block width (`items.merge_boundary_stationary`) is `BoxCm` 25 already. Citation only.
  - The random scatter (`items.drop.random_scatter`, 0.5 x U sideways, 0.2 up) is a player's drop mode (beside the hand throw), not a block's. The block drop keeps its provisional spread and speed (SM-Entities' call, option a); the block-loot spawn velocity goes to the program's requests (section 4).
- **FX-a was tried and left** (see section 3).

## 2. Tests

- **Native** (`Source/Unnamed/Tests/FoundationExperienceOrbTests.cpp`; literals from the reports, never the constants under test):
  - `.Merge`: the step-20 case and the 99/100 cm cases now use ids 1 and 41. New: ids 1 and 2 or 21 never merge, 81 does; a new orb's roll passes 10 to 45 of 1,000 pairs and is the same each call, and an old orb never rolls; a new orb scans in its first step; an authority pair through the roll merges on the first step, and a pair moved into reach on the second step does not (the flag has cleared).
  - `.Pickup`: the merged-orb case uses ids 1 and 41.
  - `.Bounce` (new): landing at 23 cm per step leaves at 11.5 and rises the next step; landing at 3 stays down; 5.9 stays down, 6.1 leaves at 3.05; an orb on the ground under a 10 cm downward pull stays down.
  - `.Sounds` (new): the level-up volume (level 5 0.125, passing 10 0.25, level 40 capped at 0.75, none without a gain to a multiple of 5, none on the first update, none 10 or 100 steps after the last sound, again at 101; a sound at step 0 counts; a step before the last sound's counts as never), the pickup pitch at four points, the volume 0.1.
  - Items tests: 29 of 29 (was 27; `C:\dev\sandbox\items-xp\logs\swap-t3`).
- **Mutations** (`C:\dev\sandbox\items-xp\mutations-swap.ps1`, `mutations-swap2.ps1`, log `logs\mutations-swap.txt`): 17 of 17 killed. The id rule off (`ms-merge-noid2`; the first form, `return true;`, did not build) or at 20, no roll, an old orb rolling, a 10% roll, no first-step scan, the flag never clearing; the bounce off, without its threshold, at factor 1, on a resting orb; the level-up gap off, at its edge, the volume uncapped or constant, the reset guard off; a uniform pickup pitch.
- **Review A** (Sonnet 5.5, diff only, `C:\dev\sandbox\items-xp\review\swap.diff`, 474 lines): nothing high.
  - Fixed: medium 1 (a level-up after a reset authority, or a frame without the authority's player, could be silenced: a step before the last sound's counts as never, and there is no sound without the player); low 3 (an orb resting under a downward pull re-bounced: only an orb in the air bounces); low 4 (the threshold's edge); low 5 (the roll and the flag through the authority); low 7 (the header's "highest multiple reached").
  - Left: medium 2 (the view's calls are not covered natively: the view needs a world; probe 13d counts the pickup hook calls); the minor wording notes.
- **RunChecks final -Flight smoke:** GREEN, `C:\dev\projects\Unnamed-ent\Tests\Evidence\2026-10-14\Checks-final-20261014-202628\summary.txt` (24 min, machine: none).

## 3. What worked and what failed

- **FX-a grew past "small".** Counting a side as blocked only when one box covers its whole cell let the drop head into a bottom slab's cell. On the cell boundary its centre was in neither box, so the centre-clear branch took the first box it overlapped; the slab's shallowest way pointed back at the placed block, and the drop sat at x 110 for 40 steps, overlapping both. A fix needs the centre-clear branch to weigh every overlapping box (and say where a slab sends it: up onto it, or past it), so FX-a stays a known issue.
- A mutation that replaced a condition with `return true;` failed to build (unreachable code is an error); the equivalent `if (true || ...)` form builds.
- `R` is a PowerShell alias (`Invoke-History`): a helper function named `R` silently never ran.

## 4. Known issues

| # | Issue | When |
|---|---|---|
| S8a-a, XP-b | Drops and orbs sink through water. | Fluids for entities |
| FX-a | A slab or fence neighbour counts as obstructed for the push-out. Making it free needs the centre-clear branch to weigh every overlapping box (section 3). | A push-out pass with partial blocks (T5's shapes) |
| FX-b | The push-out assumes bottom-centred bodies. | T6's first entity on `MoveBox` |
| S8b-c, XP-c | Throw offsets, copy offsets, the remaining orb values (split, spawn, merge box, pickup rate, despawn, step order, bounce, quad centre, draw distance). | Value-holder requests |
| SW-a | The block drop's spawn spread and speed (±0.25 blocks, ±0.1 sideways, 0.2 up) have no report source; the report's random scatter is a player drop. | A value-holder request (block-loot spawn velocity) |
| XP-a, XP-d | No Silk Touch, no Mending. | Enchantments |
| XP-e | The entities reference's verifier crashes without a FAIL line when `textures.json` is missing. | T6's first change to that reference |

### Provisional values shipped

| Value | Number | Source | Where |
|---|---|---|---|
| Orb bounce factor | 0.5 x the landing speed | estimate (wiki: "bouncy", no factor; the items' -0.5 borrowed) | `FoundationExperienceOrbs.h`, `BounceFactor` |
| Orb bounce threshold | landing faster than 6 cm per step (2 x gravity) | estimate | `FoundationExperienceOrbs.h`, `BounceMinCm` |
| Orb merge id rule, new-orb roll | ids 40 apart; 2.5% when the seeker is new | Chinese wiki (the report has only the 20-tick scan) | `FoundationExperienceOrbs.h`, `MergeIdPeriod`, `NewMergeChance` |
| Block drop spawn | ±0.25 blocks, ±0.1 per tick sideways, 0.2 up | estimate (facts section 2); request: block-loot spawn velocity | `FoundationItemEntities.h`, `SpawnSpreadCm`, `SpawnSidewaysCm`, `SpawnUpCm` |

Sourced in this step (were provisional): the pickup sound's volume 0.1 and pitch rule, the level-up volume, trigger and gap, the item ground friction.
