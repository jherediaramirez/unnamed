[CmdletBinding()]
param([string]$EngineRoot='C:\Program Files\Epic Games\UE_5.8', [switch]$Package, [switch]$CreateMap,
    [ValidatePattern('^[A-Za-z0-9_-]+$')][string]$PackageName=('MenuDevelopment-'+(Get-Date -Format 'yyyyMMdd-HHmmss')), [switch]$ExcludeDebugTools,
    [ValidateRange(0,999999)][int]$EditorModuleSuffix=0)
$ErrorActionPreference='Stop'
$root=Split-Path $PSScriptRoot -Parent
if($Package -and (Test-Path -LiteralPath (Join-Path $root "Packages\$PackageName"))){
    throw "Package $PackageName already exists. Choose a fresh -PackageName to preserve prior builds."
}
$project=Join-Path $root 'Unnamed.uproject'
$adapterArgs=@(Join-Path $PSScriptRoot 'AdaptMenu.mjs')
if($ExcludeDebugTools){$adapterArgs+='--without-debug-tools'}
& node @adapterArgs
if($LASTEXITCODE){throw 'Menu adaptation failed'}
$taskBuildArguments=@('UnnamedEditor','Win64','Development',"-Project=$project",'-WaitMutex','-NoHotReloadFromIDE')
if($EditorModuleSuffix){$taskBuildArguments+="-ModuleWithSuffix=Unnamed,$EditorModuleSuffix"}
& "$EngineRoot\Engine\Build\BatchFiles\Build.bat" @taskBuildArguments
if($LASTEXITCODE){throw 'Editor build failed'}
& "$EngineRoot\Engine\Binaries\Win64\UnrealEditor-Cmd.exe" $project -run=pythonscript "-script=$PSScriptRoot\CreateInputAssets.py" -unattended -nop4 -NullRHI
if($LASTEXITCODE){throw 'Input asset setup failed'}
if($CreateMap){
    & "$EngineRoot\Engine\Binaries\Win64\UnrealEditor-Cmd.exe" $project -run=pythonscript "-script=$PSScriptRoot\CreateMenuMap.py" -unattended -nop4 -NullRHI
    if($LASTEXITCODE){throw 'Menu map setup failed'}
}
# Both entry maps are part of the product. Setup preserves an already-authored level.
& "$EngineRoot\Engine\Binaries\Win64\UnrealEditor-Cmd.exe" $project -run=pythonscript "-script=$PSScriptRoot\CreateSuperFlatMap.py" -unattended -nop4 -NullRHI
if($LASTEXITCODE){throw 'Super Flat map setup failed'}
if($Package){
    # The editor was built above, including a unique module suffix when needed.
    # Rebuilding it in UAT would discard that suffix and touch an open editor's DLL.
    & "$EngineRoot\Engine\Build\BatchFiles\RunUAT.bat" BuildCookRun "-project=$project" -noP4 -platform=Win64 -clientconfig=Development -build -skipbuildeditor -cook '-map=/Game/Menu/L_Menu+/Game/Maps/L_SuperFlat' -stage -pak -archive "-archivedirectory=$root\Packages\$PackageName" -unattended -utf8output
    if($LASTEXITCODE){throw 'Development packaging failed'}
}
