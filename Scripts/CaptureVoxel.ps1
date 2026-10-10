[CmdletBinding()]
param([string]$EngineRoot='C:\Program Files\Epic Games\UE_5.8',[string]$Seed,[int]$Width=1600,[int]$Height=900,[int]$TimeoutSeconds=900,[string]$ExecCmds,[string]$OutputDirectory,[string]$World,[string]$WorldsDir,[switch]$Flight,[double]$FlightSeconds,[switch]$Edits,[switch]$Garden,[switch]$Caves,[switch]$Veins,[int[]]$VeinsAt,[int[]]$SpawnAt,[switch]$SpawnExact,[switch]$NoSurfaceRules,[switch]$Lights)
# Renders the generated world offscreen (/Game/Maps/L_Generated) and saves AVoxelWorld's screenshots: overview,
# closeup, ground, lumen-scene and section, plus build.txt with the first view's streaming timings. The rings stay on
# the player's spawn, and the view's edge is meshed against air (voxel.Debug.EdgeWalls 1; -ExecCmds can turn it off),
# so the outside shots show the cut through the world. Without -World it shows the dev seed; -World <id> opens that
# saved world from -WorldsDir (default Saved/Worlds). -Flight flies phase 3's acceptance flight instead (FVoxelFlight):
# flight.csv with every frame, flight.txt per leg and four screenshots, with the edge fog and no edge walls.
# -Caves adds views inside a cave, onto a cave wall with ores (cave-wall, preferring daylit walls) and onto still lava when the streamed area has them. -FlightSeconds stops it early (a quick look at the fog from the start shot). -Edits first carves a pit across a
# column corner and builds a pillar through the authority, then adds an "edits" shot of them.
# -Garden builds a plot of every model block near the spawn (rows, a pool, a glow lichen overhang, a dripstone column,
# tall grass across a section border, a distance plot) and a grass field around a bare clearing, then adds the garden,
# field and distance-008 to distance-128 shots (AVoxelWorld::ApplyShotGarden).
# -SpawnAt X,Y starts the spawn search there instead of at the origin, to shoot a chosen place; -SpawnExact checks every
# column within 32 blocks of it against the real terrain, so low dry ground such as a swamp's counts. -NoSurfaceRules shows
# the dev seed with the earlier simple dressing instead of the surface rules, to compare looks.
# -Lights first runs the torch light probe in the largest dark cave (FVoxelLightShots: glowing cubes, then point lights per
# shadow mode): lights.txt, lights.csv and lights-*.png, in a folder ending in -lights; Scripts/SummarizeLightProbe.ps1
# adds lights-passes.txt from the log's GPU profiles.
# Evidence is never overwritten.
$ErrorActionPreference='Stop'
$root=Split-Path $PSScriptRoot -Parent
$kind=if($Flight){'VoxelFlight'}else{'VoxelShots'}
if($Lights -and -not $Flight){if(-not $OutputDirectory){$OutputDirectory=Join-Path $root ('Tests/Evidence/'+(Get-Date -Format 'yyyy-MM-dd')+'/VoxelShots-'+(Get-Date -Format 'yyyyMMdd-HHmmss')+'-lights')};if(-not $PSBoundParameters.ContainsKey('TimeoutSeconds')){$TimeoutSeconds=2400}}
if(-not $OutputDirectory){$OutputDirectory=Join-Path $root ('Tests/Evidence/'+(Get-Date -Format 'yyyy-MM-dd')+"/$kind-"+(Get-Date -Format 'yyyyMMdd-HHmmss'))}
if(Test-Path -LiteralPath $OutputDirectory){throw 'Use a fresh evidence directory.'}
New-Item -ItemType Directory -Path $OutputDirectory | Out-Null
$OutputDirectory=(Resolve-Path -LiteralPath $OutputDirectory).Path
# The editor's per-run user folder is a cache, not evidence.
$profileDirectory=Join-Path ([IO.Path]::GetTempPath()) ('UnnamedVoxelShot-'+[guid]::NewGuid())
$map='/Game/Maps/L_Generated'
# The temporary -UserDir moves Saved/, so a saved world's folder is passed explicitly.
if($World){$map+='?VoxelWorld='+$World;if(-not $WorldsDir){$WorldsDir=Join-Path $root 'Saved/Worlds'}}
$arguments=@(('"'+(Join-Path $root 'Unnamed.uproject')+'"'),$map,'-game','-windowed',"-ResX=$Width","-ResY=$Height",'-ForceRes','-RenderOffscreen','-unattended','-nosplash','-nosound','-MenuSettingsSessionOnly',('-'+$(if($Flight){'VoxelFlight'}else{'VoxelShot'})+'="'+$OutputDirectory+'"'),('-abslog="'+(Join-Path $OutputDirectory 'game.log')+'"'),('-UserDir="'+$profileDirectory+'"'))
if($Seed){$arguments+=('-VoxelSeed="'+$Seed+'"')}
if($Edits -and -not $Flight){$arguments+='-VoxelShotEdits'}
if($Garden -and -not $Flight){$arguments+='-VoxelShotGarden'}
if($Caves -and -not $Flight){$arguments+='-VoxelShotCaves'}
# -Veins adds vein-copper and vein-iron, views onto the cut faces where each vein's ore and filler mix most; -VeinsAt
# F,A,Z,F,A,Z aims them at fixed spots (build.txt of a -Veins run names them), for the control run with
# -ExecCmds 'voxel.OreVeins 0'.
if($Veins -and -not $Flight){$arguments+='-VoxelShotVeins'}
if($VeinsAt -and -not $Flight){if($VeinsAt.Count -ne 6){throw '-VeinsAt takes F,A,Z,F,A,Z'};$arguments+=('-VoxelShotVeinsAt='+($VeinsAt -join ','))}
if($SpawnAt){if($SpawnAt.Count -ne 2){throw '-SpawnAt takes X,Y'};$arguments+=('-VoxelSpawnAt={0},{1}' -f $SpawnAt[0],$SpawnAt[1])}
if($SpawnExact){$arguments+='-VoxelSpawnExact'}
if($NoSurfaceRules){$arguments+='-VoxelNoSurfaceRules'}
if($Lights -and -not $Flight){$arguments+='-VoxelShotLights'}
if($Flight -and $FlightSeconds -gt 0){$arguments+="-VoxelFlightSeconds=$FlightSeconds"}
if($WorldsDir){$arguments+=('-WorldsDir="'+(Resolve-Path -LiteralPath $WorldsDir).Path+'"')}
# Extra console commands for comparisons, e.g. -ExecCmds 'r.Lumen.HardwareRayTracing.LightingMode 2'; later ones win.
$commands=@(if(-not $Flight){'voxel.Debug.EdgeWalls 1'})
if($ExecCmds){$commands+=$ExecCmds}
if($commands){$arguments+=('-ExecCmds="'+($commands -join ',')+'"')}
$started=Get-Date
$process=Start-Process -FilePath "$EngineRoot/Engine/Binaries/Win64/UnrealEditor.exe" -ArgumentList $arguments -WorkingDirectory $root -WindowStyle Hidden -PassThru
if(-not $process.WaitForExit($TimeoutSeconds*1000)){Stop-Process -Id $process.Id -Force; throw "Timed out after $TimeoutSeconds s: $OutputDirectory"}
$elapsed=[int]((Get-Date)-$started).TotalSeconds
if(Test-Path -LiteralPath $profileDirectory){Remove-Item -LiteralPath $profileDirectory -Recurse -Force -ErrorAction SilentlyContinue}
if($Flight){
    if(-not(Test-Path (Join-Path $OutputDirectory 'flight.txt')) -or -not(Test-Path (Join-Path $OutputDirectory 'flight.csv'))){throw "No flight report after $elapsed s: $OutputDirectory"}
    "PASS: flight in $elapsed s. $OutputDirectory"
    Get-Content (Join-Path $OutputDirectory 'flight.txt')
    return
}
$names=@('overview','closeup','ground','lumen-scene','section')+@(if($Edits){'edits'})+@(if($Garden){'garden','garden-close','garden-pool','garden-lichen','garden-amethyst','garden-border','field','field-clearing','distance-008','distance-016','distance-032','distance-064','distance-128'})
$shots=$names | Where-Object { Test-Path (Join-Path $OutputDirectory "$_.png") }
if($shots.Count -ne $names.Count -or -not(Test-Path (Join-Path $OutputDirectory 'build.txt'))){throw "Missing screenshots ($($shots.Count) of $($names.Count)) after $elapsed s: $OutputDirectory"}
if($Lights -and -not(Test-Path (Join-Path $OutputDirectory 'lights.txt'))){throw "No lights.txt after $elapsed s: $OutputDirectory"}
"PASS: $($names.Count) screenshots in $elapsed s. $OutputDirectory"
Get-Content (Join-Path $OutputDirectory 'build.txt')
if($Lights){Get-Content (Join-Path $OutputDirectory 'lights.txt');& (Join-Path $PSScriptRoot 'SummarizeLightProbe.ps1') -Folder $OutputDirectory}
