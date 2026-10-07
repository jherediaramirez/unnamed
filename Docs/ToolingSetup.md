# Unreal tooling setup

Prepared and verified on October 7, 2026 for
`C:\dev\projects\Unnamed\Unnamed.uproject`, using Unreal Engine
5.8.3, changelist 58210709.

## Enabled tools

All of these plugins were already installed with Unreal. No third-party Unreal
plugin or additional MCP server package was downloaded.

| Plugin | Purpose |
| --- | --- |
| ModelContextProtocol | Native Unreal MCP server; existing selection preserved. |
| MCPClientToolset | Unreal-to-external-MCP adapter; existing selection preserved. |
| EditorToolset | Editor state, assets, Blueprints, objects, scenes, logs, and captures. |
| UMGToolSet | Widget creation and hierarchy inspection. |
| SlateInspectorToolset | Editor UI snapshots, screenshots, and interaction. |
| AutomationTestToolset | Discover and run Unreal automation tests. |
| ConfigSettingsToolset | Inspect and update editor/project configuration. |
| PluginToolset | Verify plugin availability, dependencies, and enabled state. |
| PythonScriptPlugin | Unreal's bundled editor Python automation. |
| EditorScriptingUtilities | Editor asset and scripting operations. |
| WebBrowserWidget | Browser widget required by the proposed HTML menu host. |

ToolsetRegistry is enabled automatically as a dependency. ModelingToolsEditorMode
was already enabled and remains enabled. The tooling entries have
`TargetAllowList: ["Editor"]`; WebBrowserWidget remains available to runtime
targets. Packaged-build exclusion has not yet been tested.

The selected toolsets cover the menu work. Epic's AllToolsets aggregator also
enables unrelated animation, physics, procedural-generation, and gameplay
plugins, so it was not needed for this setup.

## Connection

Endpoint: `http://127.0.0.1:18777/mcp`

`Config/DefaultEditorPerProjectUserSettings.ini` enables the native MCP server
when this project's editor opens. `Config/DefaultEngine.ini` explicitly binds
the HTTP listener to `127.0.0.1`. The server stops when the editor closes.

Codex's global `C:\Users\jherediaramirez\.codex\config.toml` contains the
`unnamed_unreal` Streamable HTTP connection. `codex mcp get unnamed_unreal`
confirmed that the entry is enabled and points at the correct endpoint.
The current chat's tool list does not yet include the new connection; Codex
must refresh its MCP connections to expose it as native chat tools.

`Scripts/InvokeUnrealMCP.ps1` also provides access in the current session.
It initializes the MCP protocol, sends the initialized notification, and makes
one request at a time. Example, run from the project directory:

```powershell
& .\Scripts\InvokeUnrealMCP.ps1
& .\Scripts\InvokeUnrealMCP.ps1 -Method 'tools/call' -Parameters @{
    name = 'list_toolsets'
    arguments = @{}
}
```

Read `describe_toolset` before invoking a tool and use its returned schema.
Slate inspection covers the Unreal host; it does not inspect HTML DOM controls
inside an embedded browser.

## Verification

- Unreal closed normally and restarted without losing a saved project asset.
- The MCP listener was verified as `127.0.0.1:18777`, owned by the editor.
- Protocol initialization negotiated `2025-11-25`.
- `tools/list` returned `list_toolsets`, `describe_toolset`, and `call_tool`.
- `list_toolsets` advertised 24 tool groups.
- `ListEnabledPlugins` confirmed all eleven plugins above plus ToolsetRegistry.
- SceneTools returned the current template level `/Temp/Untitled_1`.
- Slate inspection returned the `Unnamed - Unreal Editor` and `Plugins` windows.
- Slate screenshot capture succeeded; the nonblank editor image was inspected.
- The project JSON, Codex TOML, and PowerShell helper passed parsing checks.
- The startup log contained no `Error:` or `Fatal error:` entries at verification.

Screenshot: `Saved/ToolingCheck/EditorTooling-20261007.png` (2560 x 1392).
This verifies editor tooling, not menu rendering, C++ compilation, automation
test results, or Win64 packaging. No menu implementation was started.

## Preservation

The pre-edit project descriptor, Config directory, saved editor configuration,
and Codex configuration are preserved at:

`C:\dev\sandbox\Unnamed-Tooling-Baseline-20261007T065326`

There was no Git repository, staged index, or saved project Content asset.
The reference HTML was unchanged; its SHA-256 remained:

`72F3DB289F4ECB7E55261EC48CD44B5FEAA170E8B4891641245390B473F1CD18`

## Official references

- [Unreal MCP and toolset setup](https://dev.epicgames.com/documentation/unreal-engine/unreal-mcp-in-unreal-editor)
- [Codex MCP configuration example](https://developers.openai.com/learn/docs-mcp)
