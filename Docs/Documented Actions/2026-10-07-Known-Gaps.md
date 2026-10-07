# Known Gaps - October 7, 2026

## Intentional Mocks, Not Bugs

- Only five settings persist: fullscreen, frame limit, VSync, GUI scale, and menu blur. Other options, bindings, language, packs, rules, and sample lists reset when the document/game reloads.
- Worlds, servers, accounts, Friends services, generation, backups, folders, clipboard, telemetry, audio, and other graphics options are not connected to real services. Their buttons may navigate, validate, or edit fictional session data.
- Exclusive fullscreen/video-mode selection and other unavailable capabilities remain disabled. See `Docs/ControlRegistry.json` for individual IMPLEMENTED, UI_ONLY, and INTENTIONALLY_DISABLED statuses.
- The actual renderer is DirectX 12/D3D12. Graphics API still shows source mock choices; a locked DirectX 12 display is recommended but not implemented in this fix batch.
- Hard Reset is temporary debugging UI. It deliberately protects unrelated saves and is not a factory wipe. Real-world/progression reset policy must be designed when those systems exist.

## Remaining Acceptance Gates

- Physical Windows keyboard/mouse focus and shortcut feel. CEF automation is not physical OS input acceptance.
- Driver-level VSync/tearing, multiple monitors, DPI changes, and unsupported-window failures.
- An actual native bridge/CEF transport stall. JavaScript tests cover never-resolving and late promises, not a disrupted engine transport.
- Shipping builds, other platforms, and future gameplay/world save behavior during Quit.
- Exact native Minecraft text/pixel parity and branding/asset distribution rights. Provenance remains under `Reference/minecraft-menu`.

No new native build or editor-safety run was required for the JavaScript-only fixes. Prior native build, automation, and editor protection evidence is inherited only because native source/binaries are hash-identical. Current packaged UI, persistence, and normal-exit tests are rerun separately.

Start future work from `Packages/MenuReviewFixesDevelopment` and the editable sources, not an older candidate or the pre-fix review ZIP. Use fresh package/capture/report names to preserve evidence.
