# Handoff: survival step 19a2, the armor pieces' data (T2)

Lane `feature/survival-entities` (worktree `C:\dev\projects\Unnamed-ent`), after step 19a1 ([its record](2026-10-15-Armor-19a1.md)). Check `git log` first. **The PM granted the `DA_Items` lock (DA_Items, `/Game/Items` and the item reference) to T2 19a2;** it returns when 19a2 lands. Report to SM-Player (`session_01C87ZqKQ2vSBHdtPkSU7KJn`) with `send_message`; send a plan first and wait for "go build".

## 1. Reading list (in order)

1. This file, all of it.
2. [The 19a1 record](2026-10-15-Armor-19a1.md), all of it (short), and `Docs/Items.md`, section "Armor (survival step 19a)" (grep `## Armor`).
3. `Reference/minecraft-items-26.3/tools/build_reference.py`: the header (lines 1-120: `TIER_A`, `APPEND_LAST`, `append_rank`), `attack_totals` (grep `def attack_totals`), and the item loop that builds each record (grep `items.append({`, about 40 lines before and after). Never read it whole.
4. `Scripts/Editor/ImportItems.py`, lines 195-235 (the item structs), and `Source/Unnamed/Foundation/FoundationItemAssets.h`, `FFoundationItemAsset` (grep `struct FFoundationItemAsset`) and `FoundationItemAssets.cpp`'s `MakeDefinitions` (grep it).
5. `C:\dev\projects\Unnamed\CLAUDE.md`'s rule on the item reference (in this lane: `CLAUDE.md`, the hash-checked line): regenerate only through its tool, after moving the accepted folder away.
6. Values, by grep only: `C:\dev\projects\value-holder\SURVIVAL-RUN2-FINDINGS-26.3.md` R19 (line 283, `survival2.r19.*`) and R23 "XP orbs" (grep `survival2.r23.xp_rules`); `SURVIVAL-FINDINGS-26.3.md` 3616-3627 (the equip sounds).
7. `C:\dev\tools\shift-managers\survival\PROGRAM.md` sections 4-6 (asset locks, the item reference rule, first person only) and `ACTION-PLAN.md` section 3 (the lock).

## 2. The work

1. **Base.** Merge the then-current `feature/survival` once (never rebase). The reference's base is dcd2bcee or newer: 114 items, T1 8a2's ten at indexes 104-113 (charcoal ... copper_nugget). `gold_nugget` is already there (index 82, group `drop`); add nothing for it.
2. **Extend the item reference** with `build_reference.py` only: a new group `armor` appended last in `APPEND_LAST` (after `drop`, `mob_drop` and whatever 8a2 named), 29 items: {leather, chainmail, iron, golden, diamond, netherite, copper} x {helmet, chestplate, leggings, boots} + `turtle_helmet` (indexes 114-142 on dcd2bcee). Not elytra, wolf, horse or nautilus armor, no trimmed variants. Builder changes:
   - `attack_totals` exits on any modifier but the two attack ones: skip it for the `armor` group (attack stays None) and sum the `armor`, `armor_toughness` and `knockback_resistance` modifiers into a new `"armor"` field.
   - A new `"equippable"` field (slot, equip_sound, asset_id, swappable; defaults: swappable true, equip_sound `item.armor.equip_generic`).
   - **Emit both fields only on records that have the component,** so every existing record stays byte-identical.
   - Every armor item definition is a `minecraft:select` on the trim material: keep its fallback (untrimmed) model only. Leather's layer 0 has a `minecraft:dye` tint, default -6265536: keep it per layer (the builder refuses tints today).
   - The lang-key check covers the armor slot headers (`item.modifiers.head` etc.) and the attribute names.
   - Re-run `tools/ItemFacts.java` (the facts dumper) so `facts/tooltips.json` and `durability_bar.json` gain only the new entries.
3. **Prove additions only** with a script: every old list is a prefix of the new one (items, models, textures, tags as applicable), only the manifest counts and meta rehash, existing per-item files byte-identical, SHA256SUMS lines kept identical (sorted lines may move). If the tool's ordering would shift any index, stop and report. `verify_reference.py` must pass. Report A lists the new and changed reference files with sizes.
4. **DA_Items:** `FFoundationItemAsset` gains the slot, swappable, equip sound, armor, toughness, knockback resistance and a per-layer tint; `MakeDefinitions` fills 19a1's `FFoundationItemDefinition` fields (`Equipment`, `bSwappable`, `EquipSound`, `Armor`, `ArmorToughness`). Re-import with `ImportItems.py` on top of the merged `DA_Items` (never hand-merge); put its verify output in report A. Sprites come from the reference (the 26.3 jar), so no stand-ins unless one is substituted.
5. **Leather tint:** the HUD sprite and the held view multiply a tinted layer by its colour (leather only).
6. **Pumpkin blur** (approved): `carved_pumpkin`'s `camera_overlay` `misc/pumpkinblur` drawn full screen while it is worn on the head (first person, a HUD overlay, not body armor). Texture from the 26.3 jar as a stand-in: a `Docs/StandIns.md` row (asset, jar path, where used, step).
7. **Last, the R23 orb values not yet applied** (from 19a1; `survival2.r23.xp_rules`): the bounce (x 0.4 when the pre-move vertical speed is below -gravity; today 0.5 above 6 cm, `BounceFactor`, `BounceMinCm`), the merge scan at tickCount % 20 == 1 (today age % 20 == 0; the tests' "merged on step 20" will move), and newly awarded XP's 1 x 1 x 1 search box (today the merge box grown 0.5). Update the rows in `provisional-values.md`.

## 3. Working agreement

- Plan first, then "go build". Never push, never bare `git stash`, no CLAUDE.md edits, stage by path only. `DA_Blocks` and `DA_Sounds` are T5's (SM-Blocks'); never edit them.
- Never re-record goldens or world hashes; stop and report if one would move.
- Values: the value reports and survival findings (grep, cite ids), the wiki, black-box runs; never decompiled code. Unknown values: a row in `provisional-values.md`.
- Before a heavy launch outside RunChecks: `Scripts\QuietWindow.ps1 -Name <what>`. Mutations: `C:\dev\sandbox\items-xp\mutate.ps1` (a fresh `-Name`; `if (false && ...)` forms; anchors must be unique).
- Wrap-up: `Scripts\RunChecks.ps1 -Profile final -Flight smoke` after the last code change, a Sonnet 5.5 review of the diff only, report A, commit on the go, handoff, report B. Measure the lead with `node C:\dev\sandbox\items-s8b\measure.mjs --session <session id>` (it prints the peak).
- PowerShell: `Remove-Item` on a line that also names `C:\Program Files` is refused by the tool's guard; run it alone. Files written with `[IO.File]` need absolute paths; the C++ sources use LF line endings.

## 4. Settled decisions

- First person only, no hands, no armor drawn on the player.
- Copper pieces equip silently until SM-Blocks' second sound batch adds `item.armor.equip_copper` (the call is already made).
- Turtle water breathing deferred (status effects). 19a1's push-out and orb merge follow R23, its creative equip R19.

## 5. Code map

| What | Where |
|---|---|
| Item fields | `FoundationItems.h`: `FFoundationItemDefinition` (`Equipment`, `bSwappable`, `EquipSound`, `Armor`, `ArmorToughness`) |
| carved_pumpkin | `FoundationInteractionSettings.cpp`: `AddBlockItems` |
| Worn, wear, equip, events | `FoundationAuthority.cpp`: `GetWornArmor`, `WearArmor`, `UseEquip`, `AddEquipEvents` (grep), `Execute`'s `ArmorBefore` |
| Vitals | `FoundationVitals.h/.cpp`: `WornArmor`, `WornToughness`, `PendingArmorWear`, `GetArmor`, `Hurt` |
| Pawn hand-off | `FoundationPawn.cpp`: `StepInteraction` |
| Sound, HUD | `FoundationInteractionView.cpp` (`Equipped`), `Hud/HudPresenter.cpp` (`ApplyVitals`) |
| Tests | `Tests/FoundationInventoryTests.cpp` (`.Armor`), `FoundationVitalsTests.cpp` (`.Armor`), `HudTests.cpp`, `FoundationItemEntityTests.cpp` (`.PushOut`), `FoundationExperienceOrbTests.cpp` |

## 6. Known issues

See the 19a1 record, section 3 (A-1 to A-4, SW-b). From before: S8a-a/XP-b (drops and orbs sink through water), FX-b (bottom-centred bodies), XP-e (the entities reference's verifier without `textures.json`).

## 7. What worked and what failed (19a1)

- **Worked:** mutation batches in sequence between edits (27 mutations, all caught after fixes); exact-number tests from a measured trace (R23's "center" ticks 1-2); reading a new value source as soon as it arrived instead of shipping the provisional fix.
- **Failed:** a first Codex 11 #1 fix (union exit plus a kept heading): M1 and M3 survived (each alone was enough), then R23 replaced it anyway. Expecting a drop to settle on a floor under the vanilla rule: it sinks and jitters, as vanilla's traces show.

## 8. Tokens and evidence

- **19a1:** lead peak 365k at the checkpoint (measure.mjs); one Sonnet reviewer, about 75k peak by its estimate (124k processed).
- **Evidence:** RunChecks final summary (see report B); `C:\dev\sandbox\items-xp\logs\` (`19a1-t1` to `19a1-t7`, `mutations-19a1*.txt`, `19a1-m15` to `m17`); `C:\dev\sandbox\items-xp\review\19a1.diff`.

## 9. Open questions

1. **For Jesus:** push `feature/survival-entities`. Options: (a) after the program's landing (recommended); (b) keep it local.
2. **For Jesus:** delete the empty untracked `Source/Unnamed/Tests/FoundationExperienceOrbs.h` if it reappears (it was not present at 19a1).

## Short prompt

> Survival T2 step 19a2, the armor pieces' data (lane `C:\dev\projects\Unnamed-ent`, branch `feature/survival-entities`; check `git log` first). Read `Docs/Records/2026-10-15-Armor-Handoff-Step19a2.md` all of it, then its reading list. The PM's DA_Items lock is held for this step. Plan first (the reference extension, the additions-only proof, DA_Items, the leather tint, the pumpkin blur, then the three R23 orb values), send it to SM-Player (`session_01C87ZqKQ2vSBHdtPkSU7KJn`) with `send_message`, and wait for "go build". Stage by path only.
