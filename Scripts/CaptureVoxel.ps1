[CmdletBinding()]
param([string]$EngineRoot='C:\Program Files\Epic Games\UE_5.8',[string]$Seed,[int]$Width=1600,[int]$Height=900,[int]$TimeoutSeconds=900,[string]$ExecCmds,[string]$OutputDirectory)
# Renders the generated world's test area offscreen (/Game/Maps/L_Generated) and saves AVoxelWorld's screenshots:
# overview, closeup, ground, lumen-scene and section, plus build.txt with the build's timings. Evidence is never overwritten.
$ErrorActionPreference='Stop'
$root=Split-Path $PSScriptRoot -Parent
if(-not $OutputDirectory){$OutputDirectory=Join-Path $root ('Tests/Evidence/'+(Get-Date -Format 'yyyy-MM-dd')+'/VoxelShots-'+(Get-Date -Format 'yyyyMMdd-HHmmss'))}
if(Test-Path -LiteralPath $OutputDirectory){throw 'Use a fresh evidence directory.'}
New-Item -ItemType Directory -Path $OutputDirectory | Out-Null
$OutputDirectory=(Resolve-Path -LiteralPath $OutputDirectory).Path
# The editor's per-run user folder is a cache, not evidence.
$profileDirectory=Join-Path ([IO.Path]::GetTempPath()) ('UnnamedVoxelShot-'+[guid]::NewGuid())
$arguments=@(('"'+(Join-Path $root 'Unnamed.uproject')+'"'),'/Game/Maps/L_Generated','-game','-windowed',"-ResX=$Width","-ResY=$Height",'-ForceRes','-RenderOffscreen','-unattended','-nosplash','-nosound','-MenuSettingsSessionOnly',('-VoxelShot="'+$OutputDirectory+'"'),('-abslog="'+(Join-Path $OutputDirectory 'game.log')+'"'),('-UserDir="'+$profileDirectory+'"'))
if($Seed){$arguments+=('-VoxelSeed="'+$Seed+'"')}
# Extra console commands for comparisons, e.g. -ExecCmds 'r.Lumen.HardwareRayTracing.LightingMode 2'.
if($ExecCmds){$arguments+=('-ExecCmds="'+$ExecCmds+'"')}
$started=Get-Date
$process=Start-Process -FilePath "$EngineRoot/Engine/Binaries/Win64/UnrealEditor.exe" -ArgumentList $arguments -WorkingDirectory $root -WindowStyle Hidden -PassThru
if(-not $process.WaitForExit($TimeoutSeconds*1000)){Stop-Process -Id $process.Id -Force; throw "Timed out after $TimeoutSeconds s: $OutputDirectory"}
$elapsed=[int]((Get-Date)-$started).TotalSeconds
if(Test-Path -LiteralPath $profileDirectory){Remove-Item -LiteralPath $profileDirectory -Recurse -Force -ErrorAction SilentlyContinue}
$shots=@('overview','closeup','ground','lumen-scene','section') | Where-Object { Test-Path (Join-Path $OutputDirectory "$_.png") }
if($shots.Count -ne 5 -or -not(Test-Path (Join-Path $OutputDirectory 'build.txt'))){throw "Missing screenshots ($($shots.Count) of 4) after $elapsed s: $OutputDirectory"}
"PASS: 5 screenshots in $elapsed s. $OutputDirectory"
Get-Content (Join-Path $OutputDirectory 'build.txt')
