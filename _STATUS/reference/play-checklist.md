# Play checklist for Jesus

What to try once the PM moves Unnamed-play to feature/survival (with his game closed). One block per landed feature, from the SMs' PLAY messages.

## T2 step 4: throw, merge, despawn (landing 4, 5fd40df)
- In a survival world, hold a stack and press Q (throws one; about 2 s before you can pick it back up) and Ctrl+Q (whole stack; a big stack draws as up to 5 copies).
- Throw two of the same item close together: they merge after about 2 s.
- Throw a damaged tool and pick it up: its wear is kept. Items left 5 minutes vanish. Creative Q also throws.
- Known: a block placed on a drop doesn't push it out yet; throw speed and arc are estimates (now in the value report, swapped later).

## T1 step 5b: survival inventory screen (landing 6, 38399ad)
- In survival, press E: Minecraft's own inventory screen (armor slots, offhand, 27 slots, hotbar). Try click, shift-click, number keys, drag-spread and Q over a slot. Known until step 6 (Codex review 6): double-click collect doesn't fire yet, a one-slot drag clicks the wrong slot, and the inventory keys ignore rebinding.
- The player-model box is empty on purpose (first-person-only rule). The recipe-book button is drawn but does nothing yet.

## T3 step 14: axe stripping (landing 5, 366458b)
- Right-click a log, wood, stem, hyphae or bamboo block with an axe: it strips, keeping its direction; the axe loses 1 durability (none in creative).

## T2 step 12: XP orbs (landing 8, 9fd38bf)
- In survival, mine coal, redstone, lapis, diamond or emerald ore with the right pickaxe: small green-yellow orbs pop out, fly to you within 8 blocks and fill the XP bar; the level number rises (a sound every 5 levels). The pickup sound plays.
- Iron, copper and gold ore give none (vanilla); creative breaks give none. Furnaces don't pay XP yet (T1 8a).
- Known: orb spawn scatter and pickup pitch are estimates.

## T3 step 10b: eating polish (landing 9, 045169b)
- Eat bread or an apple: the held food now moves like vanilla's (a small jiggle, then up toward the mouth), crumbs fly, and the vanilla eat sound plays.

## T1 step 6: 2x2 crafting (landing 10, 079fe3e; also fixes 5b's known issues)
- Press E in a survival generated world. Mine some blocks: pickups fill the hotbar first, then the main grid.
- In the inventory: left/right-click, shift-click (main ↔ hotbar), drag to spread (left even, right one each), double-click to collect, number keys and F over a slot.
- Put a log in the 2x2 grid: 4 planks show. Take them, or shift-click to craft all. Craft 2 planks into 4 sticks, then 4 planks into a crafting table.
- Close with items left in the grid: they return to the inventory.
- Not yet: Q and clicking outside the screen don't throw (T2 wires those); the crafting table doesn't open (step 7); the backdrop isn't blurred.

## T2 items-fixes (landing 11, 229528f)
- Drop an item (or break an ore for XP orbs), then place a block on it: it slides out to the nearest open side.
- Throw two stacks of 40 side by side: they stay 40 and 40 (merge only if they fit in one stack).
- In the inventory (E), click outside the window with a stack held: it's thrown. Put items in the 2x2 grid and close: what doesn't fit back is thrown at your feet.
- Known: next to a slab or fence the item may pop upward instead of sideways.

## T1 step 7: crafting table (landing 12, 896f2d3)
- Craft a crafting table in the 2x2 grid, place it, right-click it: Java's 3x3 crafting screen opens.
- Make a wooden pickaxe (3 planks over 2 sticks), then a stone pickaxe. Spam-click a result: each click crafts.
- Sneak with an item in hand and right-click the table: it places the block instead.
- Close with items in the 3x3: they return to the inventory; what doesn't fit drops.
- Not yet: the screen doesn't close when you walk away; the backdrop isn't blurred.

## T3 step 17: buckets (landing 13, 9efa225)
- `/give @s bucket`, go to a lake, right-click water to fill; right-click the ground to pour a source and watch it flow.
- Fill from lava, pour next to water: cobblestone/obsidian form with a hiss.
- With a full bucket, aim at a crafting table: it opens the table; sneaking pours beside it.
- (T2 items-swap, landing 14, 22b3b97) Mine several coal ores in one spot: XP orbs stay separate more often (vanilla merge rule) and bounce a little after a fall; the level-up sound is quiet at low levels, louder toward 30.
- (T5 B1, landing 15, 4b826e4) In a creative world, console `foundation.Blocks.Families 1`: the creative tabs fill with 310 new blocks (stairs, walls, fences, panes, doors, trapdoors, buttons, ladders, 16 beds, chests, crops, saplings, cauldrons, campfires). Each places in its default state only (no connections, opening or climbing yet). Look at bed icons, the chest stand-in box, campfire/fire animation. Known: the water cauldron renders see-through.
- (Run 2, T5 B2a, landing 16, 632a746) With `foundation.Blocks.Families 1`: fences, walls and fence gates block you at 1.5 blocks tall (you can't jump over them); glass panes are see-through in generated worlds; a sapling in the offhand works at `foundation.Blocks.Families 0`; a planted sapling mined in survival drops itself. Placement shapes (stairs turning corners, fences connecting) come in B2b.
- (Run 2, T3 16a, landing 17, e2c3dc5) Survival: walk into lava (you burn, take damage, and keep burning after you leave); jump into water while burning (it goes out with a hiss). Sprint into deep water: you swim in a low 0.6-tall pose and fit through 1-block gaps underwater. Pour water onto a waterlogged slab or next to a torch. No death screen yet (T4 11 next).
- (Run 2, T1 8a1, landing 18, f673fa9) No furnace screen yet (8a2), so nothing to play directly. Change you can feel: with the inventory or a crafting table open, the world keeps running (hunger, burning, falling items), and a crafting table's screen closes when you walk away or the table breaks.
- (Run 2, T6 20a, landing 19, 8050f86) **Cows.** In a survival world, open the console and type `foundation.Mobs.Summon cow` (a cow appears 3 blocks ahead; `foundation.Mobs.Clear` removes them), works in the Playground too. Watch it wander and look at you. Hit it: it flashes red, gets knocked back and panics. Kill it: it rolls onto its side, then drops beef, leather and XP orbs. (Codex 19: mobs don't take fall, lava or fire damage yet, so no cooked beef; fixed in 20b.) `foundation.Mobs 0` turns mobs off. Known (SM-Mobs): no natural spawning yet (20b); the red tint may read stronger than vanilla; head turns and walk speed are provisional. Pig, sheep, chicken, zombie, creeper models are in the data but not spawnable until 20b-20d.
- (Run 2, T4 11a, landing 20, fc7b051) **Death.** In survival, die (lava, a fall, or `/kill`): the red "You Died!" screen with your score, Respawn and Title Screen buttons; your items and some XP drop where you died; chat shows the vanilla death message ("tried to swim in lava", "burned to death", "fell from a high place"...). Respawn puts you back at spawn. Also the Codex 3 chat fixes (Allow Cheats gate, `/time add` negative values, `/kill @e`).
- (Run 2, T5 B2b, landing 21, 60d2f90) With `foundation.Blocks.Families 1`: place stairs next to each other and they turn corners (inner and outer); fences, walls, glass panes and iron bars connect to each other and to full blocks; walls grow a post and go tall under a block; fence gates sit lower inside a wall. Breaking a neighbour updates the shape. SM-Blocks' list (creative, `foundation.Blocks.Families 1`, `foundation.Blocks.Shapes 1` is the default): (1) oak stairs in an L: the corner becomes an outer or inner stair, and stairs face the way you look; (2) oak fences, cobblestone walls, glass panes and iron bars in a line beside stone connect, and breaking one updates the neighbours; (3) jump against a fence, wall or closed gate: 1.5 tall, you can't clear it; (4) stained glass panes are see-through in a generated world; (5) mine a planted sapling in survival: it drops itself.
- (Run 2, T1 8a2, landing 22, dcd2bce) **Furnace.** Craft a furnace (8 cobblestone), place it, right-click: Java's furnace screen. Put logs or coal in the bottom slot and raw iron, sand, cobblestone or a log on top: the flame burns down and the arrow fills; take the result for XP. Logs smelt into charcoal now; cows' beef cooks. Shift-click routes fuel vs input. Contents last for the session only. XP orbs pop at your feet when you take the result. Walk away or break it with the screen open: the screen closes (breaking drops the contents). Air, hunger and burning keep running while it's open. Not yet: lit look and light (8b1, T5 B3a), smoker and blast furnace (8b1), particles (8b2).
- (Run 2, T6 20b1, landing 23, 4625b57) **Farm animals.** Console `foundation.Mobs.Summon pig` / `sheep` / `chicken` (and `cow`); hold wheat (cow, sheep), a carrot (pig) or seeds (chicken) and they follow you, stopping close; build a wall or a 1-block step and watch them path around or up it (a fence stops them); drop a chicken from a height (it flaps and falls slowly); wait near a sheep on grass (it lowers its head and the grass turns to dirt); a chicken lays an egg every 5-10 minutes
- (Run 2, T4 11b, landing 24, bf7a93e) **Hurt and fire.** Take a hit: the camera tilts and you hear the hurt sound; die and the camera rolls with a death sound. While burning, the fire overlay shows in the lower corners (provisional look). In chat, typing stops at 256 characters and old lines fade after about 9 s. The red full-screen tint is off (`foundation.Hurt.ScreenTint 1` turns it on)
- (Run 2, T1 8b1, landing 25, 4ee7f17) **Lit furnaces, smoker, blast furnace.** Fuel a furnace and its front lights up and crackles; place a smoker (food) and a blast furnace (ores): each opens its own screen and cooks twice as fast; shift-double-click on a furnace slot no longer pulls your inventory back in. Not yet: light 13 (T5 B3a), particles (8b2)
- (Run 2, T6 20b2a, landing 26, bfc11db) **Animals in the world.** Create a new survival world and walk into fresh terrain (plains or forest): new chunks may come with packs of cows, pigs, sheep or chickens (about 1 chunk in 10; none in the Playground); push a cow off a 4-block drop (it takes damage) or lead one into lava or fire (it burns, and a burning kill drops cooked beef); kill any mob and its drops and XP appear on the killing hit; the red hurt tint keeps the hide readable
- (Run 2, T2 19a1, landing 27, 747233b) **Armor groundwork (small).** Put a carved pumpkin in the head slot from the survival inventory: the equip sound plays. Drop items against a wall, or place a block on a dropped item: they slide out by vanilla's measured rule. XP orbs merge and level-up sounds follow the lab values. Armor pieces arrive in 19a2
- (Run 2, T5 B3a, landing 28, 8c07620) **Doors, trapdoors, gates.** In survival, place oak doors in a row with stone and other doors beside them and note which side the hinges go to; right-click doors, trapdoors and fence gates to open/close them (with sounds); open a gate from its front and it swings away from you; right-click an iron door: nothing happens. Light a furnace: it now glows at light 13
- (Run 2, T1 8b2a, landing 29, 00322d1) **Furnace polish.** Double-click a smelted stack in the furnace output to gather the same item from your inventory; shift-click a crafting result with almost no room: what fits goes in, the rest drops at your feet; press Escape next to a lit furnace and the crackle stops; lit furnaces/smokers/blast furnaces light up the dark
- (Run 2, T6 20b2b, landing 30, 91c4fa8) **Animals keep spawning.** In a new survival world, clear the animals near you (kill them or `foundation.Mobs.Clear`) and wait in loaded plains: every 20 s the spawn cycle may bring new cows, pigs, sheep or chickens 24+ blocks away, until about 10 are around; a sheep standing in a fern or dry grass eats it; pigs and sheep follow a held carrot/wheat through a 1-high gap under a bottom slab
- (Run 2, T2 19a2, landing 31, d6c2641) **Armor.** In creative, open the Combat tab: all 29 armor pieces (leather tinted brown). Right-click a chestplate to wear it: equip sound, and the armor bar above the hearts fills. Take fall or lava damage with and without armor: less damage when worn, and the pieces wear down. Wear a carved pumpkin: the pumpkin blur covers the view
- (Run 2, T1 9, landing 32, 3e7c549) **Barrel.** Place a barrel (it faces away from you), right-click: 27-slot barrel screen; its lid opens with a creak and closes when you leave; shift-click moves stacks both ways; break it and the contents drop
- (Run 2, T5 B3b, landing 33, 5d27196) **Buttons and levers.** Place stone and oak buttons on walls, floors and ceilings, and a lever. A stone button pops back after 1 s, an oak one after 1.5 s, with clicks; the lever flips with its two pitches; a button hung under a snow layer holds
- (Run 2, T2 19b, landing 34, 905954d) **Shield.** Start a new survival world: the hotbar is empty (no starting loadout). `/give @s shield`, hold it in either hand and hold right-click: it raises after a short delay and blocks hits from the front (blocking sound); hits from behind still hurt. An axe hit disables blocking briefly (shield lowers, cooldown). Each blocked hit wears it down (durability bar).
- (Run 2, T6 20c1, landing 35, 540cfd2) **Zombie.** In a generated survival world (not the Playground floor, where mobs can't path), `foundation.Mobs.Summon zombie` a few blocks away: it turns to you, walks over and hits for 3 every second with knockback; let it kill you for "Player was slain by Zombie"; in creative it loses interest; hit it and it fights back; at noon in the open (`foundation.Fire 1`) it burns; kill it for rotten flesh (rarely iron, a carrot or a potato).
- (Run 2, T5 B4a, landing 36, ad045c4) **Block drops, no firefly bush.** In survival, break the bottom of a 3-tall sugar cane (3 drop), a cactus column, bamboo and a door (1 drops). Pour water over sugar cane to wash it away; it drops. In a new world, walk near water in a swamp or forest: short grass where firefly bushes were, and no firefly bush in creative search.
- (Run 2, T1 18-chest, landing 37, 8a5e87e) **Chests.** Place a chest (it faces you), place another beside it facing the same way: they join into a large chest with a 54-slot screen; sneak while placing to keep it single; break one half and the other goes back to single; a stone block on top stops it opening; trapped chests work the same (no redstone yet). Double chests still draw as two boxes and the lid doesn't move (deferred until after QC).
- (Run 2, T4 13a2, landing 38, db4e78c) **Footsteps and bass.** In a generated world, walk on stone, grass, wood, sand: a step sound about every 1.7 blocks (faster sprinting), each block's own sound; sneaking, swimming and flying are silent. On moss carpet or a snow layer: the carpet/snow plus a softer step of the block under it. Options > Music & Sounds: Master Volume now really works; the new Bass slider adds up to +5 dB of low end by default, 0 = vanilla. Try headphones at 0 vs default. Known: the Playground lab floor is silent.
- (Run 2, T5 B4b, landing 39, 8762cf8) **Ladders and climbing.** Place a ladder on a wall and climb it (walk into it or jump); hold sneak to stay put, let go to slide down slowly; climbing resets fall damage. Vines and an open trapdoor above a ladder climb too. Break the base of a twisting vine column: the rest pops and drops.
- (Run 2, T2 19c, landing 40, d784e70) **Bow and arrows.** In survival, `/give @s bow` and `/give @s arrow 16`; hold right-click: the bow steps through 3 pull stages and the view zooms slightly; releasing very early fires nothing. Full draw (1 s) at a wall: the arrow arcs, sticks in the block, and you can walk over it to pick it up (creative: no arrows needed, can't be picked up). Shoot a cow or zombie: damage and knockback; full draws can crit. No arrows in survival: the bow won't draw. Arrows on the ground vanish after about a minute. Known: a bow with no arrows passes the click to the other hand; the hit sound never plays; the draw pose is a simple slide.
- (Run 2, SMEAR, landing 41, 6082982) **No smear.** Walking mobs, thrown/dropped items and the swung sword stay sharp; no motion blur on camera turns (as vanilla).
- (Run 2, T6 20c2, landing 42, 8d16cc8) **Night and spiders.** New survival world, `/time set night` in open plains: zombies and spiders appear in the dark a few dozen blocks out (up to ~70 around you); torch-lit areas stay clear; walk 128+ blocks away and old ones vanish; spiders chase only in the dark, leap from a few blocks and climb walls; at dawn spiders turn neutral and zombies burn (`foundation.Fire 1`); Peaceful removes every monster. `foundation.Mobs.Summon spider` works too.
- (Run 2, T1 8b2b, landing 43, 27d0dfa) **Furnace particles.** Light a furnace: small flames and smoke drift from its front; a lit smoker puffs smoke from its top, a blast furnace from its front; pause (Escape) and they stop; open the furnace screen and they keep going. Small and faint for now (provisional look, on the QC list).
- (Run 2, T3 16c, landing 44, ed35e73) **Fire blocks and cauldrons.** In survival, place fire (flint and steel, or `/setblock ~ ~ ~1 fire`) and walk in: 1 HP every half second, and after a second you catch fire and keep burning after stepping out; soul fire hurts 2 HP; armor reduces it; water puts you out. Place a cauldron: a water bucket fills it (empty bucket back), an empty bucket takes the water back; same with lava; standing in a lava cauldron hurts like lava; a different fluid replaces the contents; sneak + right-click places the fluid instead. Known: mobs ignore fire blocks, fire doesn't spread or burn out, a water cauldron doesn't put out a burning player.
- (Run 2, T5 B5+B6a, landing 45, 20435b2) **Farmland and crops.** In survival: a hoe on grass/dirt makes farmland, a shovel on grass makes a dirt path; a block placed on farmland or a path turns it to dirt. Plant wheat seeds on farmland next to water and watch them grow over a few minutes; farmland away from water dries, then turns back to dirt if empty. Creative-place a pumpkin or melon stem on farmland: it grows, changes tint with age, and drops a fruit beside it. Only wheat seeds plant for now.
- (Run 2, SMEAR2, landing 46, 0cb8d9b) **Sharp arrows.** Shoot arrows across the view: they stay sharp in flight (no ghost specks).
- (Run 2, T6 20e1, landing 47, c79ee9a) **Sheep colours, milk, variants.** In a new survival world, a flock is mostly white with some black, grey and brown, rarely pink; right-click a sheep with green or lime dye to recolour it; kill a sheep for wool of its colour; right-click a cow with an empty bucket for milk; in snowy or desert biomes find cold/warm cows, pigs and chickens with their own looks (`foundation.Mobs.Farm 0` turns it all off). Shearing needs a shears item, which doesn't exist yet.
- Adventure mode does nothing with buckets (`/gamemode adventure` is refused; use the vitals debug selector).


## QC replay: Q01 (landing 48, a5b8f3a1)

- Raise a shield facing a summoned zombie on Normal, then on Hard: the hits are blocked (no damage, the block sound), and the shield wears more on Hard.
- Take zombie hits from the left, then the right: the camera tilts toward the side of the hit.
- At 2 HP, eat an apple and let a zombie land the killing hit as the bite finishes: the apple is among the death drops.

## QC replay: Q03 (landing 50, eb0347df)

- Nothing to replay in game: test and probe fixes only.

## QC replay: Q24 (landing 51, 070151ed)

- Nothing to replay in the editor play copy; the packaged game now starts (checked in the exit bar's packaged sessions).

## QC replay: Q04 (landing 52, d819eec0)

- Creative inventory: pick up a stack, click outside the window: the stack is thrown as an item, not deleted.
- Left-drag a stack over a few slots, tap the right button mid-drag: the drag is cancelled.
- Rebind the inventory key to I (and a hotbar key to a mouse thumb button, if you have one); open creative and use them.
- Creative, open a chest, middle-drag a stack across empty slots: each slot gets a full copy.

## QC replay: Q06 (landing 53, 568d0b74)

- Set Master Volume to 0 in the menu, open a world: the game stays silent (and the slider still shows 0). Set it back.
- Set Bass to 50%, restart the game, open a world: the bass is at the 50% level from the first sound.
- Walk across lily pads on water: you hear steps.
- Walk along a platform edge, and along a shoreline with half your body over water: steps keep playing.

## QC replay: CODEX-LOW-01 (landing 54, eb52cea7)

- Place a button and lever on supported blocks, set `foundation.Blocks.Interact 0`, then remove each support: the attachments pop instead of floating. Restore the switch afterward.
- In creative, break the upper and lower halves of two-half plants (for example sunflowers): the complete plant disappears without item drops. In survival, repeat and check the normal single drop remains.
- Automated regression/runtime checks and independent review passed; this human replay is still pending.

## Jesus's play reports

- 2026-10-15 ~13:10, build 00322d1. (1) Breaking the bottom of 3 sugar cane pops the other two but drops only 1 item → SM-Blocks (B4 or sooner; check all column plants against R12). (2) Every world and respawn starts with a pickaxe and sword ("since the start of our scratch"); remove the loadout → SM-Player (19a2 or next). (3) A plant draws solid black with glowing dots (probably firefly_bush), screenshot `Unnamed-play\Saved\Screenshots\Foundation00013.png` → SM-Blocks.
- 2026-10-15 ~13:20: Jesus: "remove all created worlds ... start fresh with no worlds for the next build". PM 4 sent all 13 `Unnamed-play\Saved\Worlds\new-world*` folders to the Recycle Bin (game closed). The Playground is built-in and unaffected.
- 2026-10-15 ~13:30: footsteps hard to hear, wants more bass on all sounds. Tap: Bass setting + footstep check → SM-Player (T4).
- 2026-10-15 ~13:35: SM-Player order: 19a2 → 19b (incl. no-loadout) → 13a (bass + footsteps, Codex 20/24) → 19c → 16b → 13b. 19c moves ahead if T6 reaches 20d sooner.
- 2026-10-15 ~13:45 (SM-Player 13a planner): the Music & Sounds sliders do nothing in game today; 13a wires Master (and adds Bass), the other sliders wait. New value request R24 (arrow wiki-only values, skeleton spread, player footstep cadence); ships provisional meanwhile.
- 2026-10-15 ~15:20: SM-Codex open-findings list (codex/open-findings.md) routed: SM-Player 19b/13a/16b/16c (31 #1 #4; 24 #1-#5; 20 #1 #2; 17 #2 #3), SM-Mobs 20c (30 #1 #2, already sent).
- 2026-10-15 ~17:00: held items (e.g. the axe) clip through nearby blocks in tight spaces (digging a 1x2 hole, walls up close); never fixed. Jesus: "Make sure you add on the list to fix. It needs to also be perfect with FOV ... ensure that if we fix the clipping issue it's still consistent with FOV." → QC-KNOWN-BUGS.md via SM-Codex (fix after QC, keep FOV consistency).
- 2026-10-15 ~20:05: nothing saves in a played world (only world.json settings). Not a regression: never built. Tap "First after QC" → top of FINAL-SCOPE.md deferred list.
- 2026-10-15 ~20:15: mobs and moving items smear (texture ghosting). Likely no motion vectors on the instanced mob/item parts, so temporal AA smears them. → QC-KNOWN-BUGS.md via SM-Codex.
- 2026-10-15 ~22:20 idea (Jesus, PM 5 chat): per-body-part hitboxes for mobs (each model part its own hit box following the animation; movement keeps the single box; maybe headshots). Beyond vanilla. "Maybe we can revisit it later once we start majoring changing stuff." Not on any list yet; revisit with the new Steve character / beyond-vanilla mob AI after QC.
- 2026-10-15 ~23:30: the console key ~ didn't open the console for Jesus; tap "F12": PM 5 added `+ConsoleKeys=F12` in Unnamed-play\Saved\Config\WindowsEditor\Input.ini (user config, gitignored, play copy only). F1 stays HUD toggle.
