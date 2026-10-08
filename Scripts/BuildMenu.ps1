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
$adapterArgs=@(Join-Path $root 'MenuWeb\AdaptMenu.mjs')
if($ExcludeDebugTools){$adapterArgs+='--without-debug-tools'}
& node @adapterArgs
if($LASTEXITCODE){throw 'Menu adaptation failed'}
$buildArguments=@('UnnamedEditor','Win64','Development',"-Project=$project",'-WaitMutex','-NoHotReloadFromIDE')
if($EditorModuleSuffix){$buildArguments+="-ModuleWithSuffix=Unnamed,$EditorModuleSuffix"}
& "$EngineRoot\Engine\Build\BatchFiles\Build.bat" @buildArguments
if($LASTEXITCODE){throw 'Editor build failed'}
& "$EngineRoot\Engine\Binaries\Win64\UnrealEditor-Cmd.exe" $project -run=pythonscript "-script=$PSScriptRoot\Editor\CreateInputAssets.py" -unattended -nop4 -NullRHI
if($LASTEXITCODE){throw 'Input asset setup failed'}
if($CreateMap){
    & "$EngineRoot\Engine\Binaries\Win64\UnrealEditor-Cmd.exe" $project -run=pythonscript "-script=$PSScriptRoot\Editor\CreateMenuMap.py" -unattended -nop4 -NullRHI
    if($LASTEXITCODE){throw 'Menu map setup failed'}
}
# Both entry maps are part of the product: the menu and the Playground (with its dev grid material).
# Setup preserves an already-authored level and verifies it.
foreach($script in @('CreateDevGridMaterial.py','CreatePlaygroundMap.py')){
    & "$EngineRoot\Engine\Binaries\Win64\UnrealEditor-Cmd.exe" $project -run=pythonscript "-script=$PSScriptRoot\Editor\$script" -unattended -nop4 -NullRHI
    if($LASTEXITCODE){throw "Asset setup failed: $script"}
}
if($Package){
    # The editor was built above, including a unique module suffix when needed.
    # Rebuilding it in UAT would discard that suffix and touch an open editor's DLL.
    # Cooked maps come from MapsToCook in Config/DefaultGame.ini.
    & "$EngineRoot\Engine\Build\BatchFiles\RunUAT.bat" BuildCookRun "-project=$project" -noP4 -platform=Win64 -clientconfig=Development -build -skipbuildeditor -cook -stage -pak -archive "-archivedirectory=$root\Packages\$PackageName" -unattended -utf8output
    if($LASTEXITCODE){throw 'Development packaging failed'}
}
