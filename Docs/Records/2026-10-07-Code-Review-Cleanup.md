# Code-review cleanup - October 7, 2026

An independent review of the C++, menu runtime, scripts, tests and docs found that the menu and the first-person Foundation work had grown side by side: two code styles, controllers depending on each other, IDs and paths typed in many places, one oversized diagnostics class, duplicated JavaScript plumbing, and mixed folders. Every finding was checked against the code before acting; all of them held. This record lists what changed. Branch: `refactor/code-review-cleanup`, one commit per step.

## What changed

1. **Formatting.** `.clang-format` and `.editorconfig`; every C++ file reformatted in a commit that changes nothing else.
2. **Out-of-date text.** Travel errors, the design-reference notice, input bridge messages and pre-bridge key labels match the current product; Known Gaps, Architecture and Super Flat docs updated.
3. **Layout.** Menu sources in `Source/Unnamed/Menu/`, Super Flat in `SuperFlat/`, one class per file, module-root includes. Menu generator and shipped JS in `MenuWeb/`, editor Python in `Scripts/Editor/`.
4. **Menu generator.** `AdaptMenu.mjs` split into one patch per feature. Every edit targets original reference text and overlapping edits are rejected, so no patch edits another's output. Output stayed byte-identical through the split.
5. **One place for IDs and paths.** `UUnnamedProjectSettings` holds the menu map, the world catalog and the input assets. The page receives the catalog through `GetState`. Python reads the same values; builds cook `MapsToCook`. One `UnnamedSession::CanSaveSettings()` rule.
6. **No controller inheritance.** `UMenuBrowserHost` hosts the browser and bindings. The menu and Foundation controllers each add it; only Foundation adds diagnostics.
7. **Diagnostics split.** `UFoundationInputRouter`, `UFoundationHud`, `UFoundationPanels`; `AFoundationController` coordinates them in the original per-frame order.
8. **Bug fix.** The Foundation fullscreen key sent a malformed request and never worked; it now uses `UMenuVideoSettings::ToggleFullscreen()`.
9. **Menu JavaScript.** One `MenuBridge` client, a `MenuStores` registry, a shared quit/travel routine, and explicit `MenuInput` capture hooks instead of replaced functions.
10. **Consistency.** Crouch in code (the saved `key.sneak` ID kept), no legacy `RequestedGait`, defaults defined once, crash-safe video saves, the probe kept out of Shipping, three log categories, CommonUI leftovers removed, zero build warnings.
11. **Naming.** Verb/`Get*` functions, `On*` delegates, `b`-prefixed bools, binding names matching their JS globals, renamed mismatched files. See [Naming conventions](../NamingConventions.md).
12. **Housekeeping.** Evidence moved to `Tests/Evidence/2026-10-07/` (still committed). The duplicate handoff folder removed (it remains in git history). The review packet moved to `Docs/Archive/`. Foundation doc names normalized. `Docs/Documented Actions/` renamed to `Docs/Records/`. Superseded packages deleted.

## Behavior changes to know about

- Singleplayer selects **Player Foundation** by default (the first catalog world) instead of Super Flat.
- The plain menu map no longer has the F3 overlay or the F2 screenshot key; F11 still toggles fullscreen through the page. All diagnostics remain in the Foundation lab.
- World-travel messages say "world" instead of "level".
- The fullscreen key in the Foundation lab now works.

## Verification

Each step was verified before committing: editor build, native automation (13 suites at the start, 15 now), Node unit tests (44 at the start, 53 now), the in-game Foundation probe (38 checks at 60 FPS, editor `-game`) and the CEF end-to-end suite `foundation-menu.mjs --editor` (30 checks: capture, relaunch persistence, three travel paths and return). Results are in [Verification](../Verification.md).

## Left as is

- `BuildMenu.ps1` and `BuildFoundation.ps1` remain separate entry points.
- The simulation keeps `P` for its profile in formula code; numbers there mirror the reference.
- The two direct-launch scripts still pass `/Game/Maps/L_Foundation` on the command line.
