[CmdletBinding()]
param([string]$EngineRoot='C:\Program Files\Epic Games\UE_5.8', [switch]$Package,
    [ValidatePattern('^[A-Za-z0-9_-]+$')][string]$PackageName=('FoundationDevelopment-'+(Get-Date -Format 'yyyyMMdd-HHmmss')),
    [ValidateRange(0,999999)][int]$EditorModuleSuffix=0)
$ErrorActionPreference='Stop'
$root=Split-Path $PSScriptRoot -Parent
$project=Join-Path $root 'Unnamed.uproject'
if($Package -and (Test-Path -LiteralPath (Join-Path $root "Packages/$PackageName"))){throw 'Use a fresh package name; existing candidates are preserved.'}
& node (Join-Path $root 'MenuWeb/AdaptMenu.mjs')
if($LASTEXITCODE){throw 'Menu generation failed'}
$buildArgs=@('UnnamedEditor','Win64','Development',"-Project=$project",'-WaitMutex','-NoHotReloadFromIDE')
if($EditorModuleSuffix){$buildArgs+="-ModuleWithSuffix=Unnamed,$EditorModuleSuffix"}
& "$EngineRoot/Engine/Build/BatchFiles/Build.bat" @buildArgs
if($LASTEXITCODE){throw 'Editor build failed'}
foreach($script in @('CreateInputAssets.py','SetupFoundation.py')){
    & "$EngineRoot/Engine/Binaries/Win64/UnrealEditor-Cmd.exe" $project -run=pythonscript "-script=$PSScriptRoot/Editor/$script" -unattended -nop4 -NullRHI
    if($LASTEXITCODE){throw "Asset setup failed: $script"}
}
# Cooked maps come from MapsToCook in Config/DefaultGame.ini.
if($Package){
    & "$EngineRoot/Engine/Build/BatchFiles/RunUAT.bat" BuildCookRun "-project=$project" -noP4 -platform=Win64 -clientconfig=Development -build -skipbuildeditor -cook -stage -pak -archive "-archivedirectory=$root/Packages/$PackageName" -unattended -utf8output
    if($LASTEXITCODE){throw 'Development packaging failed'}
}
