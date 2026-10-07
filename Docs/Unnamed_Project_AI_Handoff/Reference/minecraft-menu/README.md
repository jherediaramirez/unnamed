# Minecraft Java Edition 26.3 menu prototype

An interactive, browser-based recreation of the vanilla Java Edition menus. The complete application is in `index.html`, with embedded CSS and JavaScript. Required images, fonts, translations, and audio are in `assets/`. There is no backend, build step, game engine, or account login.

## Run

Extract `minecraft-java-menu-263.zip`, open a terminal in the extracted directory, and run:

```sh
python -m http.server 8000
```

Open [http://localhost:8000](http://localhost:8000). On systems where Python is named `python3`, use `python3 -m http.server 8000`. Serve the directory rather than opening `index.html` as a `file:` URL; translated language files use local fetches.

Start at 1920 × 1080, browser zoom 100%, GUI Scale Auto. This gives Minecraft GUI scale 4 and a logical 480 × 270 interface. At 1280 × 720, Auto gives scale 3 and logical 427 × 240. The app follows Minecraft's integer GUI scaling and minimum logical size, including the Unicode-font scale constraint.

## Use

- Click buttons, drag sliders, or focus sliders and use the arrow keys, Home, and End. Right-click cycling settings to move backward.
- Tab and Shift+Tab move focus; Enter or Space activates buttons. Escape returns to the parent screen or cancels an uncommitted dialog. Lists support selection, scrolling, and keyboard navigation.
- Settings, key bindings, fictional worlds, and fictional servers persist in this browser. Clear this site's browser storage to restore the initial defaults.
- Sound starts after the first interaction, as browsers require. Music and master-volume controls affect the bundled native music and click sounds. GUI scale, menu blur, panorama speed, splash visibility, fonts, high contrast, browser fullscreen, and available narrator speech affect the prototype. Gameplay-only settings store their values.
- World loading/creation, multiplayer connections, Friends, account-dependent Realms actions, and folder access end at reversible Minecraft-styled messages. No server is contacted and no world is created. Delete operations affect only the fictional list and provide Restore Entry.
- Quit Game opens a simulated exit screen with Return to Title. Credits can be skipped with Escape; hold Space to speed the scroll, Control for an additional speed increase, and Up to reverse it.

## References and verification

The exact released **26.3** client and asset index **34** were inspected, and downloads were checked against Mojang's SHA-1 hashes. The release manifest marks this build as a release dated September 15, 2026. The logo, subtitle, six panorama faces, UI sprites, English strings, indexed languages, click sounds, nine menu tracks, and credits music come from that distribution. WOFF fonts are browser conversions of the supplied bitmap/Unifont glyphs. `assets/asset-provenance.json` accounts for all 938 bundled asset files.

Primary references:

- [Official Java Edition 26.3 release article](https://www.minecraft.net/en-us/article/minecraft-java-edition-26-3)
- [Official detailed 26.3 release notes](https://feedback.minecraft.net/hc/en-us/articles/48913133328013-Minecraft-Java-Edition-26-3)
- [Exact 26.3 version metadata](https://piston-meta.mojang.com/v1/packages/702fe59163c6ee6578607daa85811d9bc9c7cc40/26.3.json)
- [Exact client JAR](https://piston-data.mojang.com/v1/objects/e877b6a07acd633fb3bb475002175cec036e7b87/client.jar)
- [Exact asset index 34](https://piston-meta.mojang.com/v1/packages/1e4e4a68dece1c0f51b8f93b45da8fe8da51bcd3/34.json)

The client JAR itself is not included in this download. See `reference-inventory.md` for the screen inventory and comparison geometry, and `qa-results.json` for the browser checks. Six screenshots cover Main Menu, Options, and Video Settings at both desktop sizes; `menu-captures.png` combines the three 1920 × 1080 captures.

**Reference gap:** no independently verified vanilla 26.3 Windows screenshots or video frames were accessible. The interface was checked against this exact build's assets, strings, and inspected client screen/layout code, rather than compared pixel by pixel with a native game capture. To finish native visual calibration, the missing references are Main Menu, Options, and Video Settings screenshots from released Java 26.3 on Windows, English (US), at 1920 × 1080 and GUI scale 4, with their video/graphics settings recorded.

## Remaining differences and browser limits

- The panorama uses the actual six 1024 × 1024 faces, native 85° field of view, 10° tilt, and rotation rate. WebGL rendering and CSS background blur approximate the game's rendering treatment. Drawing is capped at 30 fps; lower selected limits are honored. If WebGL is unavailable, a static image of the same panorama is used.
- Browser font rasterization can differ slightly from the native bitmap renderer. The splash uses the authentic “Also try Terraria!” string; random/date-specific splash selection and the rare alternate title logo are omitted.
- Windows display modes cannot be enumerated or selected. Exclusive Fullscreen Mode stays disabled at Current. Exclusive fullscreen, VSync, and graphics API values are stored; browser Fullscreen uses the browser API. There is no Vulkan/OpenGL device switch or restart.
- Narrator uses browser speech where available. Audio playback was verified through media state and volume changes; speaker output was not directly heard. Native music scheduling is approximated.
- Fictional world/server rows replace saved game data. Realms/Friends account states are represented without authenticating. Superflat block/preset item icons are omitted. Custom resource/data-pack imports stop at an explanatory message; the native built-in Programmer Art and High Contrast menu resources are bundled.
- Native credits text and music are used; end-of-scroll pinning/timing differs slightly. English (US) is the calibrated target. Other languages use native translations, but complete right-to-left shaping and Japanese glyph-variant rendering are not reproduced.
- Game-rule labels use the build's English strings and its inspected registry-migration mapping where the new identifiers lack corresponding English keys; those mapped labels have no native screenshot confirmation.
- External links have a Minecraft confirmation screen. Clipboard success and opening an external browser tab were not exercised in QA; clipboard failure returns an in-app explanation.

This is an unofficial interface study. Minecraft and its bundled game assets belong to Mojang/Microsoft.
