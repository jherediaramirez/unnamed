[CmdletBinding()]
param([switch]$Editor,[string]$EngineRoot='C:\Program Files\Epic Games\UE_5.8',
    [ValidatePattern('^[A-Za-z0-9_-]+$')][string]$PackageName='CodeReviewDevelopment20261007',
    [string]$OutputDirectory,[int[]]$FrameRates=@(30,60,120),[switch]$FullCostRendering)
$ErrorActionPreference='Stop'
$root=Split-Path $PSScriptRoot -Parent
if(-not $OutputDirectory){$OutputDirectory=Join-Path $root ('Tests/Evidence/'+(Get-Date -Format 'yyyy-MM-dd')+'/Foundation/Runtime-'+(Get-Date -Format 'yyyyMMdd-HHmmss'))}
New-Item -ItemType Directory -Path $OutputDirectory -Force | Out-Null
$OutputDirectory=(Resolve-Path -LiteralPath $OutputDirectory).Path
$exe=if($Editor){"$EngineRoot/Engine/Binaries/Win64/UnrealEditor.exe"}else{Join-Path $root "Packages/$PackageName/Windows/Unnamed/Binaries/Win64/Unnamed.exe"}
foreach($fps in $FrameRates){
    $output=Join-Path $OutputDirectory "runtime-$fps.json"
    if(Test-Path -LiteralPath $output){throw "Evidence exists; choose a fresh output folder: $output"}
    $arguments=@()
    if($Editor){$arguments+=('"'+(Join-Path $root 'Unnamed.uproject')+'"')}
    $arguments+='/Game/Maps/L_Foundation'
    if($Editor){$arguments+='-game'}
    $arguments+=@('-windowed','-ResX=1280','-ResY=720','-RenderOffscreen','-unattended','-nosplash','-nosound','-MenuSettingsSessionOnly',"-FoundationFPS=$fps",('-FoundationProbe="'+$output+'"'),('-abslog="'+(Join-Path $OutputDirectory "runtime-$fps.log")+'"'))
    if(-not $FullCostRendering){$arguments+='-ExecCmds="sg.GlobalIlluminationQuality 0,sg.ReflectionQuality 0,sg.ShadowQuality 0,sg.AntiAliasingQuality 0,r.ScreenPercentage 50"'}
    $process=Start-Process -FilePath $exe -ArgumentList $arguments -WorkingDirectory $root -WindowStyle Hidden -PassThru
    if(-not $process.WaitForExit(180000)){throw "Probe did not finish; inspect owned process $($process.Id) and $output.log"}
    if(-not(Test-Path -LiteralPath $output)){throw "Probe produced no report: $output"}
    $report=Get-Content -Raw -LiteralPath $output | ConvertFrom-Json
    [pscustomobject]@{Cap=$fps;MeanFPS=$report.observed_mean_fps;Failures=$report.failures;Report=$output}
    if($report.failures -gt 0){throw "Runtime checks failed: $output"}
}
