[CmdletBinding()]
param([switch]$Editor,[switch]$DirectLevel,[string]$EngineRoot='C:\Program Files\Epic Games\UE_5.8',
    [ValidatePattern('^[A-Za-z0-9_-]+$')][string]$PackageName='InteractionDevelopment20261008-r3',
    [ValidateRange(640,7680)][int]$Width=1280,[ValidateRange(360,4320)][int]$Height=720)
$ErrorActionPreference='Stop'
$root=Split-Path $PSScriptRoot -Parent
$exe=if($Editor){"$EngineRoot/Engine/Binaries/Win64/UnrealEditor.exe"}else{Join-Path $root "Packages/$PackageName/Windows/Unnamed.exe"}
if(-not(Test-Path -LiteralPath $exe)){throw "Build the Foundation candidate first: $exe"}
$arguments=@()
if($Editor){$arguments+=('"'+(Join-Path $root 'Unnamed.uproject')+'"')}
# -DirectLevel opens the Playground (interaction stations, movement course and everything else) directly.
if($DirectLevel){$arguments+='/Game/Maps/L_Playground'}
if($Editor){$arguments+='-game'}
$arguments+=@('-windowed',"-ResX=$Width","-ResY=$Height",'-nosplash')
# The user invokes this launcher to play in a visible interactive window.
Start-Process -FilePath $exe -ArgumentList $arguments -WorkingDirectory $root -WindowStyle Normal -PassThru | Select-Object Id,ProcessName
