[CmdletBinding()]
param([string]$EngineRoot='C:\Program Files\Epic Games\UE_5.8', [switch]$Package,
    [ValidatePattern('^[A-Za-z0-9_-]+$')][string]$PackageName=('FoundationDevelopment-'+(Get-Date -Format 'yyyyMMdd-HHmmss')),
    [ValidateRange(0,999999)][int]$EditorModuleSuffix=0)
$ErrorActionPreference='Stop'
$taskRoot=Split-Path $PSScriptRoot -Parent
$taskProject=Join-Path $taskRoot 'Unnamed.uproject'
if($Package -and (Test-Path -LiteralPath (Join-Path $taskRoot "Packages/$PackageName"))){throw 'Use a fresh package name; existing candidates are preserved.'}
& node (Join-Path $PSScriptRoot 'AdaptMenu.mjs')
if($LASTEXITCODE){throw 'Menu generation failed'}
$buildArgs=@('UnnamedEditor','Win64','Development',"-Project=$taskProject",'-WaitMutex','-NoHotReloadFromIDE')
if($EditorModuleSuffix){$buildArgs+="-ModuleWithSuffix=Unnamed,$EditorModuleSuffix"}
& "$EngineRoot/Engine/Build/BatchFiles/Build.bat" @buildArgs
if($LASTEXITCODE){throw 'Editor build failed'}
foreach($script in @('CreateInputAssets.py','SetupFoundation.py')){
    & "$EngineRoot/Engine/Binaries/Win64/UnrealEditor-Cmd.exe" $taskProject -run=pythonscript "-script=$PSScriptRoot/$script" -unattended -nop4 -NullRHI
    if($LASTEXITCODE){throw "Asset setup failed: $script"}
}
if($Package){
    & "$EngineRoot/Engine/Build/BatchFiles/RunUAT.bat" BuildCookRun "-project=$taskProject" -noP4 -platform=Win64 -clientconfig=Development -build -skipbuildeditor -cook '-map=/Game/Menu/L_Menu+/Game/Maps/L_Foundation+/Game/Maps/L_SuperFlat' -stage -pak -archive "-archivedirectory=$taskRoot/Packages/$PackageName" -unattended -utf8output
    if($LASTEXITCODE){throw 'Development packaging failed'}
}
