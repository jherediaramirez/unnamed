[CmdletBinding()]
param([switch]$Editor,[switch]$DirectLevel,[string]$EngineRoot='C:\Program Files\Epic Games\UE_5.8',
    [ValidatePattern('^[A-Za-z0-9_-]+$')][string]$PackageName='FoundationDevelopment20261007-r3',
    [ValidateRange(640,7680)][int]$Width=1280,[ValidateRange(360,4320)][int]$Height=720)
$ErrorActionPreference='Stop'
$taskRoot=Split-Path $PSScriptRoot -Parent
$taskExe=if($Editor){"$EngineRoot/Engine/Binaries/Win64/UnrealEditor.exe"}else{Join-Path $taskRoot "Packages/$PackageName/Windows/Unnamed.exe"}
if(-not(Test-Path -LiteralPath $taskExe)){throw "Build the Foundation candidate first: $taskExe"}
$taskArgs=@()
if($Editor){$taskArgs+=('"'+(Join-Path $taskRoot 'Unnamed.uproject')+'"')}
if($DirectLevel){$taskArgs+='/Game/Maps/L_Foundation'}
if($Editor){$taskArgs+='-game'}
$taskArgs+=@('-windowed',"-ResX=$Width","-ResY=$Height",'-nosplash')
# The user invokes this launcher to play in a visible interactive window.
Start-Process -FilePath $taskExe -ArgumentList $taskArgs -WorkingDirectory $taskRoot -WindowStyle Normal -PassThru | Select-Object Id,ProcessName
