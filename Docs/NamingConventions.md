# Naming conventions

One rule per kind of thing, so a name tells you where it lives and what it is. When something does not fit, add the exception here with its reason.

## Areas

The project is split into a few areas. The area name is used everywhere: folder, class prefix, test path, log category and JS global.

| Area | C++ folder | Class prefix | Native tests | Log category |
|---|---|---|---|---|
| Menu | `Source/Unnamed/Menu/` | `Menu` (`UMenuBrowserHost`) | `Unnamed.Menu.*` | `LogUnnamedMenu` |
| Input | `Source/Unnamed/Input/` | `UnnamedInput` (`UUnnamedInputSubsystem`) | `Unnamed.Input.*` | - |
| Foundation | `Source/Unnamed/Foundation/` | `Foundation` (`AFoundationController`) | `Unnamed.Foundation.*` | `LogUnnamedFoundation` |
| Hud | `Source/Unnamed/Hud/` | `Hud` (`UHudAssets`, `SHudView`) | `Unnamed.Hud.*` | `LogUnnamedHud` |
| Worlds | `Source/Unnamed/Worlds/` (saved worlds; the catalog stays in `Source/Unnamed/UnnamedProjectSettings.*`) | `Unnamed` (`FUnnamedWorldEntry`, `FUnnamedSavedWorld`) | `Unnamed.Worlds.*` | `LogUnnamedWorlds` |
| Voxel | `Source/UnnamedVoxel/` (core module), `Source/Unnamed/Voxel/` (Unreal side) | `Voxel` (`UVoxelWorldSettings`, `VoxelConstants`) | `Unnamed.Voxel.*` | `LogUnnamedVoxel` |
| Project-wide | `Source/Unnamed/` | `Unnamed` (`UUnnamedProjectSettings`) | `Unnamed.Project.*` | - |

Exceptions, with reasons:

- Input uses `UnnamedInput` rather than `Input`: bare `Input*` collides with engine names such as `UInputSettings`, and `UUnnamedInputSettings` is stored by class path in players' saved settings, so renaming it would lose their bindings.
- Log categories always carry the `LogUnnamed` prefix; they are global symbols and must not clash with engine categories.
- Voxel is the one area with its own module, `UnnamedVoxel`. Its core must run on any thread and in tests, and depending on `Core` alone lets the compiler keep UObjects out ([Voxel world](VoxelWorld.md)). The module name carries the project prefix so it cannot clash with third-party `Voxel*` plugin modules. Its `LogUnnamedVoxel` lives in that module (`VoxelLog.h`), not in `UnnamedLog.h`.
- Voxel console variables and commands use the `voxel.` prefix (`voxel.ViewRadius`, `voxel.Info`); its stat group is `STATGROUP_Voxel` (`stat Voxel`).
- Saved IDs never change once shipped: binding IDs such as `key.sneak` (labelled Crouch), world IDs such as `playground` (retired: `foundation`, `super-flat`, `interaction-lab`; never reused), saved world ids (a world's folder under `Saved/Worlds/`, made once from its name; renaming changes only the name), block and item IDs (the vanilla names without `minecraft:`; item id = block id; retired: `dummy_block`, `dummy_block_item`; never reused), the input save slot `UnnamedInputSettings`. Rename the label, not the ID.
- "Unnamed" is the placeholder project name. Renaming the project is a separate task ([Project rename notes](ProjectRenameNotes.md)).

- Pure HUD equations and compact automation vectors use local mathematical symbols (`S` snapshot, `P` presentation, `L` layout, `B` draw builder) to keep reference coordinates and truth tables easy to compare. Public APIs and stored fields still use descriptive names.

## C++ (Unreal style)

- Standard Unreal prefixes: `U` objects/components, `A` actors, `F` structs, `E` enums, `I` interfaces, `S` Slate widgets, `T` templates, `b` booleans (including parameters).
- One `UCLASS` per file; the file is the class name without its prefix (`UMenuBrowserHost` -> `Menu/MenuBrowserHost.h`). Plain helper structs may share their main type's header (`FoundationActions.h`).
- Include project headers by their path from `Source/Unnamed/`: `#include "Menu/MenuApplication.h"`. Never `../`.
- Functions are verbs (`ApplySetting`, `TraceTarget`, `HandleAction`); getters start with `Get` (`GetMenuHost`); predicates start with `Is`/`Can`/`Has` (`IsGameplay`, `CanSaveSettings`).
- Delegates: member `OnSomething`, type `FOnSomething`.
- Descriptive names. Single letters only for loop indices and the profile `P` in formula-heavy simulation code.
- A browser-facing object that only forwards to another system is a `...Bridge` (`UMenuInputBridge`, `UFoundationMenuBridge`); an object that owns state is named for that state (`UMenuVideoSettings`, `UMenuPreferences`).
- Format with the repository `.clang-format` (clang-format 19 ships with Visual Studio 2022).
- Helpers in a `.cpp` file's anonymous namespace need names unique within the module (`FindBlockTexture`, `ChunkMaterialNames`, not `FindTexture`, `Size` or `FContext`). Unity builds compile several `.cpp` files as one, so two files' anonymous namespaces merge, and a generic name collides or hides an engine name (warnings C4456-C4459 are errors).

## One word per concept

| Use | Not | Where |
|---|---|---|
| crouch | sneak | Code, UI and tests. Only the saved ID `key.sneak` keeps the old word. |
| world | level | Menu catalog entries and their messages. |
| map | level | Unreal assets (`/Game/Maps/L_*`, `MenuMap`). |
| settings | - | Native-owned live values (video, input). |
| preferences | options | The `UMenuPreferences` store of saved choices. |
| bindings | keybinds | Player-mappable key mappings. |

## JavaScript (menu web layer)

- A runtime module is one global object in `MenuWeb/Runtime/<Global>Runtime.js` (`MenuVideo` -> `MenuVideoRuntime.js`). Files that only add helper functions are named for their area (`MenuDialogRuntime.js`).
- A native binding name is its JS global lowercased: `MenuApplication` -> `menuapplication`, `FoundationMenu` -> `foundationmenu`.
- Feature patches are `MenuWeb/Patches/<Feature>.mjs` and export `name`, `apply` and optionally `verify`.

## Scripts, tests and evidence

- PowerShell and editor Python scripts: `VerbNoun` (`BuildFoundation.ps1`, `TestInput.ps1`, `CreatePlaygroundMap.py`); editor Python lives in `Scripts/Editor/`.
- `Scripts/Test<Area>.ps1` runs the native suite `Unnamed.<Area>`.
- Node unit tests: `Tests/<topic>.test.mjs` (run by `npm test`); CEF integration suites: `Tests/<topic>.mjs`.
- Native test files: `Source/Unnamed/Tests/<Area>Tests.cpp` or `<Area><Topic>Tests.cpp`, with test paths `Unnamed.<Area>.<Topic>`.
- Evidence: `Tests/Evidence/<yyyy-mm-dd>/<suite>-<detail>/`; never overwrite an existing folder.
- Packages: `Packages/<Feature>Development<yyyyMMdd>[-rN]`.

## Docs

- Living docs: `Docs/PascalCase.md`; a folder's entry point is `README.md`.
- Dated work records: `Docs/Records/YYYY-MM-DD-Topic.md`, indexed in [Records](Records/README.md).
- Preserved inputs and snapshots: `Docs/Archive/`.
