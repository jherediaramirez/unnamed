# Player Foundation — October 7, 2026

Historical r2 delivery below. The current r3 candidate restores the established **Left Shift sprint / C crouch** defaults; see [Personal controls restored](2026-10-07-Personal-Controls.md) for the correction and focused verification.

Implemented the first-person primitive lab in the existing Unnamed project after auditing the real CEF menu/native settings bridge and inspecting the exact vanilla Java 26.3 client. The reference is SHA-1 `e877b6a07acd633fb3bb475002175cec036e7b87`, internal build `2026-09-15T11:20:48Z`; Unreal is 5.8.3 CL58210709.

The foundation uses a world-aligned box pawn, shared 100 cm/block coordinate conversion, fixed 20 Hz source-derived movement, independent render/mouse response, jump/sprint/sneak/ledge/headroom handling and configurable settings. Existing Controls owns 35 working rows; the full 62-row vanilla catalog plus fixed/context shortcuts is documented. New token fixtures exercise targeting/actions without adding item, combat or terrain systems. F3 provides actual state and F3+F6 opens saved debug options.

Launch `C:\dev\projects\Unnamed\Packages\FoundationDevelopment20261007-r2\Windows\Unnamed.exe`, then Singleplayer → Player Foundation. `Scripts\LaunchFoundation.ps1` also supports the direct lab or editor executable. Super Flat and all prior packages remain available. Factory sprint/sneak are Ctrl/Shift; existing saved custom bindings survive.

Build/cook/package succeeded. 13 native suites, 44 JavaScript tests, 26 packaged CEF/native checks and 34 runtime checks at each requested 30/60/120 FPS cap passed. Actual rendered means were 24.37/46.36/65.15 FPS; exact prescribed cadences separately passed the pure simulation suite. Bindings/toggle/debug scale persisted through a new process. Measurements match the source-derived walk/sprint/sneak, stopping and jump baselines within declared tolerances.

Played-client comparison and OS hardware/alternate-layout/mouse-feel acceptance are pending behind a Windows Security prompt, which was not acted upon. Cuboid collision, primitive auto-jump and deferred camera effects have explicit limits. Prior packages and staged entries were verified preserved; all 9,623 baseline files remain present.

Use the [launch guide and document index](../Foundation/README.md), [verification report](../Foundation/VERIFICATION.md) and [durable result index](../../Tests/Foundation/FinalResults.json) for exact commands, source evidence, hashes and remaining checks. Earlier menu/input records retain their historical defaults and scope.
