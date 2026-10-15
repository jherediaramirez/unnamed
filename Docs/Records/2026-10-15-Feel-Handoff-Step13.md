# Feel lane handoff: step 11 done (death screen, hurt and fire feedback), step 13 next (view bobbing), 2026-10-15

Lane `feature/survival-feel`, worktree `C:\dev\projects\Unnamed-feel`, survival track T4, sub-manager "SM-Player" (run 2). Step 11 landed in two commits:
- **11a:** death screen, drops, respawn point, death messages, Codex 3 fixes. Commit `1b288375`, landing merge `fc7b0517`.
- **11b:** hurt and death camera, sounds, fire overlay, chat R4. The commit hash is in report B.

The HUD side is recorded in [2026-10-07-Hud.md](2026-10-07-Hud.md), sections "Death (step 11)" and "Hurt, fire and chat feel (step 11b)". The commands side is in [Docs/Commands.md](../Commands.md). Next on this lane: **13, view bobbing** (`movement.view_bob`, `movement.bob`), first person only, no hands: the held item moves only as an item.

## Reading list (in order, line ranges only)

1. `C:\dev\tools\shift-managers\survival\PROGRAM.md` (rules) and `ACTION-PLAN.md`: the T4 row, section 3 (locks).
2. `C:\dev\tools\shift-managers\survival\tracks\T4-feel.md`, section "Where it stands".
3. `C:\dev\tools\shift-managers\survival\value-swaps.md`: grep `view_bob` and `movement.bob` (section 1, the T4 table, ~VS:97-110).
4. `C:\dev\sandbox\scout-player\plan.md`, row 13; `facts.md`, grep "bob".
5. `Docs/Records/2026-10-12-Feel-B.md`: the VP2 prompt and G6/G7, the held-item sway work done before.
6. Code:
   - `Source/Unnamed/Foundation/FoundationPawn.cpp`, the per-frame camera block before `Camera->SetWorldLocationAndRotation`. Step 11b's roll is added there behind `FoundationDeath::IsScreenEnabled() && !IsSpringArmView() && Perspective == 0`; bobbing goes beside it.
   - `Foundation/FoundationDeath.h` `FoundationHurt`: the pure-function pattern to copy for the bob curve.
   - The held-item view: grep `HeldItem` in `FoundationInteractionView.cpp`.

## Working agreement (from the program)

- **Plan first:** report a plan to SM-Player (every file against the hot files) and wait for "go build". One switch per step.
- **Build slot:** the line has one build slot. SM-Player may hold you until another step lands. "Held" means no edits, builds or commits.
- **Merges:** merge feature/survival only at the merge points SM-Player names; never mid-step, never rebase or force. On the token: merge, commit, rebuild, then `Scripts\RunChecks.ps1 -Profile merge`, then one combined report.
- **Checks:** `Scripts\RunChecks.ps1 -Profile quick|final|merge` (add `-Flight smoke` on final). Put the summary path in every report.
- **Reviews:** a Sonnet 5.5 pre-commit review on the diff. Over about 1,500 lines, split it across two reviewers by path.
- **Subagents:** Sonnet subagents unless a task really needs Opus (SM-Player, 2026-10-15). 11a's four Opus edit agents were most of its cost.
- **Look calls** go to SM-Player: captures, a control pair and a recommendation, with the other option one switch away.
- **Never:** push, edit CLAUDE.md, or re-record goldens or hashes. Never use bare `git stash`.

## Settled decisions (step 11)

- **Switch `foundation.Death.Screen`** (1 by default). 0 means today's auto-respawn after 20 ticks: no screen, drops, orbs, roll, tilt, new sounds or fire overlay. Death messages in chat and the chat R4 fixes are not behind it.
- **No full-screen red** (the manager's look call): `0xB2FF0000` [survival.r6.hud_rules, hurt.overlay] is vanilla's entity damage overlay, for mobs (20a/b). `foundation.Hurt.ScreenTint` 1 is the full-screen stand-in; it is off by default.
- **Fire overlay:** `/Game/Hud/Textures/block/fire_1`, already imported from `Reference/minecraft-hud-26.3`, so no StandIns row. Placement is provisional, and a still is on `vanilla-captures.md` (T4, "Burning in first person").
- **Death XP:** real orbs through `SpawnExperience`. XP resets on respawn.
- **Hardcore** "Game Over!" variant: a known issue.

## Code map (step 11)

- **`Foundation/FoundationDeath.h`:**
  - `FoundationDeath`: `IsScreenEnabled`, `AreButtonsEnabled(DeathTicks)` (true at 20 or more), drop constants and `GetExperienceDrop`.
  - `FoundationHurt`: `GetHurtTiltDegrees`, `GetDeathRollDegrees`, `GetHurtTime`.
- **The interface for 20c and B7:**
  - `FFoundationAuthority::SetRespawnPoint` / `ClearRespawnPoint` / `GetRespawnPoint`, session only and not in world.json. `FFoundationPlayer::RespawnCell`.
  - `FFoundationAuthority::DropInventoryOnDeath`: 41 slots, carried, the 2x2 craft grid and an open table's grid. One entity per stack, pickup delay 40, vy 20 cm/step, scatter of 50 cm/step or less. Nothing drops with infinite supply or with item entities off.
  - `RemoveItemEntities` / `RemoveExperienceOrbs` (used by `/kill @e`).
- **`AFoundationPawn`:**
  - `IsDeathScreenActive`, `GetDeathTicks`, `RequestRespawn`, `GetDeathMessage`, `GetDeathScore`.
  - `HandleDeath`, once per death through `HandledDeaths`. `Respawn` calls it first. It does the chat message, drops and orbs.
  - `Respawn` uses the respawn point.
- **`FFoundationVitals`:** each death cause's en_us key and template (`GetDeathText`, `FormatDeathMessage`, "Player"). A fall over 5 blocks uses `death.fell.accident.generic`. Mob and player deaths fall back to generic until 20c passes an attacker name. `DeathKey` and `DeathScore` are kept across respawn.
- **`AFoundationController`:**
  - `UpdateDeathScreen` / `HandleDeathMouse`: a full-viewport `SBorder` with UI-only input. Death closes an open panel. Escape does nothing.
  - `AddChatText`; `IsGameplay` is false while dead.
- **HUD:**
  - `HudDrawList`: `DeathScreen`, `GetDeathButton` / `HitDeathButton`, the fire overlay and tint drawn first, the chat caret.
  - `FHudSnapshot`: a hand-written `operator==` via `GetFields`, so add every new member there. Its fields include the death fields, `bDamageTint`, `bOnFire`, `FireFrame`, `CaretIndex` (-1 = end), `bCaretVisible` and `bHideHud`.
  - `UHudPresenter`: `ChatOpenedAt`, and `bVisible` stays on for fire and tint under F1.
- **Sounds** (`FoundationInteractionView.cpp`, next to step 16's fire sounds): `entity.player.hurt` on a hit, `entity.player.death` on the killing hit only.
- **Chat:**
  - `FoundationChat` `GetAlpha`: 180 ticks, then the quadratic fade.
  - `UFoundationPanels::OpenText`: a 256-character cap via `OnTextChanged`, a transparent style, and `GetTypedCaret`.
- **Codex 3:**
  - `FFoundationCommandContext::bCommands` comes from the saved world.
  - `ReadTime` takes a minimum; `ReadTarget(bSingle)`; the pause/resume errors.
- **Tests:**
  - `Unnamed.Foundation.Death.{Messages,Drops,RespawnPoint,Xp,Screen}`
  - `Unnamed.Foundation.Hurt.{Tilt,DeathRoll}`, `Unnamed.Foundation.Chat.Fade`
  - `Unnamed.Hud.Feedback.{Tint,Fire,Caret}`
  - `Unnamed.Foundation.Commands.CheatsOff`, plus additions to Kill, Time, Parse and Chat
  - Runtime cases in `FoundationDeathProbe.cpp`, in `-Suite interaction`: death 1/0, hurt 1/0, fire 1/0, chatbar 1/0, and "feel: restore".

## Known issues (with when)

- **The 1 px blue line at the chat bar:** a brush-free style is in, but the line didn't show in either switch's capture, so the fix is unconfirmed. **When:** the next windowed look, by eye in a real window.
- **Provisional values** (rows in `provisional-values.md`, tracks 11a and 11b): drop height (eye minus 0.3), the 5-block fall edge, orb spread, craft-grid drops, the button label at y+6, the score yellow, the tilt partial tick, hurtDir 0, the fire overlay's placement and alpha, the caret's blink origin, and the fade quantization. **When:** Jesus's vanilla capture session (`vanilla-captures.md`, T4).
- **Fire overlay mirroring** relies on an inverted UV region; the capture shows it mirrored. **When:** if a renderer change breaks it.
- **No sound play-count probe** (hurt vs death vs hurt_on_fire). **When:** step 13 or a later test pass.
- **Mob death messages** need the attacker's name. **When:** 20c.
- **Hardcore** death screen. **When:** hardcore worlds.
- **Saving:** the respawn point isn't saved. **When:** player data is saved.
- **Native warnings** rose from 2 to 103 with the 20a merge (not from this lane). **When:** report it if it matters to SM-Player.
- **Pre-existing:** the intermittent ParallelFor allocator crash (see the step-11 handoff). It didn't recur this session.

## What worked and what failed

- **Worked:**
  - Parallel edit-only subagents with disjoint file lists, while the lead builds.
  - One shared rect function for both drawing and hit-testing the death buttons.
  - Real Slate mouse events in the probe.
  - Putting each capture in its own case after the setup case (a screenshot shows the frame before the request).
- **Failed first:**
  - C2487 again: `static constexpr double A = 1, B = 2;` in an exported class. Declare them on separate lines.
  - C4458: a parameter named `Seed` hid `FFoundationAuthority::Seed`.
  - Range-for over a braced `TArray<TPair>` didn't compile; use plain arrays.
  - The probe's old respawn cases waited for the auto-respawn, which switch 1 removes.
  - `/gamemode survival` doesn't clear the lab's infinite supply, so set the vitals and options directly in probes.
  - PowerShell `Set-Content` writes CRLF into LF files. Check line endings before staging, and use the Edit tool.
  - `ImportContainerGui.py` needs an absolute `-script=` path.

## Token figures

- **Lead:** 263k peak before the 11b final run. The final figure is in report B.
- **Subagents:**
  - Opus 5.5 (11a): 142k, 139k, 196k and 129k.
  - Sonnet 5.5: reads 87k and 65k; reviewers 107k, 104k and 98k; 11b edits 98k and 93k; 11b probe 102k.

## Evidence (best first)

- **11b final checks:** the summary path is in report B (`Tests/Evidence/2026-10-15/Checks-final-*` after 06:50).
- **11a landing merge:** `Tests/Evidence/2026-10-15/Checks-merge-20261015-055122/summary.txt`, GREEN:
  - native 421/421
  - hashes 9/9
  - runtime all 178/0, interaction 133/0, tour 35/0
  - create-world 38/0
- **11a final:** `Tests/Evidence/2026-10-15/Checks-final-20261015-050622/summary.txt`.
- **Captures:**
  - `Tests/Evidence/2026-10-15/Playground-interaction/Runtime-20261015-045455/`: `runtime-60-death-1-screen.png`, `-death-1-enabled.png`, `-death-0.png`.
  - `.../Runtime-20261015-063802/`: `runtime-60-hurt-1.png` (taken with the tint on, before the look call), `-hurt-0.png`, `-fire-1.png`, `-fire-0.png`, `-chatbar-1.png`, `-chatbar-0.png`.

## Open questions (for Jesus, with options)

1. **Push `feature/survival-feel`?**
   - (a) With the program's final merge.
   - (b) Keep it local.

   Recommendation: (a).
2. **The full-screen red tint:**
   - (a) Off (vanilla; the manager's call).
   - (b) On as a stand-in.

   Recommendation: (a).
3. **The player's name:**
   - (a) "Player".
   - (b) The profile name once it exists.

   Recommendation: (a) until profiles exist.

## Short prompt for the next chat (step 13)

> Survival program, track T4, step 13: view bobbing (first person only, no hands; the held item moves only as an item). Lane C:\dev\projects\Unnamed-feel, branch feature/survival-feel. Read C:\dev\projects\Unnamed-feel\Docs\Records\2026-10-15-Feel-Handoff-Step13.md first (reading list, working agreement, code map), then report a plan to the sub-manager SM-Player and wait for "go build". One switch for the step; Sonnet subagents.
