# Known Gaps - October 7, 2026

## Intentional Mocks, Not Bugs

- Video settings, player-mappable key bindings with their Hold/Toggle policies, and 101 additional preferences (plus language and pack order) persist. Most of those additional preferences are saved without an effect yet; see [Menu Preferences](../MenuPreferences.md). World/server samples, generation drafts and rules still reset when the document or game reloads.
- Worlds, servers, accounts, Friends services, generation, backups, folders, clipboard, telemetry, audio, and other graphics options are not connected to real services. Their buttons may navigate, validate, or edit fictional session data.
- Exclusive fullscreen/video-mode selection and other unavailable capabilities remain disabled. See `Docs/ControlRegistry.json` for individual IMPLEMENTED, UI_ONLY, and INTENTIONALLY_DISABLED statuses.
- The actual renderer is DirectX 12/D3D12. Graphics API still shows source mock choices; a locked DirectX 12 display is recommended but not implemented in this fix batch.
- Hard Reset is temporary debugging UI. It deliberately protects unrelated saves and is not a factory wipe. Real-world/progression reset policy must be designed when those systems exist.

## Remaining Acceptance Gates

- Physical Windows keyboard/mouse focus and shortcut feel. CEF automation is not physical OS input acceptance.
- Driver-level VSync/tearing, multiple monitors, DPI changes, and unsupported-window failures.
- An actual native bridge/CEF transport stall. JavaScript tests cover never-resolving and late promises, not a disrupted engine transport.
- Shipping builds, other platforms, and future gameplay/world save behavior during Quit.
- The Foundation fullscreen key (fixed during the code-review cleanup) is verified by a native request test only; the actual window-mode change needs a visible standalone window.
- Exact native Minecraft text/pixel parity and branding/asset distribution rights. Provenance remains under `Reference/minecraft-menu`.

No new native build or editor-safety run was required for the JavaScript-only fixes. Prior native build, automation, and editor protection evidence is inherited only because native source/binaries are hash-identical. Current packaged UI, persistence, and normal-exit tests are rerun separately.

Start future work from the editable sources and the current candidates named in the [README](../../README.md) (`Packages/CodeReviewDevelopment20261007`; `Packages/FoundationDevelopment20261007-r3` and `Packages/SuperFlatDevelopment` are preserved checkpoints), not an older candidate or the archived pre-fix review ZIP. Use fresh package/capture/report names to preserve evidence.
