# Handoff: survival step 19b, the shield (T2)

Lane `feature/survival-entities` (worktree `C:\dev\projects\Unnamed-ent`), after step 19a2 ([its record](2026-10-15-Armor-19a2.md), commit 512f0244). Check `git log` first: SM-Player runs the 19a2 landing merge (`feature/survival` 00322d14 or newer) in this worktree before you start, so build on that tip. Report to SM-Player (`session_01C87ZqKQ2vSBHdtPkSU7KJn`) with `send_message`; send a plan first and wait for "go build". Scope (ACTION-PLAN.md, T2 19b): the shield, blocking, first-person held-item motion only.

## 1. Reading list (in order)

1. This file, all of it.
2. [The 19a2 record](2026-10-15-Armor-19a2.md), all of it, and [the 19a1 record](2026-10-15-Armor-19a1.md), section 3 (known issues). `Docs/Items.md`: section "Armor (survival step 19a)" (grep `## Armor`) and the held-item part of "Presentation" (grep `held view`).
3. Values, grep only, cite ids: `C:\dev\projects\value-holder\SURVIVAL-RUN2-FINDINGS-26.3.md` **R18** (line 207 onward: `survival2.r18.shield_release`, `shield_delay`, `held_item_transforms`; their long sections at about lines 40727, 40942 and 41004, read by line range only); `C:\dev\projects\value-holder\MINECRAFT-26.3-VALUES.md` section "Armor / shield" (grep `## Armor / shield`; never Appendix H); `SURVIVAL-FINDINGS-26.3.md` 3634-3637 and 3821 (`item.shield.block`: volume 1, pitch 0.8 + 0.4U, `survival.r5.rules`).
4. `Reference/minecraft-items-26.3/tools/build_reference.py`: the header (`TIER_A`, `APPEND_LAST`), `armor_totals`, `equippable`, `trim_fallback`, `model_entry` (grep), and README.md's "Rebuild" section. The 19a2 proof script: `C:\dev\sandbox\items-armor\prove_additions.py`.
5. Code by grep: `FFoundationAuthority::UseItemStart` and `UseTicks` (`FoundationAuthority.h/.cpp`), `FFoundationVitals::Hurt` (`FoundationVitals.cpp`), `FFoundationItemDefinition::DisableBlockingSeconds` (`FoundationItems.h`), `UFoundationInteractionView::UpdateHeldItem` (`FoundationInteractionView.cpp`), `FoundationItemMesh::BuildMesh`.
6. `C:\dev\tools\shift-managers\survival\PROGRAM.md` sections 4-6 (asset locks, the item reference rule, first person only) and `ACTION-PLAN.md` section 3 (locks).

## 2. The work

1. **First task: A-5, the held leather tint's look.** One windowed capture of a leather piece in hand and in the hotbar (default dye #A06540) beside an iron piece as the control frame, with the sampled colours as numbers. `M_HeldItem` decodes the vertex colour with gamma 2.2 (19a2 review); if the cap reads too bright or too dark, say which and propose the switch. This is a look call: send SM-Player the captures with numbers and the control pair, keep the other option one switch away, and wait.
2. **The shield item.** `shield` is not in DA_Items: adding it extends the item reference (a new `APPEND_LAST` group, through `build_reference.py` only, additions only, proven as 19a2 did) and needs **the PM's DA_Items lock** (ask SM-Player in the plan). Its 26.3 components (`blocks_attacks`, `equippable` offhand, durability 336, repair planks) come from the components report. Its item model is `minecraft:special` (an entity renderer): the builder refuses definitions other than model and select today, so plan how the record carries it (for example the definition kind `special` and the entity texture `entity/shield/shield_base_nopattern` on a stand-in box) and say so in the plan.
3. **Blocking** (R18, the value report's "Armor / shield"): a right click holds the shield up (main hand or offhand) after the raise delay (`survival2.r18.shield_delay`: 5 ticks); while raised, frontal damage the `blocks_attacks` component covers is blocked, the shield wears, and `item.shield.block` plays; an axe hit disables it for `DisableBlockingSeconds` x 20 ticks (the 7 axes' 5 s, already data); release per `shield_release`. Through `FFoundationAuthority` commands only (UseItemStart and the use timing eating uses).
4. **First-person held-item motion only:** the raised shield moves the held item (R18 `held_item_transforms`); no hand or arm, nothing on the body. Third person / F5 untouched.
5. Tests per behaviour with exact numbers from R18; a mutation per new test; `provisional-values.md` rows for any value R18 lacks.

## 3. Working agreement

- Plan first, then "go build". Never push, never bare `git stash`, no CLAUDE.md edits, stage by path only. `DA_Blocks` and `DA_Sounds` are T5's (SM-Blocks'); never edit them. Re-import DA_Items only under the lock, moving the old assets to the sandbox first (19a2's way), with two ImportItems logs (import, then verify).
- Never re-record goldens or world hashes; stop and report if one would move.
- Values: the value reports and findings (grep, cite ids), the wiki, black-box runs; never decompiled code.
- Before a heavy launch outside RunChecks: `Scripts\QuietWindow.ps1 -Name <what>`. Script paths for `-run=pythonscript` must be absolute. Mutations: `C:\dev\sandbox\items-xp\mutate.ps1` (single-line anchors; a multi-line one stops the batch). `Automation RunTests` filters join with `+`, not `;`.
- After a mutation batch the last mutated DLL stays built: rebuild before any suite.
- Wrap-up: `Scripts\RunChecks.ps1 -Profile final -Flight smoke` after the last code change (clang-format your new lines first), a Sonnet 5.5 review of the diff only (two reviewers past about 1,500 lines), report A, commit on the go, handoff, report B. Measure the lead with `node C:\dev\sandbox\items-s8b\measure.mjs --session <transcript id>` (the transcript id is the .jsonl name under `C:\Users\jherediaramirez\.claude\projects\C--dev`, not the remote session id).

## 4. Settled decisions

- First person only, no hands, no armor or shield drawn on the player; held items move only as items.
- Copper armor equips silently until SM-Blocks' second sound batch (A-1).
- Stand-ins: any Java 26.3 texture, each with a `Docs/StandIns.md` row.

## 5. Code map

| What | Where |
|---|---|
| Item reference builder, verifier, proof | `Reference/minecraft-items-26.3/tools/build_reference.py`, `verify_reference.py`; `C:\dev\sandbox\items-armor\prove_additions.py` |
| Import mapping | `Scripts/Editor/ItemImportData.py` (`build`), `ImportItems.py`; `FoundationItemAssets.h/.cpp` (`FFoundationItemAsset`, `MakeDefinition`, `FindModelView`) |
| Use, timing | `FoundationAuthority.h/.cpp` (`UseItemStart`, `UseTicks`, `UseEquip`), `FoundationInteraction.cpp` |
| Hurt, armor | `FoundationVitals.cpp` (`Hurt`, `PendingArmorWear`), `FoundationAuthority.cpp` (`WearArmor`, `GetWornArmor`), `FoundationPawn.cpp` (`DropOnDeath`) |
| Held view | `FoundationInteractionView.cpp` (`UpdateHeldItem`, `GetItemMesh`), `FoundationItemMesh.cpp` |
| HUD | `Hud/HudPresenter.cpp`, `HudDrawList.cpp`, `HudGuiPainter.cpp` |
| Tests | `Tests/FoundationItemTests.cpp`, `FoundationCombatTests.cpp`, `HudArmorViewTests.cpp`, `FoundationExperienceOrbTests.cpp`, `FoundationDeathTests.cpp` |

## 6. Known issues

From 19a2 (record section 3): A-1 copper equip silent (SM-Blocks' second sound batch); A-2 turtle water breathing, enchantments, knockback resistance (status effects, enchantments); A-5 the held tint's look (19b, first task); A-6 the reward's 1-in-40 group modelled per pair (a black-box trace); A-7 dropped and breaking leather pieces draw layer 0 untinted (when dropped armor's look matters). From before: S8a-a/XP-b, FX-b, XP-e, SW-b.

## 7. What worked and what failed (19a2)

- **Worked:** the additions-only proof script against the moved-away folder (fast and exact; treat key-sorted lists such as the durability maxima as keyed); a Sonnet subagent on the view work while the lead did the data and orb rules; mutation batches after the code settled (a survivor showed a missing test); a patch file plus copies to set work aside for a mid-step fast-forward.
- **Failed:** a multi-line mutation anchor stopped a batch; `Automation RunTests A;B` ran only A; a relative `-script=` path resolved under the engine folder; the views subagent's brief was too large (210.9k, past its stop): split view work into two briefs.

## 8. Tokens and evidence

- **19a2:** lead peak 371.8k (measure.mjs); subagents: views 210.9k (over), pins 75.5k, reviewers 88.0k and 98.8k.
- **Evidence:** `C:\dev\projects\Unnamed-ent\Tests\Evidence\2026-10-15\Checks-final-20261015-122136\summary.txt` (GREEN, native 468/468); `C:\dev\sandbox\items-armor\` (`prove-2.txt`, `importitems-1-editor.log`, `importitems-2-editor.log`, `createitemmaterials-2-editor.log`, `reference-files.txt`, `name-status.txt`, `review\19a2-A.diff`, `review\19a2-B.diff`); `C:\dev\sandbox\items-xp\logs\mutations-19a2.txt`, `mutations-19a2b.txt`.

## 9. Open questions

1. **For Jesus:** push `feature/survival-entities`. Options: (a) after the program's landing (recommended); (b) keep it local.
2. **For SM-Player / the PM:** the DA_Items lock for the shield item (task 2). Options: (a) grant it for 19b (recommended: the shield is one appended item); (b) a project stand-in item in `UFoundationInteractionSettings` until a later batch.
3. **For Jesus (through the look call):** the held leather tint after the A-5 capture, if it differs from vanilla.

## Short prompt

> Survival T2 step 19b, the shield (lane `C:\dev\projects\Unnamed-ent`, branch `feature/survival-entities`; check `git log` first and build on the 19a2 landing merge). Read `Docs/Records/2026-10-15-Shield-Handoff-Step19b.md` all of it, then its reading list. First task: A-5, the held leather tint's look call. Plan first (A-5, the shield item through the item reference under the DA_Items lock you ask for, blocking per R18, first-person held-item motion only), send it to SM-Player (`session_01C87ZqKQ2vSBHdtPkSU7KJn`) with `send_message`, and wait for "go build". Stage by path only.
