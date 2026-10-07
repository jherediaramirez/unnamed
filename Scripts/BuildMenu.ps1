[CmdletBinding()]
param([string]$EngineRoot='C:\Program Files\Epic Games\UE_5.8', [switch]$Package, [switch]$CreateMap)
$ErrorActionPreference='Stop'
$root=Split-Path $PSScriptRoot -Parent
$project=Join-Path $root 'Unnamed.uproject'
& node (Join-Path $PSScriptRoot 'AdaptMenu.mjs')
if($LASTEXITCODE){throw 'Menu adaptation failed'}
& "$EngineRoot\Engine\Build\BatchFiles\Build.bat" UnnamedEditor Win64 Development "-Project=$project" -WaitMutex -NoHotReloadFromIDE
if($LASTEXITCODE){throw 'Editor build failed'}
if($CreateMap){
    & "$EngineRoot\Engine\Binaries\Win64\UnrealEditor-Cmd.exe" $project -run=pythonscript "-script=$PSScriptRoot\CreateMenuMap.py" -unattended -nop4 -NullRHI
    if($LASTEXITCODE){throw 'Menu map setup failed'}
}
if($Package){
    & "$EngineRoot\Engine\Build\BatchFiles\RunUAT.bat" BuildCookRun "-project=$project" -noP4 -platform=Win64 -clientconfig=Development -build -cook -map=/Game/Menu/L_Menu -stage -pak -archive "-archivedirectory=$root\Packages\MenuDevelopment" -unattended -utf8output
    if($LASTEXITCODE){throw 'Development packaging failed'}
}
