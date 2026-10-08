[CmdletBinding()]
param([string]$EngineRoot='C:\Program Files\Epic Games\UE_5.8',[string]$Seed,[int]$Width=1600,[int]$Height=900,[int]$TimeoutSeconds=900,[string]$ExecCmds,[string]$OutputDirectory,[string]$World,[string]$WorldsDir)
# Renders the generated world offscreen (/Game/Maps/L_Generated) and saves AVoxelWorld's screenshots: overview,
# closeup, ground, lumen-scene and section, plus build.txt with the build's timings. Without -World it shows the dev
# seed's test area; -World <id> opens that saved world from -WorldsDir (default Saved/Worlds). Evidence is never
# overwritten.
$ErrorActionPreference='Stop'
$root=Split-Path $PSScriptRoot -Parent
if(-not $OutputDirectory){$OutputDirectory=Join-Path $root ('Tests/Evidence/'+(Get-Date -Format 'yyyy-MM-dd')+'/VoxelShots-'+(Get-Date -Format 'yyyyMMdd-HHmmss'))}
if(Test-Path -LiteralPath $OutputDirectory){throw 'Use a fresh evidence directory.'}
New-Item -ItemType Directory -Path $OutputDirectory | Out-Null
$OutputDirectory=(Resolve-Path -LiteralPath $OutputDirectory).Path
# The editor's per-run user folder is a cache, not evidence.
$profileDirectory=Join-Path ([IO.Path]::GetTempPath()) ('UnnamedVoxelShot-'+[guid]::NewGuid())
$map='/Game/Maps/L_Generated'
# The temporary -UserDir moves Saved/, so a saved world's folder is passed explicitly.
if($World){$map+='?VoxelWorld='+$World;if(-not $WorldsDir){$WorldsDir=Join-Path $root 'Saved/Worlds'}}
$arguments=@(('"'+(Join-Path $root 'Unnamed.uproject')+'"'),$map,'-game','-windowed',"-ResX=$Width","-ResY=$Height",'-ForceRes','-RenderOffscreen','-unattended','-nosplash','-nosound','-MenuSettingsSessionOnly',('-VoxelShot="'+$OutputDirectory+'"'),('-abslog="'+(Join-Path $OutputDirectory 'game.log')+'"'),('-UserDir="'+$profileDirectory+'"'))
if($Seed){$arguments+=('-VoxelSeed="'+$Seed+'"')}
if($WorldsDir){$arguments+=('-WorldsDir="'+(Resolve-Path -LiteralPath $WorldsDir).Path+'"')}
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
