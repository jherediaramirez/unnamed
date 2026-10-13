# Atmosphere: twilight and stars

2026-10-13. Local candidate on `feature/atmosphere-cycle`, based on `8ff64c7057e8a2fb460825742fc3dd6ef48b0242`. The initial merge of `feature/voxel-world` was already up to date. No push is authorized. Jesus replaced manager/Claude coordination with this chat and Codex validation. Report A is ready with a green final suite and the visual limitations below; local commit and later merge validation await his go.

## Choice and implementation

The directional light reaches zero at the sun/moon handover (13155/13156 and the mirrored dawn). An exposure-only prototype improved 23000 west lower-half luma from 3.26 to 7.00, but over-brightened 13000 from 6.88 to 20.24 and still fell to 3.37 at the exact handover. Exposure cannot recover light that is absent. Advancing only the moon fade also leaves the sun-selected side of the handover unfilled.

The candidate therefore provides a temporary authored terrain minimum, weighted by `Night * (1 - Moonlight)`. It preserves the sun's existing atmospheric glow, bridges the zero-light interval, and returns to zero once the moon light reaches full intensity. It reads `GetEnvironment()` and introduces no clock or directional light. `ApplyCycle` remains the light writer; `ApplyLook` still owns exposure, sky light and haze, and now updates a separate twilight post-process component.

- The cool terrain minimum is linear HDR, exposure compensated, before DOF. It uses base texture, face orientation, the existing edge/underwater distance fade, and the camera's open-sky pin. It keeps brighter channels and black-base sky pixels unchanged. Enclosed-camera pin 0 contributes no twilight floor.
- Current water and glass blend after DOF and therefore filter this lit terrain normally. A rejected after-tonemapping prototype bypassed water filtering; independent review caught it. The original cave-floor asset, generator, numerical helper and existing tests were restored exactly. The new material is `M_AtmosphereTwilight`.
- A dim navy additive backdrop supplies a sky minimum. Its separate weight is `Night * (1 - SmoothStep(0, 1, Clamp(MoonDirection.Z, 0, 1)))`: the moon light reaches full intensity while the moon is still low and the atmospheric sky is dark, so fading the sky with the terrain caused a second sky dip at 13500/22500. The sky now eases with sine altitude to zero at midnight. It follows the camera, uses ordinary depth testing, and draws before stars and foreground transparency. It is an authored minimum, not a new physical atmosphere simulation.
- Stars are 768 deterministic tangent quads on a camera-centered sphere: 1,536 triangles in one retained mesh. They have fixed grayscale brightness and no animation/twinkle, fade during the latter half of `Night`, and use depth testing to stay behind opaque terrain. The unlit material compensates exposure and draws before water/glass. The existing block asset root cooks the two new materials.

## Switches

`voxel.Atmosphere.Cycle` remains 0 by default. Both features also require Night 1.

| Variable | Default | Purpose |
|---|---:|---|
| `voxel.Atmosphere.Twilight` | 0 | Temporary terrain and sky minimum |
| `voxel.Atmosphere.TwilightFloor` | 1.5 | Terrain strength, clamped 0–8 |
| `voxel.Atmosphere.TwilightSky` | 0.2 | Navy sky strength, clamped 0–1 |
| `voxel.Atmosphere.Stars` | 0 | Deterministic square stars |
| `voxel.Atmosphere.StarBrightness` | 0.3 | Exposure-compensated star brightness, clamped 0–2 |

The original night exposure, moon brightness/phase and sky tunables are unchanged. Square sun/moon disks, clouds, weather, default changes and the quiet DN-P p95 study remain outside this change.

## Evidence and validation

Evidence root: `Tests/Evidence/2026-10-13/AtmosphereTwilightStars-20261013-133953`.

The root contains the clean-status/index/map preservation snapshot, exact launch helpers, per-capture DLL hashes and arguments, pilot generations, numerical analysis, independent review, native reports and mutation logs. All captures use the existing offscreen capture path; scenario changes are arguments, not new capture C++. Every heavy launch passes `QuietWindow.ps1`; builds use `-NoHotReloadFromIDE -DisableAdaptiveUnity`.

The final source has 14 passing atmosphere tests, with no warnings or failures (`runs/final2-before-matrix/Report/index.json`). Tests cover the existing clock/orbit/night, both twilight envelopes, the open-sky pin, HDR floor and material order, switch restoration, the deterministic star field, visibility, and retained component lifecycle. The existing Look test also passed after restoring its original cave-floor path.

The deliberate regressions were detected:

| Mutation | Expected failing coverage |
|---|---|
| Remove the terrain twilight envelope | Envelope, composition and switches |
| Keep stars visible during daylight | Star visibility and component behavior |
| Remove the HDR terrain minimum | Linear floor and composition |
| Apply the HDR floor without the open-sky pin | Cave isolation in composition |
| Reuse the short terrain fade for the sky | Sky envelope and live switch/reader wiring |

Each mutation was restored from exact saved bytes, then rebuilt with `-DisableAdaptiveUnity`. The older after-tonemapping prototype's pin mutation is retained as historical evidence, not as validation of the final HDR material. `review-1.md` caught the water-compositing problem; `review-2.md` verified that repair; `review-3.md` found no remaining actionable issue after the separate sky fade and its failing mutation.

The nine `probe2-*` captures use DLL `DC871461224956D2E0AB9E0BD934A613AA5AF33CD789F81E0162F815BF09C0F0`. West sky display luma at 13500 / 14000 / 15000 / 16000 / 18000 is 14.28 / 13.65 / 10.12 / 8.30 / 9.31. The original short sky fade fell to about 4.3 at 13500. The 21000 central sky ROI includes the existing moon halo, so its higher value is not a uniform background change or evidence of the envelope alone. Ground brightness across those later times still follows the existing moving moon and its shadows.

An additional off-axis sky ROI avoids the central moon disk path: 13500 is 11.64, 22500 is 12.86, and midnight is 7.97. The lowest sampled dusk value is 7.17 at 16000, about 0.8 below midnight; it remains visibly navy. `sky-continuity-probe2-corner/` records that cross-check. These samples support a smooth-looking transition, not an assertion that every pixel or time is brighter than midnight.

The final 36-frame matrix (`final2-*`) and twelve controls (`control2-*`) passed capture provenance and runtime checks on that same DLL. The six requested times are each captured east/west with both switches off, Twilight alone, and Twilight plus Stars. `analysis-final2/decision-sheet.png` gives the principal before/after pictures; both full directional sheets and all measurements are beside it. The earlier `final-*` matrix is revision 1, superseded by the separate sky fade. Pilot images must not be mistaken for the final comparison matrix.

| West sample | Opaque hill, off / on | Off-axis sky, off / on | Mixed lower half, off / on |
|---|---:|---:|---:|
| 13000 | 3.32 / 11.09 | 4.33 / 13.19 | 6.89 / 12.10 |
| 23000 | 3.44 / 11.15 | 2.04 / 10.54 | 3.26 / 8.60 |

The opaque hillside's minimum among the requested samples is 3.36 versus matched midnight's 3.34. Mixed water/canopy views can still be darker than midnight because of existing moon direction and shadows: west 22500 lower-half luma is 7.62 versus midnight 9.23; east 13500 is 3.65 versus midnight 7.55. This change does not force every scene pixel to a midnight value.

The paused directional-light handover pairs have full-frame luma changes of +0.0082 at dusk and -0.0005 at dawn, with no visible jump in the inspected sheets. Daylight and Cycle 0 feature-toggle controls are visually unchanged; small independent-launch differences remain. `control-audit-final.json` and the gate/handover sheets record those checks.

The repeated-star controls show fixed sky points. Off-axis sky mean absolute RGB change is 0.359 between Stars off/on, versus 0.170 for repeated off captures and 0.173 for repeated on captures. The opaque hill and water-scene differences remain below repeated-off noise, with no positive changes of 8 luma or more in those sampled regions. `stars-final2/star-controls.png` shows the isolated differences. These static checks do not establish moving-camera shimmer performance.

The paired cave and fluid runs passed provenance/runtime checks on the final DLL (66 valid PNGs). The three cave views and all 20 fluid views were inspected. The cave wall is visually unchanged (luma 4.235 to 4.233); the corridor looks into open sky and cannot certify an enclosed camera. Lava remains coherent, with normal animation differences. No visible new depth/sort overlay was found. Glass and underwater appearance are not covered by this fixture.

Water remains dark next to the lifted opaque terrain at the zero-light handover: a waterfall region is 4.85 to 5.05 while adjacent stone is 4.86 to 19.90. This is a visible appearance limitation, not an equally improved water result. See `feature-checks/final-features/feature-visual-review/review.md`, its annotated water regions, and the complete fluid sheets.

The enabled-feature flight completed all eight legs (99.845 simulation seconds, 7,420 frames) without unexpected runtime errors, near holes or late columns. Its four stills show a remaining elevated-view black horizon band, including the initial hover. The unchanged opaque fog floor hides the new depth-tested navy backdrop at a time with effectively zero directional light; this is the supported mechanism, not a proven regression without a matched feature-off flight. Circle-shot stars also streak alongside terrain motion blur. These pictures do not certify the panoramic look or moving-star sharpness. The existing far-field counters record 2,572 frames with far holes; 46 frames exceed the chunk-work budget, worst 5.49 ms. This is functional flight coverage, not controlled performance acceptance. See `review-4-flight.md` and the feature flight's `flight.txt`.

The first full final-profile run stopped with 307/309 native passes: Mesher.Digest and Pipeline.ShiftPlan each received an unrelated auto-started remote-server HTTP `socket_send_failure` log error, with no assertion failures. That failed run is retained in `final-suite.json` and `Checks-final-20261013-153805`.

The complete suite was rerun on the same DLL with the engine-supported process-only argument `-ini:EditorPerProjectUserSettings:[/Script/ModelContextProtocolEngine.ModelContextProtocolSettings]:bAutoStartServer=False`, supplied through `UE-CmdLineArgs`. No project settings or test assertions were changed, and no steps were skipped. `RunChecks -Profile final -Flight smoke -Base feature/voxel-world` finished **GREEN**, exit 0, at 16:04 EDT. Summary: `Tests/Evidence/2026-10-13/Checks-final-20261013-154255/summary.txt`.

- 309/309 native tests passed (one external connectivity-timeout warning, no failures or crashes).
- 205/205 separate voxel suites passed without warnings; all 9 expected hashes matched.
- Runtime scenarios passed 128 + 83 + 27 checks, and world creation passed 38 checks.
- The default smoke flight passed with no unexpected errors. The separate feature-enabled full flight above also completed.
- 85 npm tests, source checks and all five reference verifiers passed; the source/DLL/suite time chain is green.

`final-suite-isolated.json` records identical before/after DLL SHA-256 `DC871461224956D2E0AB9E0BD934A613AA5AF33CD789F81E0162F815BF09C0F0`. Its raw environment-restoration equality is false because PowerShell converted an absent value to an empty string; a throwaway-variable probe reproduced that distinction. The disposable helper process exited, the fresh caller has no `UE-CmdLineArgs`, and repository settings are unchanged. `final-preservation-audit.json` records this cleanup check, all 17 product hashes and both map hashes unchanged, exactly 19 intended changed files, no staged files and no remaining S11 Unreal processes. Unreal was released at 16:04 EDT.

The shared `feature/voxel-world` ref advanced through `d5d95bf` to `42f6606c265ac65f77232023f9954950b610a732` during validation (far tree LOD and its default enabled). This candidate and its captures still use the stated `8ff64c7` baseline. Recheck that ref for the later merge and merge-profile checks, after Report A and local commit approval. The updated handoff belongs to that Report B step; nothing has been committed or pushed here.

Measurements are 8-bit display luma, not physical luminance. The east camera is bamboo occluded; the west top ROI is clear sky and its marked hillside ROI is opaque ground. Lower-half and lower-center measurements include water and other scene content. Vanilla noon/midnight frames and `sp1_field_cave_sheet.png` are unmatched visual references, not numerical targets.

## Integration (2026-10-13, local only)

- Candidate commit `4d23faf` (the 19 files above; the 17 product hashes matched `sky-tail-source-manifest.json` before staging). Merge of `feature/voxel-world` at `42f6606` (far trees, on by default) into `feature/atmosphere-cycle`: `9ef2a4f`; one conflict, `Docs/Records/README.md`, both sides kept. Nothing pushed.
- Merge suite: `Tests/Evidence/2026-10-13/Checks-merge-20261013-161409/summary.txt`, GREEN in 24 min, every step run, 9/9 hashes, native crashes 0; process-only `UE-CmdLineArgs` MCP `bAutoStartServer=False` override, restored to absent afterwards. Merged DLL SHA-256 `7E3897048D0B3CF2F420B8836E6ADC06313E412BDE7EB7C258213E4FAC613FBE`.
- Recaptures on the merged DLL: `captures/merge-{13000,23000}-west-tw{0-s0,1-s1}`, compared in `merge-recapture/merge-vs-premerge-west.png` and `.json`. Twilight-relevant regions (opaque hillside, off-axis sky, top, lower centre) match the pre-merge captures within 0.14 luma; canopy and lower-half regions read 0.4-1.1 lower because far trees now draw trunks. The old `final2-*` captures used the pre-merge DLL and do not validate the merged build on their own.
- Defaults unchanged: Cycle 0, Night 1, Twilight 0, Stars 0; `voxel.Far.Trees` stays 1. The look limits listed above (dark water beside lifted terrain, the far-horizon band, star streaks in fast turns, no performance/packaged/moving-camera acceptance) remain open; see the [atmosphere handoff](2026-10-13-Atmosphere-Handoff.md).
