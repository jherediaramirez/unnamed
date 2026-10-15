# Handoff: survival step 19a, armor (T2), after the items tail

Lane `feature/survival-entities` (worktree `C:\dev\projects\Unnamed-ent`), after the items swap (`c4ed753`, [the swap record](2026-10-14-Items-Swap.md)). SM-Entities set the scope on October 14. **SM-Entities sends 19a's brief with this handoff; it wins over this file where they differ.** Check `git log` first. Merge `feature/survival` only on the manager's merge go, never in the middle of a step.

## 1. Reading list (in order)

1. This file, all of it.
2. SM-Entities' 19a brief (sent with this handoff).
3. `C:\dev\tools\shift-managers\survival\codex\reviews\11-T2-fixes\report.md` (about 30 lines): findings #1 to #5.
4. [The swap record](2026-10-14-Items-Swap.md), sections 3 and 4 (what FX-a showed, known issues).
5. [The fixes record](2026-10-14-Items-Fixes.md), section 1 (the push-out) and its provisional table at the end of section 2.
6. `C:\dev\tools\shift-managers\survival\value-swaps.md`: line 36 (the screen's armor slots), lines 71-72 (the armor formula, armor on the body), line 221 (the equip sounds).
7. Value sources, by grep only, never read whole, and never open Appendix H's paths:
   - `C:\dev\projects\value-holder\MINECRAFT-26.3-VALUES.md`: `armor.formula`, `armor.reduction_grid` (section "Armor formula", about line 207), `armor.body` (parked: first person only).
   - `C:\dev\projects\value-holder\SURVIVAL-FINDINGS-26.3.md`: `item.armor.equip_` (the R5 sound table, about line 3616) and `survival.r5.play_calls`.
8. `C:\dev\tools\shift-managers\survival\PROGRAM.md`, sections 4 to 6 (the asset locks; the item reference rule; first person only).
9. The code, by function (section 4). Never read `FoundationAuthority.cpp` (about 1,900 lines) or `FoundationInteractionProbe.cpp` whole: grep, then read the function.

## 2. The work

### 2.1 The items tail first (Codex review 11, #1 to #5)

1. **#1, push-out ping-pong (low-medium).** The centre-clear branch of the anonymous `PushOut` (`FoundationItemEntities.cpp`) leaves the first overlapping box by its shallowest side and never checks where that lands. Between a full block and a partial box less than 25 cm apart, it goes back and forth for ever.
   - **How it relates to FX-a:** the swap tried FX-a (a side counts as blocked only when one box covers its whole cell). It hit exactly this bug: with a bottom slab beside the placed block, the drop's centre crossed the cell boundary, the centre-clear branch took the slab, the slab's shallowest way pointed back at the block, and the drop sat at x 110 for 40 steps. The swap reverted FX-a.
   - **Fix #1 first.** Codex's options: among the five ways, take only one whose destination box is clear of every solid (else up, a provisional fallback); or push out of the union of the overlapped boxes. Add Codex's scenario as a test: stone at x 0-100, an up-facing amethyst cluster box at x 118.75-181.25, a drop at x 105.
   - FX-a can then come back on top of it: free unless one box covers the whole cell, with the swap's slab test (every side walled, -X a bottom `oak_slab`: the drop leaves by a side, out of every block, below the top cell). The slab case is the one to beat.
2. **#2, dead throws (low):** `ContainerClick`'s `Thrown` (`FoundationAuthority.cpp`, about line 935) and `ContainerClose`'s leftovers (about line 966) call `Throw` with no `Facts.bAlive` check. Refuse both with `Dead` for a dead player, as `Drop` does through `CheckHand`. Test it in `Unnamed.Foundation.Inventory.Authority` or `.Throw`.
3. **#3, probe 13d's sound check (low):** "the orb pickup sound resolves and plays" (`FoundationInteractionProbe.cpp`, about line 1262) counts sound-log entries with a file. `FFoundationBreakEffects::PlayEvent` logs before it checks `File.Wave`. Either add an accessor on the break effects that says whether the event's wave is loaded, and check it, or rename the check to "resolves". The probes run with `-nosound`.
4. **#4, docs (low):** `Docs/Items.md` line 219, the `.Merge` test line, still says "40 + 40 is 64 + 16; a tie goes into the seeker". It should say "40 + 40 stay apart; on a tie the seeker moves into the other".
5. **#5, switch 0 (plausible, low):** `MoveBox` pushes out at `foundation.Items.Entities 0` and `foundation.Items.Xp 0`. Recommendation: note it as part of the motion that existing drops keep (the S8a and swap precedent: they stay, fly and pay), in the help texts and `Docs/Items.md`. Don't add a switch branch.

Each fix gets a mutation (`C:\dev\sandbox\items-xp\mutate.ps1`, a fresh `-Name`; a mutation that leaves unreachable code fails to build, so use `if (true || ...)` forms).

### 2.2 Step 19a, armor (scout AC's plan row 19a; the brief has the details)

- Armor slots exist: `FFoundationInventory::Armor` (slots 36-39, T1 5a), the screen's slots (T1 5b, value-swaps line 36), and `EFoundationEquipmentSlot` (`FoundationItems.h:30-35`). Every item's `Equipment` is `None` today (`FoundationItems.h:126-128`).
- The damage side exists: `FFoundationVitals::Armor` and `ArmorToughness` (lab stand-ins, `FoundationVitals.h:55-60`), and `GetDamageAfterArmor` (`FoundationVitals.cpp:298`), applied in the hurt path unless the damage type bypasses armor. Check it against `armor.formula`/`armor.reduction_grid`: divisor = 2 + toughness / 4; effective = min(max(armor - dmg / divisor, armor x 0.2), 20); taken = dmg x (1 - effective / 25). Then derive `Armor` and `ArmorToughness` from the worn pieces instead of the lab's fixed values.
- The HUD armor row exists (`Hud/HudDrawList.cpp:227-235`, drawn from the HUD model's `Armor` when above 0): wire it to the worn total.
- Equip sounds: `item.armor.equip_{leather,chain,iron,gold,diamond,netherite,turtle,copper,elytra,generic}`, PLAYERS, volume 1, pitch 1 (`survival.r5.play_calls`; value-swaps line 221). They are already in `DA_Sounds` (T5's B0); play them through `FFoundationBreakEffects::PlayEvent`.
- `carved_pumpkin` as head equipment: it's a block item (`DA_Blocks`, T5's lock). Give it the head slot in code by id, not in `DA_Blocks`.
- First person only: no armor drawn on the player (`armor.body` is parked).
- **Does armor need DA_Items changes? Yes.** `Reference/minecraft-items-26.3/items.json` has 86 items (tools, weapons, materials, buckets, food) and **no armor pieces** (no helmets, chestplates, leggings, boots, turtle shell or elytra). The pieces need:
  1. the item reference extended through its own `tools/build_reference.py` (hash-checked: move the accepted folder away first, as CLAUDE.md says), with the equippable, armor and toughness data from the 26.3 data generator's item components report;
  2. the sprites (stand-ins: a `Docs/StandIns.md` row each);
  3. a `DA_Items` re-import (`Scripts/Editor/ImportItems.py`).

  The re-import needs the `DA_Items` lock, which SM-Entities requests. Since T1's step 6, `DA_Items` carries `DA_Recipes`' digest, so re-import on top of the latest `feature/survival`, never by hand-merging.

## 3. Working agreement

- Report to SM-Entities (`session_01HHSREoLkWuRSPoQFWbsqfX`) with `send_message`. First send a plan: the files against the hot files, after diffing them against the latest `feature/survival`. Then wait for the go.
- Never push. Never use bare `git stash`. No CLAUDE.md edits. `DA_Blocks` and `DA_Sounds` are T5's; `DA_Items` only with the lock.
- **Stage by path only, never `git add -A` or `git add .`.** An empty untracked `Source/Unnamed/Tests/FoundationExperienceOrbs.h` (0 bytes, from a failed write in the swap) stays unstaged. Deleting it is Jesus's call (the classifier refused it; it's on his list).
- Never re-record goldens or world hashes. If one would move, stop and report.
- Values come from the value report and the survival findings (grep by evidence id), the wiki, or black-box runs. Never from decompiled code.
- Before a heavy launch outside RunChecks, run `Scripts\QuietWindow.ps1 -Name <what>` (add `-Game` for `-game` runs). Before a `-game` run, wait out `C:\dev\sandbox\locks\cef-e2e.lock`.
- Wrap-up:
  - `Scripts\RunChecks.ps1 -Profile final -Flight smoke` after the last code change.
  - A Sonnet 5.5 review of the diff only.
  - Report A: staged name-status, suite counts against their base, changed pinned values with sources, the provisional table.
  - Commit on the go, then write the handoff, then send report B.
- Measure the lead after every build, test run and subagent. MeasureAgentUsage wasn't available in the swap chat, and `node C:\dev\sandbox\items-s8b\measure.mjs --session <newest .jsonl under C:\Users\jherediaramirez\.claude\projects\C--dev\>` prints only cumulative totals for the lead. Report the peak context if you have a tool that gives it; else say so.
- PowerShell gotchas: a helper function named `R` is `Invoke-History` (it silently never runs). `[IO.File]` calls resolve relative paths against the process directory, not `Set-Location`, so use absolute paths. Write commit messages to a file and use `git commit -F <file>`.

## 4. Code map

| What | Where |
|---|---|
| Push-out (#1, #5) | `FoundationItemEntities.cpp`: the anonymous `PushOut` (the centre-clear branch), `MoveBox`, `IsInside` |
| Throws (#2) | `FoundationAuthority.cpp`: `Throw` (about line 396), `ContainerClick` (about 935), `ContainerClose` (about 966), `Drop`'s `CheckHand` |
| Sound log (#3) | `FoundationBreakEffects.cpp`: `PlayEvent` (about lines 1113-1124); probe 13d in `FoundationInteractionProbe.cpp` (grep `resolves and plays`) |
| Orb rules (swap) | `FoundationExperienceOrbs.h/.cpp`: `CanMerge`, `GetLevelUpVolume`, `GetPickupPitch`, `Move` (bounce) |
| Armor data | `FoundationItems.h`: `EFoundationEquipmentSlot`, `FFoundationItemDefinition::Equipment`, `FFoundationInventory::Armor`; `FoundationContainer.cpp` (the armor slots' acceptance) |
| Armor damage | `FoundationVitals.h/.cpp`: `Armor`, `ArmorToughness`, `GetDamageAfterArmor` |
| Armor HUD | `Hud/HudDrawList.cpp:227-235`, `HudModel.h` |
| Item reference | `Reference/minecraft-items-26.3/tools/build_reference.py`, `Scripts/Editor/ImportItems.py` |
| Tests | `Tests/FoundationItemEntityTests.cpp` (`.PushOut`), `Tests/FoundationExperienceOrbTests.cpp`, `Tests/FoundationInventoryTests.cpp`, `Tests/FoundationVitalsTests.cpp` |
| Scratch | `C:\dev\sandbox\items-xp\`: `runtests.ps1 -Tests <filter> -Name <fresh>`, `mutate.ps1`, `mutations-swap.ps1` (a batch to copy), `logs\`, `review\` |

## 5. Settled decisions

- Vanilla first. The value report and the survival findings beat the wiki where they differ.
- First person only, no hands. No armor on the body.
- The block drop keeps its provisional spawn spread and speed. `items.drop.random_scatter` is a player drop mode (for T4's death drops); a block-loot spawn velocity request goes to the program's lists (SM-Entities carries it).
- The orb bounce (0.5 above 6 cm per step, from the air only) and the merge's id rule are shipped as provisional.

## 6. Known issues (carried)

| # | Issue | When |
|---|---|---|
| R11-1, FX-a | Push-out ping-pong between a full and a partial box; a slab or fence neighbour counts as obstructed. | This step, item 2.1 |
| R11-2 to R11-5 | Dead throws, the 13d sound check, the `.Merge` docs line, the switch-0 push-out note. | This step, item 2.1 |
| S8a-a, XP-b | Drops and orbs sink through water. | Fluids for entities |
| FX-b | The push-out assumes bottom-centred bodies. | T6's first entity on `MoveBox` |
| S8b-c, XP-c, SW-a | Throw offsets, copy offsets, the remaining orb values, the block drop's spawn velocity. | Value-holder requests |
| SW-b | The orb view's sound calls aren't covered natively (review A, medium 2 of the swap). | A view-level test hook, when one is cheap |
| SW-c | The empty untracked `Source/Unnamed/Tests/FoundationExperienceOrbs.h`: never stage it. | Jesus |
| XP-a, XP-d | No Silk Touch, no Mending. | Enchantments |
| XP-e | The entities reference's verifier crashes without a FAIL line when `textures.json` is missing. | T6's first change to that reference |

## 7. What worked and what failed (items swap)

- **Worked:**
  - A 14-mutation background batch while the docs were written. 13 built and were killed; one needed a buildable form.
  - Pure helpers for the sound rules (`GetLevelUpVolume`, `GetPickupPitch`), so native tests reach them without a world.
  - Finding a deterministic rolled pair in the test (search ids until `CanMerge` passes) instead of hard-coding a hash result.
- **Failed:**
  - FX-a (section 2.1, #1).
  - A mutation of the form `return true;` (unreachable code is a build error).
  - A PowerShell `[IO.File]` write with a relative path left the stray empty file.
  - A helper named `R` never ran.

## 8. Tokens and evidence

- **Items swap:** lead cumulative processed 12,297.8k, output 65.1k (measure.mjs; no peak figure available), current context estimated at about 110k at report A. One Sonnet reviewer, 68.6k peak.
- **Evidence:**
  - `C:\dev\projects\Unnamed-ent\Tests\Evidence\2026-10-14\Checks-final-20261014-202628\summary.txt` (GREEN: native 375, voxel 207, hashes 9/9, runtime-all 161, interaction 116)
  - `C:\dev\sandbox\items-xp\logs\` (`mutations-swap.txt`, `swap-t1` to `swap-t3`)
  - `C:\dev\sandbox\items-xp\review\swap.diff`

## 9. Open questions

1. **For Jesus:** delete the empty untracked `Source/Unnamed/Tests/FoundationExperienceOrbs.h`. Options: (a) delete it (recommended: it's 0 bytes and was never tracked); (b) leave it.
2. **For Jesus:** push `feature/survival-entities`. Options: (a) after the program's landing (recommended); (b) keep it local.
3. **For Jesus:** add `Reference/minecraft-entities-26.3/` to CLAUDE.md's hash-checked list. Options: (a) add it (recommended); (b) leave it out.
4. **For the program:** a value-holder request for the block-loot spawn velocity (the position spread and speed of a broken block's drops). Options: (a) add it to `REQUESTS-SURVIVAL.md` (recommended); (b) keep the estimates.

## Short prompt

> Survival T2 step 19a, armor, after the items tail (lane `C:\dev\projects\Unnamed-ent`, branch `feature/survival-entities`; check `git log` first). Read `Docs/Records/2026-10-14-Items-Handoff-Step19a.md` all of it, then SM-Entities' 19a brief, then the handoff's reading list. First, Codex review 11 #1-#5 (the push-out ping-pong first, then FX-a on top of it); then armor (the item reference extended for the armor pieces, `DA_Items` only with the lock, damage from the worn pieces, the HUD armor row, equip sounds, `carved_pumpkin` on the head, first person only). Stage by path only. Send your plan to SM-Entities (`session_01HHSREoLkWuRSPoQFWbsqfX`) with `send_message` and wait for the go.
