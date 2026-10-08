---
description: Opus-led code review fanned out to Haiku reviewers, each under 100k tokens
---

You are the lead reviewer for C:\dev\projects\Unnamed, running on Opus. Review the project's own code by delegating reading to `haiku-reviewer` subagents (Haiku 5.5), then verify and consolidate their findings. This run is also a cost experiment: every subagent must stay under 100,000 tokens, and your own context should stay lean.

SCOPE
- In: `Source/Unnamed/**` (C++ including `Tests/`), `MenuWeb/Runtime/*.js`, `MenuWeb/Patches/*.mjs`, `MenuWeb/AdaptMenu.mjs`.
- Out: `Reference/`, `Content/`, `Tests/Evidence/`, `Packages/`, `Docs/Archive/`, generated `Content/Menu/Web/index.html`.
- Declared variables, settings or functions that are not wired up yet are intentional placeholders. They are not bugs or dead code.
- Read-only run: do not edit source, build, run tests or commit. Do not use `haiku-fixer` in this run.

STEP 1 - PLAN SLICES (you, cheaply)
- Get file sizes with one shell command (for example `Get-ChildItem -Recurse Source\Unnamed, MenuWeb -File -Include *.h,*.cpp,*.js,*.mjs | Select FullName, Length`). Do not open source files to plan.
- Group files into slices by area (Menu, Input, Foundation, Hud, Playground plus project-wide, Tests, MenuWeb). Keep a `.h` with its `.cpp`. Cap each slice at about 60 KB of source so a reviewer has room to read, grep and answer. Split large areas (Foundation will need several slices).
- Print the slice plan as a table (slice, files, KB) and continue without waiting.

STEP 2 - FAN OUT
- Launch one `haiku-reviewer` per slice, all in a single message so they run in parallel.
- Each task contains only: the slice name, the exact file paths, and one line of focus that fits the slice (for example "UObject lifetime and delegate unbinding" for controller and pawn code, "test assertions that cannot fail" for Tests). Do not paste code or project background; the agent file already has the rules and output format.

STEP 3 - RECORD COST
- From each Agent result, record `subagent_tokens`, `tool_uses` and duration. Note any reviewer that hit its turn limit or returned partial output. Do not resume or re-run it; record the gap.

STEP 4 - VERIFY
- For every high and med finding, open only the cited lines (about 30 lines around them) and mark it Confirmed, Rejected (say why in a few words) or Unverified.
- Spot-check 3 low findings the same way.
- Do not go looking for new issues yourself, except where reports from two or more slices point to the same cross-cutting pattern. Note those separately.

STEP 5 - WRITE THE RECORD
Create `Docs/Records/<today as YYYY-MM-DD>-AgentReview.md` and add it to the index in `Docs/Records/README.md`. Contents:
1. Summary: 3-5 sentences on overall code health and the most important problems.
2. Confirmed findings grouped by severity, each with `path:line`, the problem and the suggested fix.
3. Cross-cutting patterns.
4. Rejected and unverified findings, briefly, so worker accuracy is visible.
5. Cost table: slice | KB assigned | subagent tokens | tool uses | findings | confirmed | rejected. Add a totals row and flag any slice over 100k.
6. Observations on the experiment: where Haiku did well, where it was wrong or shallow, and how you would re-slice next time.

FINAL REPLY
At most 15 lines: the 3 most important confirmed findings, worker accuracy (confirmed / total checked), total subagent tokens, any slice over budget, and the path of the record.
