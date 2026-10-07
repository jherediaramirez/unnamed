# Environment

Verified 2026-10-07 on Windows 11. The supplied chat working directory (`C:\dev\projects\Minecraft 2.0`) did not exist. The already-created project is `C:\dev\projects\Unnamed\Unnamed.uproject`; no replacement project was created.

| Item | Verified value |
|---|---|
| Engine | UE 5.8.3, changelist 58210709, `C:\Program Files\Epic Games\UE_5.8` |
| Project identity | `Unnamed`, existing ProjectID retained; C++ targets/module also `Unnamed` |
| IDE/toolchain | Visual Studio Community 2022 17.14.37710.0 |
| Compiler | Build reports MSVC 14.44.35229, installed tools directory 14.44.35207 |
| Windows SDK | 10.0.22621.0; Win64 SDK reported valid by UnrealBuildTool |
| Node | 24.18.0; npm used only for verification dependencies |
| Engine Python | 3.11.8, invoked inside Unreal's Python commandlet for map setup |
| Verification packages | Playwright 1.56.1, pngjs 7.0.0; lockfile in Tests |

## Plugins

The installed engine's built-in WebBrowserWidget/WebBrowser provides runtime Chromium and Slate hosting. Editor-only tools are MCPClientToolset, ModelContextProtocol, EditorToolset, UMGToolSet, SlateInspectorToolset, AutomationTestToolset, ConfigSettingsToolset, PluginToolset, PythonScriptPlugin and EditorScriptingUtilities. ToolsetRegistry is an automatic dependency. Their installed descriptors reported version 1.0; engine binaries are supplied with 5.8.3. Existing ModelingToolsEditorMode was retained. No third-party Unreal/MCP plugin was downloaded.

The editor MCP listener is restricted to 127.0.0.1:18777. The separate browser developer ports used during verification were 18888/18889/18890/18891, verified on loopback. Normal menu launch does not open a browser diagnostic port. Editor tools are excluded from the packaged runtime target.

## Preservation

Before implementation, copied descriptor, Config, Content, Docs, Scripts and saved editor config to `C:\dev\sandbox\Unnamed-Menu-Baseline-20261007T070212`, with a SHA-256/size manifest. Earlier tooling preservation is documented in `ToolingSetup.md`. The starting project had no source module, no content assets and no Git repository. Git checkpoints were created after the scaffold, packaged slice and complete route/registry verification. No existing project content, user assets or saved worlds were reset or deleted.

The reference index SHA-256 is `72F3DB289F4ECB7E55261EC48CD44B5FEAA170E8B4891641245390B473F1CD18`. The supplied handoff SHA-256 is `CC5D10759538416E242B079A7C296D6C85A635E1C52A0C17AC86A04F9D29F9B5`. Original asset hashes/sizes remain in the supplied asset-provenance manifest. Reference files are marked non-text in Git to prevent line-ending conversion.
