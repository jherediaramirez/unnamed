# Independent Review Fixes - October 7, 2026

All five independent-review findings are addressed in the source and the new Development candidate.

| Problem | Fix | Regression evidence |
|---|---|---|
| World names, preset text, and invalid rule text were lost or mismatched after redraw/child return | Keep raw drafts in the current dialog; render and submit the same text; preserve invalid inputs and disable Save/Done correctly, including hidden rules | Packaged draft suite: child return, F11 redraw, empty inputs, filtering, correction, Save, and Cancel |
| Failed earlier setting request discarded a newer queued choice | The failing request no longer deletes a later same-setting queue entry | Unit test queues 30 then 90, rejects 30, and verifies 90 is applied |
| Unanswered bridge calls could lock settings/Quit indefinitely | Five-second request waits; twenty-second overall Quit budget starts before settings drain; recovery unlocks controls; late/stale replies cannot replace newer state | Unit tests simulate unanswered settings/drain/Quit requests, late completion, retry, and reordered polls |
| Blank server name was accepted despite the intended validation | Require a nonempty trimmed name and valid address on input, redraw, and submission; remove silent default-name fallback | Packaged and unit tests cover whitespace, bad address, valid Save, and redraw |
| New tests overwrote shared supplemental observations and mixed old registry evidence | Store reset/context observations inside each capture; reject existing capture/report paths; registry selects matching successful HTML/profile captures only | Capture-loader unit test plus current isolated video/menu runs and preservation hashes |

## Boundaries

Browser deadlines stop waiting; they cannot cancel a native operation already dispatched to Unreal. An unanswered Quit completion permits an explicit retry instead of leaving its cached pending state locked; Unreal still protects against duplicate requests, and fresh readback replaces the retry allowance. The native window/shutdown safeguards remain unchanged. No forced-exit fallback was added.

World/server/preset/rule actions are still session-only mocks. Correct draft behavior does not create real world generation, networking, or world-file editing. The Graphics API selector was not changed as part of these five fixes.

## Verification

Passed: 25 unit tests and 984 runtime checks. At both 1280x720 and 1920x1080: 24 draft/validation checks, 37 video checks, and 423 full-menu checks. Normal quit/persistence/recovery passed 16 checks. All 938 reference/mirrored assets remain unchanged.

Final results and hashes are recorded in [Verification](../Verification.md) and `Tests/Evidence/2026-10-07/ReviewFixesArtifactManifest.json`. Before-fix failures and harness-label mistakes are retained, not counted as successful acceptance. Real transport stalls were simulated in JavaScript tests; no actual engine bridge outage was injected.

For a fresh rerun from `Tests`, use unique output names:

```powershell
npm test
node source-check.mjs
node review-regression.mjs --capture=next-drafts-1280x720
node review-validation.mjs --prefix=next-menu-review
node quit-game.mjs --report=NextQuitResults.json
node registry.mjs --capture=next-menu-review-packaged-1920x1080 --video-capture=next-menu-review-video-1920x1080
```

The draft, validation, and quit suites launch and normally close only their own diagnostic games. A failed harness may terminate only its own process for cleanup; that never counts as successful Quit verification.
