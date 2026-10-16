[CmdletBinding()]
param([switch]$Editor,[string]$EngineRoot='C:\Program Files\Epic Games\UE_5.8',
    [ValidatePattern('^[A-Za-z0-9_-]+$')][string]$PackageName='InteractionDevelopment20261008-r3',
    [string]$OutputDirectory,[int[]]$FrameRates=@(30,60,120),[switch]$FullCostRendering,
    [ValidateRange(320,7680)][int]$Width=1280,[ValidateRange(240,4320)][int]$Height=720,
    [switch]$HudCaptures,[switch]$TraceHud,
    # One map (the Playground) runs every scenario: the movement course and input pipeline, the interaction
    # stations, both, or the screenshot tour (not part of all). Optional placement preset and finite fixture apply to the interaction stations.
    # 'views' (not part of all either) opens the generated world's dev seed (/Game/Maps/L_Generated) instead: player-view
    # shots with the HUD, outline and held item at full-cost rendering, posed from Scripts/Views/DevSeed.txt (passed by
    # path, so a package finds it too); -ViewsScout logs candidate poses instead. Not with -HudCaptures (Playground cases).
    # 'pause' (not part of all) records the frames around ESC and Back to Game (FoundationPauseProbe.cpp).
    # 'swim' (not part of all) plays the swimming control pair in the Playground pool (FoundationSwimProbe.cpp).
    # 'body' (not part of all) poses the player body in F5's back and front views and the spring arm preview, with shots (FoundationBodyProbe.cpp).
    # 'smear' (not part of all) shoots mobs and drops moving across the view, the held sword and a turning camera, for the smear fix (K-PLAYER-7; FoundationSmearProbe.cpp); run it with -FullCostRendering for the game's own anti-aliasing.
    [ValidateSet('movement','interaction','all','tour','views','pause','swim','body','mobs','smear')][string]$Suite='all',[switch]$ViewsScout,
    [ValidateSet('VanillaReference','LucidFluid')][string]$Preset,[switch]$Finite,
    # The break lane's capture pack: the tour suite with -FoundationTourBreak at full-cost rendering, into Playground-break.
    [switch]$BreakPack,
    # Extra console commands after the run's own, e.g. -ExecCmds 'foundation.Place.Preview 1'; later ones win. The views
    # suite sets foundation.Place.Preview, foundation.Break.Outline and the clock (voxel.Time) itself, so -ExecCmds
    # cannot override those there.
    [string]$ExecCmds,
    # Extra command-line arguments, e.g. -ExtraArgs '-nocefaccelpaint' or a switch read at BeginPlay, which -ExecCmds
    # reaches only after the first world has started: -ExtraArgs '-ini:Engine:[ConsoleVariables]:foundation.Pause.Menu=0'.
    [string[]]$ExtraArgs)
$ErrorActionPreference='Stop'
$root=Split-Path $PSScriptRoot -Parent
if($BreakPack){$Suite='tour';$FullCostRendering=$true}
if($Suite -eq 'views'){
    if($HudCaptures){throw '-HudCaptures cannot run with -Suite views: the HUD cases assume the Playground (L_Playground), and the views suite opens L_Generated.'}
    $FullCostRendering=$true
}
if(-not $OutputDirectory){$OutputDirectory=Join-Path $root ('Tests/Evidence/'+(Get-Date -Format 'yyyy-MM-dd')+"/$(if($Suite -eq 'views'){'Generated'}else{'Playground'})-$(if($BreakPack){'break'}else{$Suite})/Runtime-"+(Get-Date -Format 'yyyyMMdd-HHmmss'))}
if(Test-Path -LiteralPath $OutputDirectory){throw "Evidence folder exists; choose a fresh output folder: $OutputDirectory"}
New-Item -ItemType Directory -Path $OutputDirectory | Out-Null
$OutputDirectory=(Resolve-Path -LiteralPath $OutputDirectory).Path
if($BreakPack){
    # The machine state before the run; java or javaw means a game is being played, so the pack does not start.
    $state=@(Get-Process java,javaw,UnrealEditor* -ErrorAction SilentlyContinue)
    Set-Content -LiteralPath (Join-Path $OutputDirectory 'machine-state.txt') -Value (("Before run $(Get-Date -Format o): Get-Process java,javaw,UnrealEditor*")+[Environment]::NewLine+$(if($state.Count){$state|Format-Table -AutoSize Name,Id,StartTime|Out-String}else{'(none running)'}))
    if(@($state|Where-Object{$_.Name -in 'java','javaw'}).Count){throw "java or javaw is running; the break pack was not started: $OutputDirectory"}
}
$exe=if($Editor){"$EngineRoot/Engine/Binaries/Win64/UnrealEditor.exe"}else{Join-Path $root "Packages/$PackageName/Windows/Unnamed/Binaries/Win64/Unnamed.exe"}
foreach($fps in $FrameRates){
    $output=Join-Path $OutputDirectory "runtime-$fps.json"
    if(Test-Path -LiteralPath $output){throw "Evidence exists; choose a fresh output folder: $output"}
    $arguments=@()
    if($Editor){$arguments+=('"'+(Join-Path $root 'Unnamed.uproject')+'"')}
    $arguments+=$(if($Suite -eq 'views'){'/Game/Maps/L_Generated'}else{'/Game/Maps/L_Playground'})
    $arguments+="-FoundationSuite=$Suite"
    if($ViewsScout){$arguments+='-FoundationViewsScout'}
    # A package does not stage Scripts/, so the probe reads the poses from the repository.
    if($Suite -eq 'views'){$arguments+=('-FoundationViewsPoses="'+(Join-Path $root 'Scripts\Views\DevSeed.txt')+'"')}
    if($BreakPack){$arguments+='-FoundationTourBreak'}
    if($Preset){$arguments+="-InteractionPreset=$Preset"}
    if($Finite){$arguments+='-InteractionFinite'}
    if($Editor){$arguments+='-game'}
    # Offscreen acceptance captures must retain their requested size even above the desktop work area.
    $arguments+=@('-windowed',"-ResX=$Width","-ResY=$Height",'-ForceRes','-RenderOffscreen','-unattended','-nosplash','-nosound','-MenuSettingsSessionOnly',"-FoundationFPS=$fps",('-FoundationProbe="'+$output+'"'),('-abslog="'+(Join-Path $OutputDirectory "runtime-$fps.log")+'"'),('-UserDir="'+(Join-Path $OutputDirectory "profile-$fps")+'"'))
    if($HudCaptures){$arguments+='-HudCaptures'}
    if($ExtraArgs){$arguments+=$ExtraArgs}
    # Named CPU events include Slate tick/paint timings; retain CPU/GPU/frame trace for Insights.
    if($TraceHud){$arguments+=@('-trace=cpu,gpu,frame','-statnamedevents',('-tracefile="'+(Join-Path $OutputDirectory "runtime-$fps.utrace")+'"'))}
    $commands=@()
    if(-not $FullCostRendering){$commands+='sg.GlobalIlluminationQuality 0,sg.ReflectionQuality 0,sg.ShadowQuality 0,sg.AntiAliasingQuality 0,r.ScreenPercentage 50'}
    if($ExecCmds){$commands+=$ExecCmds}
    if($commands){$arguments+=('-ExecCmds="'+($commands -join ',')+'"')}
    $process=Start-Process -FilePath $exe -ArgumentList $arguments -WorkingDirectory $root -WindowStyle Hidden -PassThru
    # The combined suites (and -HudCaptures) run longer than one suite used to.
    if(-not $process.WaitForExit(900000)){throw "Probe did not finish; inspect owned process $($process.Id) and $output.log"}
    if(-not(Test-Path -LiteralPath $output)){throw "Probe produced no report: $output"}
    $report=Get-Content -Raw -LiteralPath $output | ConvertFrom-Json
    [pscustomobject]@{Cap=$fps;MeanFPS=$report.observed_mean_fps;Failures=$report.failures;Report=$output}
    if($report.failures -gt 0 -or $process.ExitCode -ne 0){throw "Runtime checks failed (exit $($process.ExitCode)): $output"}
    $captures=@(Get-ChildItem -LiteralPath $OutputDirectory -Filter "runtime-$fps*.png")
    if($captures.Count -eq 0 -and -not($ViewsScout -and $Suite -eq 'views') -and $Suite -ne 'swim'){throw "Runtime produced no rendered capture: $output"}
    foreach($capture in $captures){
        $bytes=[IO.File]::ReadAllBytes($capture.FullName)
        $actualWidth=([int]$bytes[16] -shl 24)+([int]$bytes[17] -shl 16)+([int]$bytes[18] -shl 8)+[int]$bytes[19]
        $actualHeight=([int]$bytes[20] -shl 24)+([int]$bytes[21] -shl 16)+([int]$bytes[22] -shl 8)+[int]$bytes[23]
        if($actualWidth -ne $Width -or $actualHeight -ne $Height){throw "Capture size mismatch: $($capture.Name) is ${actualWidth}x${actualHeight}; expected ${Width}x${Height}"}
    }
}
