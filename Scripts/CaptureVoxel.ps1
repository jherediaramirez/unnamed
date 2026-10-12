[CmdletBinding()]
param([string]$EngineRoot='C:\Program Files\Epic Games\UE_5.8',[string]$Seed,[int]$Width=1600,[int]$Height=900,[int]$TimeoutSeconds=900,[string]$ExecCmds,[string]$OutputDirectory,[string]$World,[string]$WorldsDir,[switch]$Flight,[double]$FlightSeconds,[switch]$Edits,[switch]$Garden,[switch]$Caves,[switch]$Veins,[int[]]$VeinsAt,[int[]]$SpawnAt,[switch]$SpawnExact,[switch]$ClimateSpawn,[double[]]$View,[switch]$NoSurfaceRules,[switch]$Lights,[switch]$TorchGates,[switch]$TorchRoom,[switch]$TorchLook,[int]$TorchGlow=-1,[ValidateSet('breach','flood','scenes')][string]$Fluids,[double[]]$Transition)
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
# column within 32 blocks of it against the real terrain, so low dry ground such as a swamp's counts. Without -SpawnAt the
# search starts at 0,0 (-VoxelSpawnAt=0,0), so shots and the flight keep the nearest-land spawn they were framed on;
# -ClimateSpawn instead shoots the spawn a new world gets (the climate search, FVoxelSpawnFinder). -NoSurfaceRules shows
# the dev seed with the earlier simple dressing instead of the surface rules, to compare looks.
# -Lights first runs the torch light probe in the largest dark cave (FVoxelLightShots: glowing cubes, then point lights per
# shadow mode): lights.txt, lights.csv and lights-*.png, in a folder ending in -lights; Scripts/SummarizeLightProbe.ps1
# adds lights-passes.txt from the log's GPU profiles.
# -TorchGates instead runs the torch light pool's gate cases (session T3, FVoxelLightShots with -VoxelShotTorchGates): the
# look pairs (cave, cave-lava and the probe cave with 10 torches, MegaLights 0 and 1), the edit cost of one torch, T1's timed
# cases with torches-1/10/100 beside megalights-1/10/100, 64 torches clustered against spread, and the torch leak test:
# torches.txt, torches.csv, torches-*.png and lights-leak-*.png, in a folder ending in -torches; lights-passes.txt as for -Lights.
# -TorchRoom instead builds the torch look reference (session T3, probe P4; FVoxelLightShots with -VoxelShotTorchRoom): a
# 9 x 4 x 9 air room in a stone shell 31 blocks under the surface near the spawn, seen from a corner at eye height with the
# exposure frozen on one torch at 1000 cd: one torch, then four, with MegaLights 0 and 1 at several voxel.LightIntensity and
# voxel.LightTemperature values, then a glowstone in the ceiling, then one and four torches at voxel.LightFalloff 2 and 8
# (each after a short voxel.LightBrightness search matching the 1000 cd floor patch); room.txt (cells, camera, EV, patch table, voxel.LightsInfo),
# room.csv and room-*.png, in a folder ending in -room. Every written cell and changed variable is put back.
# -TorchLook instead shoots the torch look pairs (session T3, the warm look; -VoxelShotTorchLook) at the run's own settings
# (no sweep), each with MegaLights 0 and 1, under the room's frozen exposure: the room with 1 torch, a close crop of that
# torch, the room with 4 torches and T1's probe cave with 10; look.txt (mean luma, saturated and black shares, settings,
# voxel.LightsInfo per shot) and look-<view>-ml<0|1>.png, in a folder ending in -look.
# -TorchGlow <0-15> meshes the torch models at that glow from the start (-VoxelTorchGlow=N, voxel.TorchGlow; the default 1
# is warm A's whole torch (T3b), 0 leaves only the flame glowing at its own 1, 14 is the old white torch).
# -Fluids breach|flood|scenes first runs a flowing water capture (FVoxelFluidShots, -VoxelShotFluids; breach and flood
# switch voxel.Fluids on, scenes runs it as set): breach
# mines into a sea from a sealed room under it (breach.txt, breach.csv per second, breach-dry/03s/08s/20s.png); flood
# places 25 water sources on flat ground and traces 30 s of frames (flood.txt, flood.csv per frame, flood-end.png). In a
# folder ending in -<scenario>. scenes (water and sky W3b) builds the vanilla client probe's five water and lava scenes on
# a Superflat world (-World, with the grass top at y -61; -SpawnAt -65,25 -SpawnExact as the probe stood) and shoots its
# 20 poses at -Width 1920 -Height 1080 (scenes.txt: the reach checks, each scene's ground as levels; scenes-<pose>.png).
# Not with -Flight.
# -Transition SX,SY,SZ,STX,STY,STZ,HX,HY,HZ,HTX,HTY,HTZ (blocks: a sun view's camera and target, then a shade view's) first
# runs the eye adaptation check (FVoxelTransitionShots, -VoxelShotTransition): settled on the sun view, the camera cuts into
# the shade and back out at the map's own adaptation speeds, with frames 0 to 5 s after each cut (transition.txt,
# transition-sun-settled.png, transition-<in|out>-<ms>.png). Not with -Flight.
# Evidence is never overwritten.
$ErrorActionPreference='Stop'
$root=Split-Path $PSScriptRoot -Parent
# The look pairs build the torch room (its steps are skipped).
if($TorchLook){$TorchRoom=[switch]$true}
$kind=if($Flight){'VoxelFlight'}else{'VoxelShots'}
if(($Lights -or $TorchGates -or $TorchRoom) -and -not $Flight){if(-not $OutputDirectory){$OutputDirectory=Join-Path $root ('Tests/Evidence/'+(Get-Date -Format 'yyyy-MM-dd')+'/VoxelShots-'+(Get-Date -Format 'yyyyMMdd-HHmmss')+$(if($TorchLook){'-look'}elseif($TorchRoom){'-room'}elseif($TorchGates){'-torches'}else{'-lights'}))};if(-not $PSBoundParameters.ContainsKey('TimeoutSeconds')){$TimeoutSeconds=2400}}
if($Fluids -and $Flight){throw '-Fluids runs without -Flight.'}
if($Fluids -and -not $Flight -and -not $OutputDirectory){$OutputDirectory=Join-Path $root ('Tests/Evidence/'+(Get-Date -Format 'yyyy-MM-dd')+'/VoxelShots-'+(Get-Date -Format 'yyyyMMdd-HHmmss')+"-$Fluids")}
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
if($ClimateSpawn -and ($SpawnAt -or $SpawnExact)){throw '-ClimateSpawn takes no -SpawnAt or -SpawnExact.'}
if($SpawnAt){if($SpawnAt.Count -ne 2){throw '-SpawnAt takes X,Y'};$arguments+=('-VoxelSpawnAt={0},{1}' -f $SpawnAt[0],$SpawnAt[1])}
elseif(-not $ClimateSpawn){$arguments+='-VoxelSpawnAt=0,0'}
if($SpawnExact){$arguments+='-VoxelSpawnExact'}
# -View CX,CY,CZ,TX,TY,TZ adds a "view" shot from a camera to a target, in blocks (a spot -SpawnAt streams), e.g. an
# ocean floor or the column grid (-ExecCmds 'voxel.Debug.ColumnGrid 1').
if($View -and -not $Flight){if($View.Count -ne 6){throw '-View takes CX,CY,CZ,TX,TY,TZ'};$arguments+=('-VoxelShotView='+($View -join ','))}
if($PSBoundParameters.ContainsKey('Transition')){if($Flight){throw '-Transition runs without -Flight.'};if($Transition.Count -ne 12){throw '-Transition takes a sun camera and target, then a shade camera and target (12 numbers)'};$arguments+=('-VoxelShotTransition='+($Transition -join ','))}
if($NoSurfaceRules){$arguments+='-VoxelNoSurfaceRules'}
if(($Lights -or $TorchGates -or $TorchRoom) -and -not $Flight){$arguments+='-VoxelShotLights'}
if($TorchGates -and -not $Flight){$arguments+='-VoxelShotTorchGates'}
if($TorchRoom -and -not $Flight){$arguments+='-VoxelShotTorchRoom'}
if($TorchLook -and -not $Flight){$arguments+='-VoxelShotTorchLook'}
if($Fluids -and -not $Flight){$arguments+="-VoxelShotFluids=$Fluids"}
if($TorchGlow -ge 0){$arguments+="-VoxelTorchGlow=$TorchGlow"}
if($Flight -and $FlightSeconds -gt 0){$arguments+="-VoxelFlightSeconds=$FlightSeconds"}
if($WorldsDir){$arguments+=('-WorldsDir="'+(Resolve-Path -LiteralPath $WorldsDir).Path+'"')}
# Extra console commands for comparisons, e.g. -ExecCmds 'r.Lumen.HardwareRayTracing.LightingMode 2'; later ones win.
$commands=@(if(-not $Flight){'voxel.Debug.EdgeWalls 1'})
# The torch gates compare 100 torches with T1's 100 probe lights, so the pool's cap (voxel.LightCount, 64) goes to 100.
if($TorchGates -and -not $Flight){$commands+='voxel.LightCount 100'}
if($ExecCmds){$commands+=$ExecCmds}
if($commands){$arguments+=('-ExecCmds="'+($commands -join ',')+'"')}
$started=Get-Date
$process=Start-Process -FilePath "$EngineRoot/Engine/Binaries/Win64/UnrealEditor.exe" -ArgumentList $arguments -WorkingDirectory $root -WindowStyle Hidden -PassThru
$timedOut=-not $process.WaitForExit($TimeoutSeconds*1000)
# A timed-out run stops its own editor (by PID) and waits for it to let go of the profile.
if($timedOut){Stop-Process -Id $process.Id -Force -ErrorAction SilentlyContinue; $process.WaitForExit(30000) | Out-Null}
$elapsed=[int]((Get-Date)-$started).TotalSeconds
# A crash's dump and log are under the profile: keep them in <OutputDirectory>\Crashes before it goes, on both exits.
. (Join-Path $PSScriptRoot 'CrashFolders.ps1')
$closed=Close-Profile -UserDir $profileDirectory -Destination $OutputDirectory
$crashNote="$(if($closed.Crashes){"; crash folders kept $($closed.Crashes) in $OutputDirectory\Crashes"})$(if($closed.Kept){"; profile kept at $($closed.Kept)"})"
if($timedOut){throw "Timed out after $TimeoutSeconds s${crashNote}: $OutputDirectory"}
if($Flight){
    if(-not(Test-Path (Join-Path $OutputDirectory 'flight.txt')) -or -not(Test-Path (Join-Path $OutputDirectory 'flight.csv'))){throw "No flight report after $elapsed s${crashNote}: $OutputDirectory"}
    "PASS: flight in $elapsed s${crashNote}. $OutputDirectory"
    Get-Content (Join-Path $OutputDirectory 'flight.txt')
    return
}
$names=@('overview','closeup','ground','lumen-scene','section')+@(if($Edits){'edits'})+@(if($View){'view'})+@(if($Garden){'garden','garden-close','garden-pool','garden-lichen','garden-amethyst','garden-border','field','field-clearing','distance-008','distance-016','distance-032','distance-064','distance-128'})
$shots=$names | Where-Object { Test-Path (Join-Path $OutputDirectory "$_.png") }
if($shots.Count -ne $names.Count -or -not(Test-Path (Join-Path $OutputDirectory 'build.txt'))){throw "Missing screenshots ($($shots.Count) of $($names.Count)) after $elapsed s${crashNote}: $OutputDirectory"}
$lightsReport=if($TorchLook){'look.txt'}elseif($TorchRoom){'room.txt'}elseif($TorchGates){'torches.txt'}else{'lights.txt'}
if(($Lights -or $TorchGates -or $TorchRoom) -and -not(Test-Path (Join-Path $OutputDirectory $lightsReport))){throw "No $lightsReport after $elapsed s${crashNote}: $OutputDirectory"}
if($Fluids -and -not(Test-Path (Join-Path $OutputDirectory "$Fluids.txt"))){throw "No $Fluids.txt after $elapsed s${crashNote}: $OutputDirectory"}
if($Fluids -and (Select-String -LiteralPath (Join-Path $OutputDirectory "$Fluids.txt") -SimpleMatch 'RESULT: FAILED' -Quiet)){throw "The $Fluids capture failed (see $Fluids.txt)${crashNote}: $OutputDirectory"}
"PASS: $($names.Count) screenshots in $elapsed s${crashNote}. $OutputDirectory"
Get-Content (Join-Path $OutputDirectory 'build.txt')
if($Lights -or $TorchGates -or $TorchRoom){Get-Content (Join-Path $OutputDirectory $lightsReport)}
if($Fluids){Get-Content (Join-Path $OutputDirectory "$Fluids.txt")}
if(($Lights -or $TorchGates) -and -not $TorchRoom){& (Join-Path $PSScriptRoot 'SummarizeLightProbe.ps1') -Folder $OutputDirectory}
