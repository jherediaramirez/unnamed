[CmdletBinding()]
param([switch]$Editor,[string]$EngineRoot='C:\Program Files\Epic Games\UE_5.8',
    [ValidatePattern('^[A-Za-z0-9_-]+$')][string]$PackageName='InteractionDevelopment20261008-r3',
    [string]$OutputDirectory,[int[]]$FrameRates=@(30,60,120),[switch]$FullCostRendering,
    [ValidateRange(320,7680)][int]$Width=1280,[ValidateRange(240,4320)][int]$Height=720,
    [switch]$HudCaptures,[switch]$TraceHud,
    # One map (the Playground) runs every scenario: the movement course and input pipeline, the interaction
    # stations, both, or the screenshot tour (not part of all). Optional placement preset and finite fixture apply to the interaction stations.
    [ValidateSet('movement','interaction','all','tour')][string]$Suite='all',
    [ValidateSet('VanillaReference','LucidFluid')][string]$Preset,[switch]$Finite)
$ErrorActionPreference='Stop'
$root=Split-Path $PSScriptRoot -Parent
if(-not $OutputDirectory){$OutputDirectory=Join-Path $root ('Tests/Evidence/'+(Get-Date -Format 'yyyy-MM-dd')+"/Playground-$Suite/Runtime-"+(Get-Date -Format 'yyyyMMdd-HHmmss'))}
if(Test-Path -LiteralPath $OutputDirectory){throw "Evidence folder exists; choose a fresh output folder: $OutputDirectory"}
New-Item -ItemType Directory -Path $OutputDirectory | Out-Null
$OutputDirectory=(Resolve-Path -LiteralPath $OutputDirectory).Path
$exe=if($Editor){"$EngineRoot/Engine/Binaries/Win64/UnrealEditor.exe"}else{Join-Path $root "Packages/$PackageName/Windows/Unnamed/Binaries/Win64/Unnamed.exe"}
foreach($fps in $FrameRates){
    $output=Join-Path $OutputDirectory "runtime-$fps.json"
    if(Test-Path -LiteralPath $output){throw "Evidence exists; choose a fresh output folder: $output"}
    $arguments=@()
    if($Editor){$arguments+=('"'+(Join-Path $root 'Unnamed.uproject')+'"')}
    $arguments+='/Game/Maps/L_Playground'
    $arguments+="-FoundationSuite=$Suite"
    if($Preset){$arguments+="-InteractionPreset=$Preset"}
    if($Finite){$arguments+='-InteractionFinite'}
    if($Editor){$arguments+='-game'}
    # Offscreen acceptance captures must retain their requested size even above the desktop work area.
    $arguments+=@('-windowed',"-ResX=$Width","-ResY=$Height",'-ForceRes','-RenderOffscreen','-unattended','-nosplash','-nosound','-MenuSettingsSessionOnly',"-FoundationFPS=$fps",('-FoundationProbe="'+$output+'"'),('-abslog="'+(Join-Path $OutputDirectory "runtime-$fps.log")+'"'),('-UserDir="'+(Join-Path $OutputDirectory "profile-$fps")+'"'))
    if($HudCaptures){$arguments+='-HudCaptures'}
    # Named CPU events include Slate tick/paint timings; retain CPU/GPU/frame trace for Insights.
    if($TraceHud){$arguments+=@('-trace=cpu,gpu,frame','-statnamedevents',('-tracefile="'+(Join-Path $OutputDirectory "runtime-$fps.utrace")+'"'))}
    if(-not $FullCostRendering){$arguments+='-ExecCmds="sg.GlobalIlluminationQuality 0,sg.ReflectionQuality 0,sg.ShadowQuality 0,sg.AntiAliasingQuality 0,r.ScreenPercentage 50"'}
    $process=Start-Process -FilePath $exe -ArgumentList $arguments -WorkingDirectory $root -WindowStyle Hidden -PassThru
    # The combined suites (and -HudCaptures) run longer than one suite used to.
    if(-not $process.WaitForExit(900000)){throw "Probe did not finish; inspect owned process $($process.Id) and $output.log"}
    if(-not(Test-Path -LiteralPath $output)){throw "Probe produced no report: $output"}
    $report=Get-Content -Raw -LiteralPath $output | ConvertFrom-Json
    [pscustomobject]@{Cap=$fps;MeanFPS=$report.observed_mean_fps;Failures=$report.failures;Report=$output}
    if($report.failures -gt 0 -or $process.ExitCode -ne 0){throw "Runtime checks failed (exit $($process.ExitCode)): $output"}
    $captures=@(Get-ChildItem -LiteralPath $OutputDirectory -Filter "runtime-$fps*.png")
    if($captures.Count -eq 0){throw "Runtime produced no rendered capture: $output"}
    foreach($capture in $captures){
        $bytes=[IO.File]::ReadAllBytes($capture.FullName)
        $actualWidth=([int]$bytes[16] -shl 24)+([int]$bytes[17] -shl 16)+([int]$bytes[18] -shl 8)+[int]$bytes[19]
        $actualHeight=([int]$bytes[20] -shl 24)+([int]$bytes[21] -shl 16)+([int]$bytes[22] -shl 8)+[int]$bytes[23]
        if($actualWidth -ne $Width -or $actualHeight -ne $Height){throw "Capture size mismatch: $($capture.Name) is ${actualWidth}x${actualHeight}; expected ${Width}x${Height}"}
    }
}
