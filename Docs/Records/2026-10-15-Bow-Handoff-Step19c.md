# Handoff: survival step 19c, the bow and arrows (T2)

Lane `feature/survival-entities` (worktree `C:\dev\projects\Unnamed-ent`), after step 19b ([its record](2026-10-15-Shield-19b.md), commit 230284d1). Check `git log` first: SM-Player runs 19b's landing merge (`feature/survival` 5d271969 or newer) before you start; build on that tip. Report to SM-Player (`session_01C87ZqKQ2vSBHdtPkSU7KJn`) with `send_message`; send a plan first and wait for "go build".

**19c's plan is `C:\dev\tools\shift-managers\survival\tracks\T2-plan-19c.md`** (scout P3, written before 19b landed). This handoff only adds what 19b changed since; where they differ, this file wins.

## 1. Reading list

1. This file, all of it.
2. `C:\dev\tools\shift-managers\survival\tracks\T2-plan-19c.md`, all of it.
3. [The 19b record](2026-10-15-Shield-19b.md), all of it; `Docs/Items.md` "The shield (survival step 19b)" (grep `## The shield`) and "No starting items".
4. Code by grep, at 230284d1 or the landing tip: `FFoundationAttack`, `HurtPlayer`, `IsBlocking`, `BlockingUseTicks` (`FoundationAuthority.h/.cpp`); `FoundationHeldPose::Get` (`FoundationItemMesh.h`); `SetHeldItem` and `FindSpecialView` (`FoundationInteractionView.cpp`, `FoundationItemAssets.cpp`); `model_refs`, `blocks_attacks`, `APPEND_LAST` (`Reference/minecraft-items-26.3/tools/build_reference.py`); `C:\dev\sandbox\items-shield\prove_additions.py`.

## 2. What 19b changed against the 19c plan

- **The damage hook, as built:** the plan's `FFoundationDamageSource` is named **`FFoundationAttack`** {`Type` (EFoundationDamage), `Amount`, `SourceCm` (world cm), `DisableBlockingSeconds`}; the entry is **`bool FFoundationAuthority::HurtPlayer(int32 Player, FFoundationVitals& Vitals, const FFoundationAttack& Attack)`**. It blocks only `MobAttack` and `PlayerAttack` today (every `#bypasses_shield` type arrives without a source); an arrow's hit must route through it with a type the shield blocks (26.3's `arrow` damage type is not in `#bypasses_shield`): add an `EFoundationDamage` value (append last; `FoundationVitals.cpp` `GetRule` table and the death-message keys follow the enum order) and add it to `HurtPlayer`'s `bBlockable`. False means nothing hurt the player (a full block).
- **Items reference and DA_Items:** items.json holds **144 items**; the shield is item **143** (its protocol id is 1446: the plan's "protocol 144/145" means the items.json positions 144 and 145 for `bow` and `arrow`), models 150-151, textures unchanged at 154. `APPEND_LAST` ends with `shield`: add `ranged` after it. `model_refs(node, when, special)` already follows `condition` (on `using_item`) for opt-in groups; the bow needs the same opt-in plus `range_dispatch` (the plan's 19c1). A case's `using_item` maps to `FFoundationItemModelCaseAsset::UsingItem`; extend that for `use_duration` thresholds rather than adding a parallel structure. The proof script is `C:\dev\sandbox\items-shield\prove_additions.py` (edit the group check to `ranged`).
- **The DA_Items lock:** PM 4 granted it to SM-Player for 19b and 19c straight on; ask SM-Player to confirm it in the plan. Re-import as 19b did: move `DA_Items`, `T_ItemTextures`, `T_ItemTextures32` to the sandbox, run `ImportItems.py` twice (import, verify), then `CreateItemMaterials.py`.
- **Native pins that follow the item count** (update each from its source when the bow and arrow land): `FoundationItemTests` (144 items), `FoundationCombatTests` (144 rows x2, full-charge 145), `FoundationMiningTests` (144), `HudItemTests` (144 tooltips, durability 32 maxima / 10,672 values), `FoundationRecipeTests` (552 shaped), `HudCreativeTests` (the combat tab: 54, the shield at 21), `FoundationItemMeshTests` (143 layered first-person models; special models skip).
- **The test guard:** every test macro sits inside `#if WITH_DEV_AUTOMATION_TESTS` up to the file's last line. `node Scripts/CheckTestGuards.mjs` checks it and is RunChecks' first step in every profile; run it before every commit. A test named `X.Y.Z` makes `X.Y` a group, so RunChecks counts a test `X.Y` as "declared but not run": give new tests sibling names (19b renamed `Items.Mesh.Shield` to `Items.ShieldMesh`).
- **The pose place:** both hands' held poses live in `FoundationHeldPose::Get(Hand, bFit)` (`FoundationItemMesh.h`: context, pivot, mirror). Jesus (2026-10-15): "Hold bobbing, keep visuals simple": a rigged first-person body will replace held-item poses later. Put the bow's draw pose there, simple (the pulling model switch plus at most one pose entry), with no polish pass. First person only, no hands, nothing on the body.
- **No starting items:** players start empty; tests set `Rules.Loadout` (and probes call `FFoundationAuthority::ResetInventory`) to get items. A bow test lab gives itself the bow and arrows.

## 3. Working agreement

As in 19b's handoff: plan first, then "go build"; never push, never bare `git stash`, no CLAUDE.md edits, stage by path only; DA_Blocks and DA_Sounds are T5's; never re-record goldens or world hashes. Values: the value reports and findings (grep, cite ids), the wiki, black-box runs, never decompiled code. Before a heavy launch outside RunChecks: `Scripts\QuietWindow.ps1 -Name <what>`. Mutations: `C:\dev\sandbox\items-xp\mutate.ps1` (single-line anchors), then rebuild before any suite. Wrap-up: `Scripts\RunChecks.ps1 -Profile final -Flight smoke` after the last code change, a Shipping compile of the game target (`Build.bat Unnamed Win64 Shipping`), Sonnet 5.5 review of the diff only (two reviewers past about 1,500 lines), report A, commit on the go, handoff, report B. A `git checkout --` or `git clean` to set work aside is refused by the auto-mode classifier: plan splits before the work, not after.

## 4. Known issues from 19b

| Id | Issue | When |
|---|---|---|
| S-1 | Nothing calls `HurtPlayer` in play but the lab panel's two front-hit buttons (mob melee still calls `Vitals.Hurt`). | Mobs (T6 20c); arrows (19c) should call it |
| S-2 | The shield's held box (placement, face orientation, the lit `M_Mob` material) and the offhand's mirrored pivot are not looked at in game. | The first-person body (Jesus's tap) |
| S-5 | `IsBlocking` trusts a use not continued this step until `Step` cancels it. | A use timing pass |

Also open: S-3 (shift-click to the offhand slot, banner patterns, offhand blocks not drawn), S-4 (the flat GUI stand-in), 19a2's A-1, A-2, A-6, A-7.

## 5. Tokens and evidence (19b)

- Lead peak 441.3k; subagents: view V1 106.9k, view V2 76.7k, reviewers 110.8k and 104.9k (all under the 150k stop; three over the 100k line: brief smaller).
- Evidence: `C:\dev\projects\Unnamed-ent\Tests\Evidence\2026-10-15\Checks-final-20261015-151649\summary.txt` (GREEN, native 479/479); `C:\dev\sandbox\items-shield\` (prove-4.txt, importitems-1/2-editor.log, shipping-build-final.log, guard-proof\proof.txt, review\); `C:\dev\sandbox\items-xp\logs\mutations-19b-a.txt`, `mutations-19b-b.txt`.

## 6. Open questions

1. **For Jesus:** push `feature/survival-entities`. Options: (a) after the program's landing (recommended); (b) keep it local.
2. **For Jesus:** Unnamed's CLAUDE.md says `UFoundationInteractionSettings` holds "...placement presets and the loadout"; the loadout is gone. Options: (a) drop "and the loadout" (recommended); (b) leave it.
3. **For SM-Player:** the DA_Items lock straight on for 19c, and the entities-reference lock (the plan's 19c1).

## Short prompt

> Survival T2 step 19c, the bow and arrows (lane `C:\dev\projects\Unnamed-ent`, branch `feature/survival-entities`; check `git log` first and build on 19b's landing merge). Read `Docs/Records/2026-10-15-Bow-Handoff-Step19c.md` all of it, then its reading list (19c's plan is `C:\dev\tools\shift-managers\survival\tracks\T2-plan-19c.md`; the handoff's section 2 overrides it where they differ). Plan first (19c1 data under the DA_Items and entities-reference locks you ask for, 19c2 logic through `HurtPlayer`, the draw pose in `FoundationHeldPose::Get`, simple), send it to SM-Player (`session_01C87ZqKQ2vSBHdtPkSU7KJn`) with `send_message`, and wait for "go build". Stage by path only.
