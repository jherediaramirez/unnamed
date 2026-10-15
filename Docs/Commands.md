# Chat commands

Survival step 15 (lane `feature/survival-feel`, 2026-10-14). Typing `/gamemode`, `/give`, `/tp`, `/time` and `/kill` in the chat (T, or / which opens it with the slash) runs them the way Java 26.3 does, with vanilla's syntax, feedback and error lines. The lines show bottom-left over the HUD.

Switch: `foundation.Chat.Commands` (1). 0 restores the old local fixture: Enter records the text in the F3 event log, nothing runs, and there's no chat list.

## Where it lives

| Part | File | Role |
|---|---|---|
| Parser and commands | `Source/Unnamed/Foundation/FoundationCommands.h/.cpp` | A brigadier-style reader with a cursor. `FoundationCommands::Run(line, context)` returns the chat lines. |
| Chat list | `Source/Unnamed/Foundation/FoundationChat.h/.cpp` | 100 messages, the fade, the switch, vanilla's chat colours. |
| Submit | `AFoundationController::SubmitChat` | Builds the command context: authority, player, vitals, feet, F3's coordinate origin, the pawn's `TeleportFeet`, and the world clock (`AVoxelWorld`; none in the Playground), and the opened saved world's "Allow Cheats" (`FUnnamedSavedWorld::bCommands`; the Playground and the dev world allow them). |
| Panel | `UFoundationPanels::OpenText` | With the switch at 1, the edit box is invisible (it only takes the keys), shown bare at the bottom (`Show(..., bBare)`), and Enter calls `SubmitChat`. With the switch at 0, it is the old centred fixture. |
| HUD | `FHudSnapshot::Chat`/`bChatOpen`/`ChatInput`, `HudDrawList::WrapChat`, `UHudPresenter::Refresh` | The HUD's ASCII font with its shadow, on a 50% black band. The HUD stays visible under the open chat, which also draws through F1 (it still takes the typing), and adds vanilla's input bar with the typed line and a `_` caret. |
| Tests | `Unnamed.Foundation.Commands.*` (`Source/Unnamed/Tests/FoundationCommandsTests.cpp`) | Lang, Parse, GameMode, CheatsOff, Give, Teleport, Time, Kill, Chat. |
| Runtime | `FoundationCommandsProbe.cpp` (`-Suite interaction` and `-Suite views`) | Types `/give @s apple 3`, `/time set night` and a chat line, captures the open and closed chat, then the switch-0 control. |

Gameplay changes go only through things that already exist and are public:
- the player's `FFoundationInventory::Add` and the authority's `SpawnItem` for what doesn't fit;
- `FFoundationVitals::Mode` and `Hurt(Kill, FLT_MAX)`;
- the lab options a saved world's mode sets (`bInfiniteSupply`, `bToolWear`, `bCreativeInventory`, as `AVoxelWorld` applies them);
- `FVoxelWorldClock::RunCommand`, and `AddTicks` and `GetDayClock` (for `add`'s negative times and the already-paused check).

The HUD only draws.

## Text

Every string comes verbatim from the 26.3 jar's `Reference/minecraft-menu/assets/minecraft/lang/en_us.json`. `Unnamed.Foundation.Commands.Lang` compares each one with that file. Without "Allow Cheats" (Hardcore forces it off) every command answers `Unknown or incomplete command. See below for error` with the context line, as vanilla hides commands above the player's permission level, and changes nothing. Error lines follow vanilla's form:
- the message in red;
- then the context line: up to 10 characters before the cursor in gray ("..." when cut), the rest in red, then `<--[HERE]`.

Colours are vanilla's named chat colours: red FF5555, gray AAAAAA.

| Command | Supported | Feedback |
|---|---|---|
| `/gamemode <survival\|creative> [target]` | Mode and creative rules. The mode already set prints nothing (vanilla's silent failure). | `Set own game mode to Creative Mode` |
| `/give <target> <item> [count]` | Item ids from the registry, `minecraft:` optional, count 1 up to 100 stacks. What doesn't fit drops at the feet with no pickup delay. | `Gave 3 [Apple] to Player` |
| `/tp <x y z>`, `/tp <target> <x y z>`, `/tp <destination>`, `/tp <target> <destination>` (also `/teleport`) | `~` relative. Whole absolute x and z get +0.5 (the block's centre). Bounds 3e7 out, 2e7 up/down. | `Teleported Player to 100.500000, 70.000000, -19.500000` (vanilla's `%f`) |
| `/time set <day\|noon\|night\|midnight\|time>`, `add <time>`, `query time\|day\|gametime`, `pause`, `resume` | The world clock, in 26.3's own replies. A time is a number with t, s or d; `set` takes 0 and up, `add` -2147483648 and up (the total stops at 0). `pause` on a paused clock (`resume` on a running one) is vanilla's `Clock minecraft:overworld is already paused` (`running`) error. | `Set minecraft:overworld to time marker minecraft:night` |
| `/kill [target]` | Kills the player through creative's invulnerability. `@e` also removes every item entity and experience orb (step 11a). | `Killed Player`; `/kill @e` with others: `Killed 3 entities` (`commands.kill.success.multiple`) |

Targets: `@s @p @a @r` and the player's name (any case). `@e @n` work for `/kill` and `/tp` (they find the player; `/kill @e` also the item entities and orbs, the world's only other entities); `/tp`'s destination is one entity, so `/tp @a`, `/tp @e` and `/tp @s @e` refuse with vanilla's "Only one entity is allowed..." line; `/gamemode` and `/give` refuse them with vanilla's "Only players may be affected..." line.

## Known differences (flagged; easy to change)

- **Adventure and Spectator** are refused with `Adventure Mode isn't available in this build`, which isn't vanilla. The modes aren't modelled yet.
- **`/kill @e`** counts the player, the item entities and the experience orbs, the only entities so far (mobs come with step 20). The player's death drops (death screen on) leave on its next step, after the removal, so they stay.
- **The player's name** is `Player`, a stand-in: the project has no username.
- **`/time`:** 26.3's replies (time markers, `Set minecraft:overworld to ... tick(s)`) replace the older "Set the time to 1000". `query daytime` is vanilla's incorrect-argument error. `set <word>` tries the markers first, then a time; an unknown word reports "Time marker ... does not exist". The order of 26.3's two `set` branches isn't verified. Without a world clock (the Playground), `/time` prints `There is no default clock in dimension minecraft:overworld`.
- **Not supported:**
  - local `^ ^ ^` coordinates: a non-vanilla line;
  - selector options `[...]`;
  - `/tp` rotation arguments;
  - `/time rate`.
- **Formatting:** the context line's underline and the italic `<--[HERE]` aren't drawn (the HUD font has neither style). `[Apple]` is always white; vanilla colours it by rarity.
- **Layout** (minecraft.wiki chat settings, provisional):
  - width 320 GUI px, 9 px rows, 10 rows closed and 20 open;
  - the bottom 40 px above the screen's bottom edge;
  - the band 328 px wide;
  - 10 s visible, then a linear fade over the last second. Vanilla's exact curve exists only in client code.
  - the input bar: x 2 to width-2, y height-14, 12 px tall, 50% black. The typed text is at (4, height-12) in light gray E0E0E0. The `_` caret is always at the end of the line; it doesn't blink or follow the cursor, there is no selection, and a long line isn't scrolled.
- **Not echoed:** commands don't print the typed line, as vanilla. Death messages in chat are step 11.
- **Errors:** an empty argument after a trailing space reports "Unknown or incomplete command" (vanilla may name the argument's own error).
