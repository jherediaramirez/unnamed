# Feel lane handoff: step 15 done, step 11 next (death screen and hurt feedback), 2026-10-14

Lane `feature/survival-feel`, worktree `C:\dev\projects\Unnamed-feel`, survival track T4. Sub-manager: the "SM-Player" chat. Step 15 (chat commands) is recorded in [2026-10-14-Commands.md](2026-10-14-Commands.md) and [Docs/Commands.md](../Commands.md). Next on this lane: **11, death screen and hurt feedback**, then 13, view bobbing.

## Reading list (in order, line ranges only)

1. `C:\dev\tools\shift-managers\survival\PROGRAM.md`: the program rules (switch per step, reports A/B, quiet windows, the CEF lock).
2. `C:\dev\sandbox\scout-player\plan.md`:
   - lines 1-25 ("Every step");
   - table row 11 (~line 223);
   - "Probes worth one short vanilla run" (~240-250: the death-screen frame).
3. `C:\dev\sandbox\scout-player\facts.md`:
   - "Health, hunger and death" (~64-80);
   - the Feedback table (~156-170);
   - the "Death" bullet (~285).
4. Code:
   - `Source/Unnamed/Foundation/FoundationVitals.h` 50-110 (`DeathTicks`, `RespawnTicks` 20, `ShouldRespawn`, `DeathMessage`, `Respawn`);
   - `FoundationPawn.cpp` `Respawn` (~310) and `StepVitals` (~320-335: the auto-respawn);
   - `FoundationHud.cpp` `NoteVitals` (~160-175: the death and respawn log lines);
   - `Hud/HudDrawList.cpp` (the heart blink comes from `FHudPresentation`; see `Hud/HudModel.h` `FHudPresentation`).
5. `Docs/Commands.md`: the chat now exists. Vanilla prints the death message ("Player was killed", `death.attack.*` in `Reference/minecraft-menu/assets/minecraft/lang/en_us.json`) to chat. Step 15 left that to step 11: `AFoundationController::Chat` (`FFoundationChat::AddText`) is the place, from `UFoundationHud::NoteVitals` or the controller.
6. `Docs/Records/2026-10-07-Hud.md`: the HUD's font, shadows and sprites.

## Working agreement (from the program)

- **Plan and switch:** report a plan to the sub-manager (every file against the hot files) and wait for the go. One switch per step: plan.md names `foundation.Death.Screen` (0 = auto-respawn).
- **Code placement:** gameplay goes through the authority or the vitals, never the HUD. T3 (`Unnamed-use`) owned FoundationAuthority, FoundationItems, FoundationSimulation and the held-item view during step 10. Ask the manager who holds them now.
- **Checks:** `Scripts\RunChecks.ps1 -Profile quick|final|merge` (add `-Flight smoke` on final). Put the summary.txt path in each report.
- **Reviews:** a Sonnet 5.5 pre-commit review on the diff only. Over 1,500 diff lines, split it across two reviewers by path.
- **Stop conditions:** never re-record goldens or world hashes; stop and report if your work would. Never push. No CLAUDE.md edits.
- **Context budget:** measure lead context with `node C:\dev\sandbox\MeasureAgentUsage.mjs --session <id>`, run from `C:\dev`. The project copy under `Scripts/` looks in the wrong transcript folder (`C--dev-projects-Unnamed`) for chats started in `C:\dev`. The copy at `C:\dev\sandbox\` resolves to `C--dev`. Find your session id as the newest `.jsonl` under `~\.claude\projects\C--dev` that mentions your lane.

## Settled decisions (step 15, accepted by the manager)

- **Vanilla first:**
  - `/time` uses 26.3's clock replies;
  - Adventure and Spectator are refused;
  - the stand-in name is "Player";
  - `/tp` uses `%f` positions and +0.5 on whole x and z;
  - errors use vanilla's two lines;
  - the layout follows the wiki and is provisional;
  - death messages go to step 11.
- **Chat look (manager's call):** vanilla's input bar at the bottom (wiki values, provisional), no hint text; switch 0 keeps the old centred fixture as the control.

## Code map (step 15)

- `FoundationCommands.h/.cpp`:
  - `FoundationCommands::Run(line, FFoundationCommandContext&)` returns the chat lines (runs of coloured text);
  - `Format(key, args)` fills a vanilla string; `GetLangStrings()` lists them.
- `FoundationChat.h/.cpp`:
  - `FFoundationChat`: 100 messages, `GetVisible(now, open)`, `GetAlpha`, `GetRevision`, `HasRecent`;
  - the switch `foundation.Chat.Commands` (1);
  - colours `White`/`Red`/`Gray`.
- `AFoundationController`: `SubmitChat`, `GetChat`, `ClearChat`; F3+D clears the chat.
- `UFoundationPanels`:
  - `OpenText` (switch 1: an invisible edit box, `Show(..., bBare)`; switch 0: the old fixture);
  - `GetTypedText`.
- `AFoundationPawn::TeleportFeet(cm)`: /tp. It keeps flight and the pose, and resets motion and fall.
- HUD:
  - `FHudChatRun`/`FHudChatLine`, `FHudSnapshot::Chat`/`bChatOpen`/`ChatInput`;
  - `HudDrawList::WrapChat` and the chat block (history band, input bar, the PREVIEW labels hidden under the bar);
  - `UHudPresenter::Refresh`: lines rebuilt only on revision, open/close, typed-text change or the 1/20 s fade step;
  - the HUD stays visible under the open chat.
- Tests:
  - `Unnamed.Foundation.Commands.{Lang,Parse,GameMode,Give,Teleport,Time,Kill,Chat}`;
  - the runtime cases `chat 1:*`/`chat 0:*` in `-Suite interaction` (Playground, no clock) and `-Suite views` (generated world, clock), from `FoundationCommandsProbe.cpp`.

## Known issues (with when to deal with them)

- **The intermittent ParallelFor allocator crash** in `VoxelWorldgen::ReadFeatures`: one native attempt on 2026-10-14 (`Tests/Evidence/2026-10-14/Native-20261014-125106/Crashes`). Also one `Unnamed.Voxel.Pipeline.ShiftPlan` failure after a 51 s stall, while another editor's automation controller sent our worker FindWorkers messages (`Native-20261014-125221`). The two following full native runs were clean (338/338, `Native-20261014-125603`, `Native-20261014-131301`). **When:** report it if it recurs; the project's crash-hunt notes put the base rate at about 1 in 9.
- **The chat's provisional values:**
  - the fade curve;
  - the 40 px history offset;
  - the input bar;
  - E0E0E0 typed text;
  - a caret that is always at the end and doesn't blink;
  - a faint 1 px blue line at the screen's bottom edge, likely the hidden edit box's focus outline (the box is at render opacity 0). Try a style without the focus brush.

  **When:** the next vanilla capture run (Jesus's go), with the death-screen frame.
- **Death messages in chat.** **When:** step 11.
- **Not supported:**
  - Adventure and Spectator: wait for those modes;
  - selector options;
  - `^` coordinates;
  - `/time rate`;
  - `/tp` rotations;
  - rarity colours in `[Item]`.

  **When:** a later commands step, if wanted.
- **`/gamemode` resets the lab's supply, wear and creative-inventory options** to the mode's (as a saved world's mode does). The mode isn't saved to world.json. **When:** when player data is saved.

## What worked and what failed

- **Worked:**
  - reusing `FVoxelWorldClock::RunCommand` (26.3 replies already verified);
  - pinning every string against `en_us.json` in a test;
  - drawing the input bar in the HUD rather than styling Slate (exact GUI pixels, vanilla font).
- **Failed first:**
  - UE's `FString ==` is case-insensitive (`/GameMode` ran). Use `Equals(..., ESearchCase::CaseSensitive)` for anything vanilla treats as case-sensitive, and the same for `RemoveFromStart`;
  - `FString::IsNumeric` accepts a bare `-` or `.`;
  - a `static constexpr double A = 1, B = 2;` member pair in a `UNNAMED_API` class fails with C2487 (declare them on separate lines);
  - a pawn method named `TeleportTo` hides `AActor::TeleportTo` (C4263/C4264), so it is `TeleportFeet`;
  - the probe's clock check must sample right after the command: the clock keeps running under the chat.

## Token figures

- **Lead:** peak 266k at the first review; a final figure goes in report B.
- **Subagents** (Sonnet 5.5 reviewers):
  - commands diff: 89k;
  - chat, HUD and controller diff: 74k;
  - input bar: 57k.

## Evidence (best first)

- **Final checks:** `Tests/Evidence/2026-10-14/Checks-final-20261014-131726/summary.txt`, GREEN in 23 min:
  - native 338/338;
  - voxel and hashes 9/9 equal;
  - runtime all 139/0, interaction 94/0, tour 27/0;
  - create-world 38/0;
  - the flight smoke passed;
  - npm, sourcecheck and the verifiers passed.
- **Views suite on the final code:** `Tests/Evidence/2026-10-14/Generated-views/Runtime-feel-cmd-134029/`, 70 checks, 0 failures:
  - `runtime-60-chat-1-open.png`: night, the feedback lines and vanilla's input bar;
  - `-chat-1-closed.png`;
  - `-chat-0-open.png`: the switch-0 control.
- **Interaction suite on the final code:** `Tests/Evidence/2026-10-14/Playground-interaction/Runtime-20261014-132616/`, 94/0. Its `runtime-60-chat-1-open.png` shows the input bar over the Playground, with the no-clock line in red; `-chat-0-open.png` is the old fixture.
- **Before the input bar:**
  - `Playground-interaction/Runtime-feel-cmd-123601/`, 94/0;
  - `Generated-views/Runtime-feel-cmd-124224/`, 70/0.

## Open questions (for Jesus, with options)

1. **Push `feature/survival-feel`?** The manager can't approve pushes. (a) Push after the final merge. (b) Keep it local. Recommendation: (a), with the program's final merge.
2. **The player's name:**
   - (a) keep "Player";
   - (b) read the launcher or profile name once one exists;
   - (c) a setting.

   Recommendation: (a) until profiles exist.
3. **Adventure and Spectator:** (a) keep refusing; (b) model them (a step of their own). Recommendation: (a).

## Short prompt for the next chat (step 11)

> Survival program, track T4, step 11: death screen and hurt feedback. Lane C:\dev\projects\Unnamed-feel, branch feature/survival-feel. Read C:\dev\projects\Unnamed-feel\Docs\Records\2026-10-14-Feel-Handoff-Step11.md first (reading list, working agreement, code map), then report a plan to the sub-manager SM-Player and wait for the go. The switch is foundation.Death.Screen (0 = today's auto-respawn). Include the death message in the chat (FFoundationChat, step 15).
