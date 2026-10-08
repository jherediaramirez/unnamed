# Minecraft Java 26.3 HUD research for Unnamed

Inspected 7 October 2026. Target: vanilla Java release **26.3**, resource pack **97.1**, protocol **777**. This is a research and asset handoff for `C:\dev\projects\Unnamed\Unnamed.uproject`, using UE 5.8. It does not implement or import the HUD.

## Evidence and what is included

The installed vanilla client was independently hashed against fresh Mojang version metadata. SHA-1: `e877b6a07acd633fb3bb475002175cec036e7b87`. Its internal build timestamp is `2026-09-15T11:20:48+00:00`. The source inspection used Vineflower 1.12.0 on that client, not a NeoForge-patched class or an older tutorial. The source stays in `C:\dev\tmp\Unnamed-HUD-Research-20261007\source`; `evidence/source-map.json` records paths, method locations and SHA-256 identities. Decompiled line numbers are specific to that local output.

The package contains **194 unchanged PNGs**, **29 JSON/MCMeta support files**, and **3 Unicode font archives**: 226 asset files. All 84 PNGs under the native `textures/gui/sprites/hud` directory are included. Additional groups include all 40 effect icons, 22 boss-bar sprites, spectator controls, toast backgrounds, latency symbols, fonts and camera overlays. `manifest.json`, `manifest.csv`, `PNG-GUIDE.md`, and `catalog.html` map every PNG to a purpose, position, behavior and Unreal use. The original namespace tree avoids ambiguous names such as several unrelated `background.png` files.

The fresh asset index is **34**, SHA-1 `1e4e4a68dece1c0f51b8f93b45da8fe8da51bcd3`. It supplies actual Unicode providers in place of the client's empty `include/unifont.json` stub. Each indexed object was fetched from its official hash-addressed location and verified. Fixed texture references in the inspected HUD, context-bar, boss, tab, chat, subtitle, toast, spectator and screen-effect classes have a separate coverage report: **130 resolved references, zero missing**. Runtime item models, player skins, arbitrary resource packs, all inventory screens, and every possible data-driven camera overlay are outside that bounded coverage claim.

This is source-derived behavior plus decoded asset inspection. The interactive layout is a diagram using real PNGs, **not a captured vanilla frame**. No played-client timing experiment or Unreal runtime parity acceptance was performed for this research. Existing project files were inspected read-only. Browser security policy rejected automated local-HTML preview, so visual browser acceptance and interactive-browser checks remain unverified; JavaScript syntax, local links and referenced asset bytes were checked without rendering the page.

Other work changed six existing source files and removed one test file during this research. `evidence/preservation.json` records the observations; this task did not modify or restore them. A final read reconfirmed the text crosshair, native diagnostics and token-based inventory fixture. The next implementation must re-read the then-current controller and input seams.

## 1. Coordinate system and composition

Use logical **GUI units**, not fixed 1920×1080 coordinates. Let `W,H` be Java's GUI-scaled viewport size and `C=floor(W/2)`. Origins below are top-left. Most icons are 9×9, but the icon pitch is **8**, producing a deliberate one-unit overlap. The hotbar forms a 182-unit-wide center anchor. Hearts align with its left edge; food aligns with its right edge. The armor row rises with additional heart rows. Air rises with mount-heart rows independently of player hearts.

Java calculates an integer GUI scale `s`: starting at 1, increment while the requested maximum is not reached and another step leaves at least a 320×240 logical area. The Auto request has no ordinary requested cap. Unicode enforcement can advance an odd scale to an even scale. The logical size is `ceil(framebufferWidth/s), ceil(framebufferHeight/s)`. Use framebuffer pixels, not blindly the Windows client-window size. Debug scaling is separate in 26.3. Sources: `Window.calculateScale:720`, `Window.setGuiScale:808`; official [26.3 release notes](https://www.minecraft.net/en-us/article/minecraft-java-edition-26-3).

At an explicitly chosen GUI scale 3, 1920×1080 becomes 640×360. A 9×9 icon covers 27×27 physical pixels; the hotbar covers 546×66. Auto on that framebuffer normally selects 4, not 3. Odd logical dimensions introduce native integer-rounding asymmetry: do not replace all formulas with a floating-point center and expect identical pixels.

| Element | Native drawn rectangle / position | Relationship |
|---|---|---|
| Hotbar frame | `(C-91,H-22)`, 182×22 | Flush to bottom, nine slots |
| Selected slot | `(C-92+20*i,H-23)`, 24×23 | `i=0..8`; overlaps neighboring frame pixels |
| Hotbar item | `(C-88+20*i,H-19)`, nominal 16×16 | 20-unit slot pitch, three-unit left inset from hotbar origin |
| Left offhand frame / item | `(C-120,H-23)`, 29×24 / `(C-117,H-19)` | Right main hand; only for a nonempty offhand |
| Right offhand frame / item | `(C+91,H-23)`, 29×24 / `(C+101,H-19)` | Left main hand; do not merely mirror the item's artwork |
| Health cell | `(C-91+8*col,H-39-row*rowHeight)`, 9×9 | Grows left-to-right, extra rows upward |
| Food cell | `(C+82-8*i,H-39)`, 9×9 | Fills from the right; replaced by visible mount health |
| Armor | `(C-91+8*i,H-49-(rows-1)*rowHeight)`, 9×9 | Immediately above topmost player-health row |
| Air | `(C+82-8*i,H-49)` normally, 9×9 | Above food; lifted 10 per extra mount-heart row |
| Context bar | `(floor((W-182)/2),H-29)`, 182×5 | XP, locator, mount jump, or empty |
| Experience level | centered measured text at `H-35` | Green text with four black offset passes; independent of bar selection |
| Selected item name | centered at `H-59` | Moves to `H-45` when player cannot be hurt |
| Action-bar message | center translation `(C,H-68)`, text offset y=-4 | Separate from selected item name |
| Crosshair | `(floor((W-15)/2),floor((H-15)/2))`, 15×15 | True viewport center, first person |
| Crosshair attack track | `(C-8,floor(H/2)+9)`, 16×4 | Ready-target marker instead draws 16×16 here |
| Hotbar attack track | `(C+97,H-20)` or `(C-113,H-20)`, 18×18 | Main-hand side, opposite offhand frame |
| Effect tiles | `(W-25*n,1)` beneficial; `(W-25*n,27)` other | `n` starts at 1 per row; tile 24×24, icon 18×18 at +3,+3 |
| Boss bar | `(C-91,12+19*i)`, 182×5 | Centered name at y-9; additional bosses stack downward |

For ordinary ten-heart/twenty-food rows, each group spans 81 GUI units. There is a 20-unit horizontal gap between their extents. Their baseline is 17 units above the hotbar's origin. The context bar is seven units above that origin. This compact relationship is the defining composition; arbitrary responsive horizontal stretching changes it. Source: `Hud.extractItemHotbar:571`, `extractPlayerHealth:771`, `extractFood:967`, context-bar classes.

At W=640,H=360: hotbar `(229,338)`, hearts start `(229,321)`, rightmost food `(402,321)`, armor/air y=311, context bar `(229,331)`, level text y=325, item name y=301, crosshair `(312,172)`. These are rectangle origins, not centers.

## 2. Health, absorption and heart states

Normal starting health is **20 points = 10 full hearts**. A full sprite means two points, half means one. The renderer uses `ceil(actualHealth)`; fractional server health is not a fractional alpha-filled heart. Death and damage are gameplay events, not conclusions drawn by counting red pixels.

There are **44 player-heart PNGs**: four container variants and eight each for normal, poisoned, withered, frozen and absorbing hearts. Each filled family has full/half × normal/Hardcore × ordinary/blinking. The three `vehicle_*` PNGs are a separate mount-health family. Hardcore changes artwork, not the unit represented by a heart.

Appearance precedence is **Poison, then Wither, then fully frozen, otherwise normal**. Absorption normally uses gold absorbing hearts, but uses withered artwork when the selected heart type is Wither. Partially frozen players may have a snow overlay before they qualify for frozen heart artwork. Health Boost expands maximum normal health; Absorption is a separately consumed temporary buffer. Regeneration changes health through gameplay and lifts one selected normal-heart container by two GUI units during the traveling animation.

Calculate layout from `displayMax=max(maxHealthAttribute, oldDisplayedHealth, currentCeiledHealth)`, `a=ceil(absorption)`, `rows=ceil((displayMax+a)/20)`, and `rowHeight=max(10-(rows-2),3)`. Individual containers are `ceil(displayMax/2)+ceil(a/2)`; keep that separate from the row-count expression for unusual odd maxima. The normal one-row case has an unused row step of 11; two rows step by 10, three by 9, and eventually the step floors at 3. Additional hearts extend upward instead of widening the hotbar. Armor follows the top health row. For out-of-range custom maximum-health values, preserve this source distinction and test rather than normalizing it silently.

Draw descending container order, then container, absorption layer if applicable, blinking old-health layer, and current-health layer. Keep current health, last frame health, and delayed displayed health separate. The delayed value catches up when more than 1,000 ms has elapsed since its last update. Damage during damage cooldown sets a 20-tick blink window; a health increase under that condition sets 10 ticks. Blink state alternates by three-tick bands. This history is why a white flash cannot be reproduced with only a statically selected `full_blinking.png`.

Low-health jitter triggers when `ceil(health)+ceil(absorption)<=4` points: two hearts, not four hearts. It adds 0 or 1 GUI unit vertically. Randomness is seeded once from the HUD tick (`tick*312871`), so it does not flicker differently at 30 versus 144 render FPS. The shared stream also influences food/air motion. Reproducing the precise random stream is an optional strict-parity detail; reproducing the tick cadence and bounded motion is essential. Sources: `Hud.extractPlayerHealth:771`, `extractHearts:848`, `HeartType.forPlayer:1470`.

## 3. Armor, hunger, saturation and sprinting

Armor is ten icons representing up to 20 visible armor points. The row is absent when the armor value is zero. An empty armor sprite is therefore part of a partially filled visible row, not a permanently displayed unarmored row. The row does not show equipment durability, toughness, enchantments, Resistance, or a complete damage-reduction percentage. Individual item durability belongs on its inventory icon.

Hunger is **0..20 food points**, two per drumstick. The half/full selection mirrors the armor thresholds but the row runs right-to-left. The six PNGs comprise ordinary and Hunger-effect empty/half/full sets. Exhaustion and saturation are separate gameplay values with no normal vanilla HUD meter. At saturation<=0 the food row jitters on ticks divisible by `food*3+1`, with a per-icon offset from -1 through +1. A player riding a living mount whose health is shown gets mount hearts in this location instead of food.

Food exhaustion greater than 4 spends four exhaustion, then drains one saturation if available; otherwise it removes one food except on Peaceful. With natural regeneration enabled, full food and positive saturation allow a heal every 10 ticks based on saturation (up to one health point per event); food>=18 supports the slower one-point heal per 80 ticks. At zero food the starvation check runs every 80 ticks: its health floor depends on difficulty (normally 10 points Easy, 1 Normal, lethal on Hard). Peaceful has additional player-level behavior; do not implement its complete policy from the FoodData branch alone.

Ordinary sprint food eligibility is **food>6 points**, i.e. above three drumsticks, with other mobility/mode/item checks. Low health itself is not the ordinary food cutoff. The [official introductory health guide](https://www.minecraft.net/en-us/article/health-minecraft) explains the visible heart and bubble conventions but includes a misleading sprint sentence; the pinned 26.3 `FoodData.hasEnoughFood` and `LocalPlayer` eligibility logic are the authority here. Avoid adding a generic stamina bar while aiming for Java parity. Sources: `FoodData.tick:32`, `hasEnoughFood:92`; `Hud.extractArmor:824`, `extractFood:967`; `LocalPlayer` sprint logic.

## 4. Air: remaining underwater time and recovery

Air tracks the player's **eyes in water**, not whether the feet or collision body touch water. The default maximum is **300 ticks**. With no modifiers and normal 20 Hz simulation, air decreases by one per tick, giving **15 seconds to zero**. A drowning hit occurs when it reaches **-20**; the server resets air to zero and applies **2 health points (one heart)**. Thus the first hit is about **16 seconds after submersion from full air**, followed by one hit per second under those conditions. This is not a universal countdown to death: armor bypass rules, effects, healing, current health, server tick rate and modifications can change outcomes.

Recovery normally adds **4 air per tick**, capped at maximum. From zero it takes 75 ticks, **3.75 seconds** at 20 Hz. Negative air can take slightly longer. The meter remains visible after surfacing until full, so immediately hiding the widget when underwater becomes false is incorrect. Source: `Entity.getMaxAirSupply:2909`; `LivingEntity.baseTick:447`, `shouldTakeDrowningDamage:515`, `decreaseAirSupply:597`, `increaseAirSupply:609`.

Respiration is represented through the oxygen bonus attribute. Positive bonus B gives a depletion probability `1/(B+1)` each tick. For ordinary Respiration levels, the included enchantment JSON adds the corresponding bonus. Expected time increases, but the result is stochastic; do not promise an exact timer such as 60.000 seconds. Water Breathing and Conduit Power prevent ordinary drowning depletion and refill lost air. **Breath of the Nautilus prevents depletion but, on its own, does not refill lost air underwater.** Invulnerability, breath-capable entities and bubble columns are additional branches. Source: `MobEffectUtil.hasWaterBreathing:43`, `shouldEffectsRefillAirsupply:47`; included `data/minecraft/enchantment/respiration.json`.

For rendering, clamp air `A` to `[0,M]`. Compute `full=ceil((A-2)*10/M)`, `pop=ceil(A*10/M)`, and `empty=10-ceil((A+delay)*10/M)`, where delay=1 only for nonzero air while underwater. For cells numbered 1..10 from the right: draw full when index<=full; otherwise draw bursting at the pop position only underwater and when full!=pop; otherwise draw an empty texture at the appropriate trailing cells. The pop window and delayed empty state are deliberate. At M=300 each bubble represents about 30 unmodified ticks (1.5 seconds), with the boundary transitions above.

At zero, all ten empty bubbles remain, wobbling on even HUD ticks by 0..1 GUI unit. Bubble pops also trigger a sound with a per-bubble guard, volume `0.5+0.1*max(0,empty-2)` and pitch `1+0.1*max(0,empty-4)`. The last-played guard resets outside water. In Unreal trigger this from presentation state transitions, not every paint invocation. The sound itself is not a PNG and is not extracted here.

Normally air y=`H-49`. With mount hearts, y=`H-49-10*(mountRows-1)`; two rows move it to H-59 and three to H-69. Do not lift air for extra player hearts on the left. Source: `Hud.extractAirBubbles:915`, `getAirBubbleYLine:945`, `getCurrentAirSupplyBubble:950`.

## 5. Hotbar, offhand and dynamic item information

The base frame is one PNG; the nine items are independent renderings. A selected border moves in 20-unit steps. Main-hand preference controls which side gets the offhand frame, and an empty offhand removes it. Item pickup/pop animation temporarily scales individual item rendering; it does not resize the hotbar's layout. Stack sizes, name, rarity, custom-name italic style, durability, cooldown and enchantment glint all depend on item state.

Item count is drawn only when count!=1 or explicit count text is supplied; its origin is `(itemX+17-textWidth,itemY+9)`. The durability background is a procedural black 13×2 rectangle at `(itemX+2,itemY+13)` with a 1-unit-high colored fill of the item's computed width. The cooldown is a translucent procedural rectangle from `itemY+floor(16*(1-cooldown))`, with height `ceil(16*cooldown)` and width 16. These are not missing PNGs. Items can use generated icons, block meshes, entity models, tint, animation and property overrides; collecting one `textures/item/*.png` per item would not reproduce that renderer.

Selected-item text normally starts at H-59 (H-45 when the mode cannot hurt the player). Its timer is 40 ticks multiplied by the notification-display-time option, reset on changed item/name; it fades in its last ten ticks. The inventory texture atlas does not contain the actual selected item name. Sources: `Hud.extractSlot:1145`, `tick:1180`, `extractSelectedItemName:632`; `GuiGraphicsExtractor.itemDecorations:939` through `itemCooldown:965`.

## 6. XP, locator and mount jump share one bar

The context bar has one 182×5 allocation at H-29. Implement its selection as one state machine:

1. If waypoint data is available and a jumpable mount is currently charging or cooling down, show mount jump.
2. Otherwise, with waypoint data, show XP if the mode supports XP and its recent-display timer is active; show locator in other cases.
3. Without waypoint data, a jumpable mount selects mount jump even while idle.
4. Otherwise show XP in a mode supporting XP, or empty.

The XP preference window lasts 100 player ticks, normally five seconds, following applicable experience updates; initial packet setup is specially suppressed by the sentinel. The level number is rendered separately whenever the mode supports XP and level>0, including while locator or jump owns the track. Do not hide the number just because the green track disappears. Sources: `Hud.nextContextualInfoState:1341`, `willPrioritizeExperienceInfo:1332`, `LocalPlayer` experience update handling.

XP fill is `floor(183*p)` clipped from the left; `p` is within-level progress. XP needed for next level is `7+2*L` below 15, `37+5*(L-15)` for 15..29, and `112+9*(L-30)` at 30+. The level is not simply total XP divided by a constant. Source: `ExperienceBar.extractBackground`; `Player.getXpNeededForNextLevel:1512`.

Mount fill and boss fill use Java's discrete interpolation: for positive normalized p, `floor(181*p)+1`; at zero, zero. At full p it reaches 182. A positive jump cooldown draws the full cooldown sprite instead of the charge fill. This bar is unrelated to normal first-person jumping.

Locator sprites include five distance/style dots, two animated direction arrows and one background. The background PNG is **12×5** with a native **nine-slice** definition (left/right 5, top/bottom 1), stretched through that rule to 182×5. The arrow sheets are **7×10** but render one **7×5** frame at a time, with frame durations **10 then 4 ticks**. Dot variants are selected by distance, not cycled as animation frames. Default style uses near=128 and far=332 blocks; the bowtie style overrides near to 64. Read the included style JSON and `WaypointStyle.sprite` for intermediate frame selection.

Dots draw at `ceil((W-9)/2)+floor(yawAngle*173/120)`, y=H-31, size 9×9. Only yaw in `(-60,+60]` is shown; the camera entity's own waypoint is excluded. Color comes from the transmitted icon or stable waypoint identity. Above/below arrows draw at dot x+1 and context y-6/+6. Location disclosure, sneaking/invisibility/disguise policy and transmit/receive range belong to the server waypoint system. Feed approved waypoints to the HUD instead of enumerating all players locally. The [official locator introduction](https://www.minecraft.net/en-us/article/test-the-new-player-locator-bar) is historical context; the 26.3 classes and metadata pin the formulas used here.

## 7. Crosshair and attack indication

The crosshair is a 15×15 GUI sprite, not a font '+'; the existing Unnamed fixture currently uses text. It shows in first person, with a spectator interaction-target gate and a separate 3D debug-crosshair condition. Vanilla uses `BlendFunction.INVERT` for both crosshair and its attack indicator. A white alpha-blended UMG Image will match shape but not background-dependent color. Treat any approximation as an explicit visual difference, especially against bright sky, snow and black surfaces.

Attack indicator preferences are Off, Crosshair, or Hotbar. Crosshair recharge uses a 16×4 background/fill and clipped width `floor(17*strength)`. The 16×16 ready marker appears only with full charge, a living/alive target, item attack delay>5 ticks, and any item attack-range condition satisfied. It does not remain on screen whenever the cooldown reaches zero. The hotbar option uses an 18×18 vertical meter filling from the bottom with `floor(19*strength)`, positioned opposite the offhand frame. The delay is `20/attackSpeed` reference ticks, and strength is the clamped elapsed-ticks ratio. This represents attack recovery, not held-use progress, block-breaking cracks or stamina. Sources: `Hud.extractCrosshair:441`, `extractItemHotbar:613`; `Player.getCurrentItemAttackStrengthDelay:1727`; `RenderPipelines.CROSSHAIR:1162`.

## 8. Effects, bosses, and peripheral HUD

Effect tiles occupy top-right rows. Beneficial effects use y=1; **neutral and harmful** effects use y=27. Demo mode offsets these down by 15. Each row steps left 25 units; its 18×18 icon is inset three units inside a 24×24 frame. Ambient flags select the ambient frame independently of benefit/harm. `showIcon=false` suppresses the icon. A screen that already shows active effects can suppress these HUD tiles. The final 200 ticks of nonambient effects use source-defined pulsing alpha; ordinary corner tiles do not print duration labels. Instant Health/Damage PNGs exist although a normal instantaneous application is not a long-lived timer tile. `PNG-GUIDE.md` explains all 40 icons individually. Source: `Hud.extractEffects:490` and `MobEffects` registry.

Boss bars use seven color pairs and four notch pairs (6,10,12,20 segments). Draw color background and optional notch background; then clip color progress and notch progress to the same width. They stack at y=12,31,50… with names nine units above each bar. The source stops adding bars once its next vertical offset reaches H/3. Each bar corresponds to a server boss event ID, not necessarily a currently visible boss mesh. Source: `BossHealthOverlay.extractRenderState:60`.

| Peripheral item | Placement and behavior | Texture versus runtime |
|---|---|---|
| Chat | Bottom-left, with configurable scale, width, line spacing, opacity, focus and history; message visibility/fading independent of survival resources | Text glyphs and procedural backgrounds; supporting markers only are PNGs |
| Scoreboard sidebar | Right edge; at most 15 visible entries. width measured from title/name/score; bottom=`H/2+(entryCount*9)/3` | Runtime team styling, values, text and background rectangles |
| Player list (Tab) | Top-center table sized from player names, scores, columns and optional header/footer | Runtime names/skins/scores plus six latency icons |
| Latency | On each player-list row | Unknown if latency<0; ping_5<150ms; ping_4<300; ping_3<600; ping_2<1000; otherwise ping_1 |
| Subtitles | Bottom-right around H-35, rows upward, directional indicators | Localized sound captions, timed fading and procedural backing; no subtitle-message PNG |
| Action-bar message | Centered near H-72 text origin | Transient runtime text, optional hue animation; fade over final 20 ticks |
| Title / subtitle | Center of viewport; title drawn at 4× and subtitle at 2× relative text scale | Default 10/70/20 tick fade-in/hold/fade-out; content is text |
| Spectator selection | Bottom hotbar position, sliding/fading by its activity timer | Same frame, spectator control PNGs, dynamic player heads and actions |
| Toasts | Top-right transient queue | PNG backgrounds/tutorial cues; runtime title/body/item/skin and movement |
| Saving indicator | Bottom-right at x=`W-textWidth-5`, y=`H-14` | 'Saving…' text with state-driven alpha, not a disk PNG |
| Debug / F3 | Dedicated overlay and separate debug scale | Runtime diagnostics/charts; preserve existing project F3 behavior |
| F3+F4 mode switcher | Centered temporary interface | Included background/slot/selection; actual mode permission and item cues are separate |
| Tooltips | Bounds computed from text and viewport; normally associated with a screen interaction | Two sliced sprites plus dynamic content; not the selected-item-name HUD label |

Sources: `Hud.displayScoreboardSidebar:681`, `extractTitle:359`, `extractOverlayMessage:329`, `extractSavingIndicator:1315`; `ChatComponent`, `PlayerTabOverlay`, `SubtitleOverlay`, `SpectatorGui`, `ToastManager`. The adjacent mode-switcher assets are inventoried; its full interaction behavior is not part of the core 130-reference audit.

## 9. Camera overlays and draw order

Vignette, pumpkin camera mask, freezing border, spyglass, nausea, underwater texture, fire and portal overlay belong to camera presentation. Their PNGs alone do not encode the entire visual effect. Source order in `Hud.extractRenderState` starts with camera overlays, crosshair, then hotbar/decorations, effects and bosses; later strata contain sleep shading, messages, titles, chat, player list and subtitles. Sleep shading is a procedural full-screen color and is handled outside the normal hidden-HUD gate. Debug and saving paths have their own entry points. F1 therefore should not be implemented by blindly deleting every UI surface.

The scope uses a shaped opening and procedural black outside; its scale approaches the source target over time. Freezing opacity follows percent frozen. Camera overlays for equipped items are data-driven through equipment components. Portal uses an animated block texture; first-person fire uses a fire atlas sprite; suffocation can use the obstructing block's surface; underwater combines texture/rendering with the world camera environment. The included fire_0 is a related companion asset; fire_1 is the inspected first-person fire reference. Totem activation is a timed item-model animation, so the included totem PNG is supporting art only. Damage camera tilt and world/entity damage flash are also distinct from heart blinking.

Do not apply a universal pixel-icon import preset to all overlays: the native metadata explicitly requests blur for several large textures. Preserve alpha, blend, tint, UV animation, aspect ratio and source timing. Sources: `Hud.extractCameraOverlays:271`, `extractSpyglassOverlay:1042`, `extractVignette:1066`, `extractPortalOverlay:1106`, `extractConfusionOverlay:1118`; `ScreenEffectRenderer`; included MCMeta.

## 10. Game-mode and visibility policy

| State | Core behavior |
|---|---|
| Survival / Adventure | Normal hotbar, player health/food/armor/air policy; XP-capable; relevant effects/overlays |
| Creative | Hotbar remains; survival health/armor/food/air block and XP/level are absent; context state can still depend on locator/mount data |
| Spectator | Spectator selector replaces ordinary item hotbar; ordinary survival stats and XP absent; crosshair only for eligible interaction target in first person |
| F1 HUD hidden | Normal camera HUD/crosshair/decorations/effects/boss/message/chat paths gated; source has sleep/subtitle exceptions |
| Third person | Ordinary crosshair and first-person equipment masks gated; survival status cluster still has meaning |
| Inventory/pause/menu | Manage screen focus and per-screen effect visibility; do not let a passive HUD consume mouse/keys or replace pause/menu input policies |

Vehicle health is called separately from the player-health gate; do not assume every mount element is implicitly Survival-only. A future implementation must derive capability flags from the actual player mode and world state. Unreal's `AGameMode` class name is not the player's Survival/Creative state. Source: `Hud.extractHotbarAndDecorations:541`, `MultiPlayerGameMode.canHurtPlayer:118`, `hasExperience:557`.

## 11. Recommended Unreal implementation for this project

The live foundation is already native C++/Slate: `Source/Unnamed/Foundation/FoundationController.cpp` creates a text crosshair, diagnostic text and temporary hotbar text through `UFoundationDiagnostics`. `FoundationActions.h` explicitly labels its slot values as a typed test double with **no inventory, damage, loot or block mutation**. `FoundationSimulation.h` has a future food/mobility eligibility boundary but no survival-resource model. The menu has its own browser-based implementation. These facts were read from the current project, not assumed from the older Minecraft 2.0 project.

Use a **local-player-owned native Slate HUD**, with optional UMG wrapper/widgets where authoring benefits outweigh complexity. A compact custom leaf widget can issue batched sprite/text draws for repeated 9×9 cells using cached geometry and a pure draw-list builder. This fits the existing native foundation. UMG with one root layout and shallow reusable widgets is also viable; do not port the research HTML into the gameplay WebBrowser. Epic documents [in-game Slate](https://dev.epicgames.com/documentation/en-us/unreal-engine/using-slate-in-game-in-unreal-engine).

Separate responsibilities:

1. **Gameplay facts:** health/max health, absorption, food/saturation, armor, current/max air, eyes-underwater, effects, XP, items, mount and mode. Standalone uses the same authority boundary as future multiplayer; clients do not award health, food, items or XP by widget action.
2. **HUD snapshot:** immutable, coherent data with IDs/revisions and capability flags. Use a fixture adapter for absent systems, visibly labeled in the preview lab. Never present fixture hearts as proof that drowning or damage exists.
3. **Presentation state:** last/displayed health, blink timers, selection/name timers, bubble-pop guard, effect fade, locator projection. Update from changes or bounded active animations.
4. **Renderer/layout:** deterministic rectangles, brushes, UV clipping, text and z-order. This layer does not query all actors, mutate gameplay or write SaveGames.

Import selected PNGs through an Editor-only script into an additive HUD content folder. Maintain source path → asset path → native dimensions → frame/slice metadata in a data asset/registry. For small pixel sprites, use nearest filtering, UI-oriented compression with alpha retained, no mipmaps, sRGB for ordinary color art, clamped addressing, no unrequested power-of-two rescale and deliberate residency. Confirm exact settings in the installed engine before scripting them. Preserve `.png.mcmeta` rules in the registry. [Epic's texture editor reference](https://dev.epicgames.com/documentation/en-us/unreal-engine/texture-asset-editor-in-unreal-engine) documents filtering, mip, color-space, alpha and addressing controls. The larger blurred overlays and tiled animated world sprites need separate material presets.

Resolve **one final scale** from Java GUI scale, viewport framebuffer size, Unreal viewport/Slate geometry and Windows DPI. Compensate for already-applied Slate DPI; never multiply two independent auto-scales. Keep the crosshair anchored to true view center and status groups to the bottom center. Round geometry in the final pixel space. A user-adjustable safe-area variant can be added later, but vanilla-reference comparison uses zero extra padding. [DPI scaling](https://dev.epicgames.com/documentation/en-us/unreal-engine/dpi-scaling-in-unreal-engine) and [safe zones](https://dev.epicgames.com/documentation/en-us/unreal-engine/umg-safe-zones-in-unreal-engine) explain Unreal's relevant systems.

For crosshair inversion, prototype the smallest compatible compositing path and inspect it against bright/dark scenes. A post-process solution can sample scene color, but must handle viewport sizing, placement in the post-process chain and interaction with other UI; a standard UMG UI material is not automatically equivalent. Avoid a render target per icon. [Epic's post-process material documentation](https://dev.epicgames.com/documentation/en-us/unreal-engine/post-process-materials-in-unreal-engine) is the starting point, not proof that any chosen implementation matches the Java pipeline.

Use event-driven updates, cache stable layout and minimize widget construction. Isolate short animations; measure before adding a Retainer Panel, large atlas, complicated batching or custom renderer. [Epic's UMG optimization guidance](https://dev.epicgames.com/documentation/en-us/unreal-engine/optimization-guidelines-for-umg-in-unreal-engine) favors changes driven by events over polling property bindings and advises shallow layouts. [UI invalidation](https://dev.epicgames.com/documentation/en-us/unreal-engine/invalidation-in-slate-and-umg-for-unreal-engine) supports caching unchanged content. These are recommendations; no performance claim is made without measurement.

## 12. Phases and acceptance

**First implementation milestone:** verified import/registry, pure layout and cell-selection functions, a controlled HUD preview state source, and native integration with current slot selection, offhand side, crosshair, F1 and gameplay focus. Preserve F3, existing controls (Left Shift sprint / C crouch), menu, movement, maps and settings. Show missing survival data as an explicit lab fixture, not as a silently invented production backend.

**Next milestone:** only with the necessary systems in scope, connect authoritative survival, inventory, effects and XP adapters. Agree on mode/persistence/death behavior as gameplay work. Locator networking, mount systems, boss events, dynamic item model rendering and communication overlays are separately identifiable dependencies. Keep all their assets and presentation contracts ready without pretending the features already exist.

| Validation | Required cases |
|---|---|
| Pure geometry | 320×240, 427×240, 640×360 and odd logical sizes; left/right offhand; 9 slots; center rounding; extra health/mount rows |
| Health state | 0,1,2,3,4,19,20; fractional health; odd max; multiple rows; absorption loss; healing during blink; poison+wither; frozen; Hardcore |
| Air | A=300,299,272,271,270,269,2,1,0,-1,-19,-20; underwater versus surface; refill; modifiers; one/two/three mount rows |
| Context state | All waypoint/XP timer/mount charge/cooldown/mode combinations; level text remains independent |
| Items and effects | Empty/full stacks; count1; durability; cooldown; nine slots; left-handed offhand; 200-tick effect threshold; hidden/ambient/neutral icons |
| Native input | F1, F3, Tab, Escape, inventory/chat, rebinding, alt-tab, pause/resume, cursor capture; passive HUD never steals focus |
| Visual | PNG edge sharpness; alpha; cropping; fonts; MCMeta arrows/9-slice; crosshair inversion; bright/dark scenes; 720p/1080p/1440p/4K, ultrawide, GUI scales and Windows DPI |
| Runtime | Editor and packaged standalone; only then listen/dedicated if a multiplayer backend is in scope; reconnect/respawn/late join when supported |
| Performance | Warm, same-scene HUD-off/on CPU/GPU/frame-time measurements; Slate/Unreal Insights; idle/animation/stress states; report deltas, not guesses |

A layout-unit test does not prove rendering. A decoded PNG does not prove a correctly cooked texture. An editor compile does not prove native input, gameplay authority, multiplayer or packaged acceptance. The next agent should retain separate evidence for each gate and deliver a package only after the relevant native checks.

## Sources and provenance index

- [Official 26.3 release notes](https://www.minecraft.net/en-us/article/minecraft-java-edition-26-3): version context and 26.3 UI/input changes.
- [Mojang 26.3 metadata](https://piston-meta.mojang.com/v1/packages/702fe59163c6ee6578607daa85811d9bc9c7cc40/26.3.json): client and asset-index identities; downloaded and saved in evidence.
- [Official vanilla client](https://piston-data.mojang.com/v1/objects/e877b6a07acd633fb3bb475002175cec036e7b87/client.jar): primary source of the inspected behavior and most PNG bytes.
- [Official asset index 34](https://piston-meta.mojang.com/v1/packages/1e4e4a68dece1c0f51b8f93b45da8fe8da51bcd3/34.json): Unicode dependency resolution.
- [Official health introduction](https://www.minecraft.net/en-us/article/health-minecraft): broad presentation context; exact rules above come from the pinned client.
- [Official locator introduction](https://www.minecraft.net/en-us/article/test-the-new-player-locator-bar): historical feature intent; exact 26.3 behavior comes from pinned classes.
- Epic documentation links appear beside the associated implementation recommendations.
- `evidence/source-map.json`: local primary-code paths, hashes and method declarations; `evidence/reference-coverage.json`: bounded fixed-asset coverage; `manifest.json`: original byte provenance for every extracted asset.

Minecraft artwork remains Mojang/Microsoft material; extraction and a local research folder do not grant a redistribution license. Preserve this provenance and use appropriate original/licensed replacements for distribution decisions. The prose, diagrams and Unreal architecture recommendations are newly authored; no decompiled Java is copied into the project deliverable.
