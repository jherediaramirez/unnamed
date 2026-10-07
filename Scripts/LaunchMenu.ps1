[CmdletBinding()]
param([int]$Width=1920,[int]$Height=1080,[switch]$Editor,[switch]$Diagnostics,[int]$DebugPort=18888,[string]$EngineRoot='C:\Program Files\Epic Games\UE_5.8',
    [ValidatePattern('^[A-Za-z0-9_-]+$')][string]$PackageName='SuperFlatDevelopment',
    [switch]$SessionOnly,[string]$VideoSettingsFile)
$ErrorActionPreference='Stop'
$root=Split-Path $PSScriptRoot -Parent
$exe=if($Editor){"$EngineRoot\Engine\Binaries\Win64\UnrealEditor.exe"}else{Join-Path $root "Packages\$PackageName\Windows\Unnamed.exe"}
$arguments=@()
if($Editor){$arguments+=('"'+(Join-Path $root 'Unnamed.uproject')+'"');$arguments+='-game'}
$arguments+=@('-windowed',"-ResX=$Width","-ResY=$Height",'-NoSplash','-WinX=0','-WinY=0')
if($Diagnostics){$arguments+="-cefdebug=$DebugPort"}
if($SessionOnly){$arguments+='-MenuSettingsSessionOnly'}
if($VideoSettingsFile){$arguments+=('-MenuVideoSettingsFile="'+$VideoSettingsFile+'"')}
Start-Process -FilePath $exe -ArgumentList $arguments -WorkingDirectory $root -WindowStyle Normal -PassThru | Select-Object Id,ProcessName
