# Browser verification

Verification used a live Chromium browser running the static prototype. The app was exercised in true 1920 × 1080 and 1280 × 720 iframe viewports, with integer GUI scales 4 and 3. A scaled interaction harness allowed reliable clicks throughout the larger viewport; final screenshots use the full physical viewport without scaling the captures.

The report contains 280 recorded checks: 274 passed, two confirmed disabled controls, and four superseded assertions. The original failures and their resolutions remain visible in `qa-results.json`. These involved the initial key-binding/reset implementation and test expectations for the native English locale count and telemetry-label capitalization. Subsequent corrected checks passed. No unresolved failed assertion remains.

Checks covered all visible main-menu responses; every Options category and nested settings route; sliders and cycling values; consistent shared settings; language/font changes; GUI scaling; fullscreen; sound playback state and volume; key capture/conflicts/reset; native scrollbars and scrolling; resource-pack movement and high contrast; world selection/edit/delete/restore; all three Create New World tabs; Hardcore constraints; rules/experiments/biome/preset staging; multiplayer warning, address validation and CRUD; offline connection/account endpoints; Realms entry routes; credits; link cancellation; and simulated Quit/Return.

All six required viewport/screen captures were visually inspected. The smaller viewport also passed bounding-box checks for main/options controls and all three creation tabs. Video rows scroll to their final controls without extending past the footer.

The inline JavaScript passed `node --check`. Every one of 938 bundled asset files was traced to the verified exact client, asset index, native built-in resource packs, or declared metadata/font conversion. Critical panorama, audio, locale and UI paths were checked for local existence. No prototype warnings or errors were observed in captured browser logs; the browser extension produced unrelated metadata warnings.

Native screenshot pixel comparison remains unverified. These captures were compared with the inspected native geometry, assets and labels. No Windows Minecraft process was run. External-link opening and clipboard success were not exercised; their confirmation/failure routes are implemented. Physical speaker output was not directly heard. GPU API changes, exclusive display modes, playable worlds, server connections and account actions are deliberately simulated or inactive.
