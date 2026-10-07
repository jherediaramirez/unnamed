[CmdletBinding()]
param([switch]$Editor,[string]$EngineRoot='C:\Program Files\Epic Games\UE_5.8',
    [ValidatePattern('^[A-Za-z0-9_-]+$')][string]$PackageName='FoundationDevelopment20261007-r3',
    [string]$OutputDirectory,[int[]]$FrameRates=@(30,60,120),[switch]$FullCostRendering)
$ErrorActionPreference='Stop'
$taskRoot=Split-Path $PSScriptRoot -Parent
if(-not $OutputDirectory){$OutputDirectory=Join-Path $taskRoot ('Tests/Foundation/Runtime-'+(Get-Date -Format 'yyyyMMdd-HHmmss'))}
New-Item -ItemType Directory -Path $OutputDirectory -Force | Out-Null
$OutputDirectory=(Resolve-Path -LiteralPath $OutputDirectory).Path
$taskExe=if($Editor){"$EngineRoot/Engine/Binaries/Win64/UnrealEditor.exe"}else{Join-Path $taskRoot "Packages/$PackageName/Windows/Unnamed/Binaries/Win64/Unnamed.exe"}
foreach($fps in $FrameRates){
    $output=Join-Path $OutputDirectory "runtime-$fps.json"
    if(Test-Path -LiteralPath $output){throw "Evidence exists; choose a fresh output folder: $output"}
    $taskArgs=@()
    if($Editor){$taskArgs+=('"'+(Join-Path $taskRoot 'Unnamed.uproject')+'"')}
    $taskArgs+='/Game/Maps/L_Foundation'
    if($Editor){$taskArgs+='-game'}
    $taskArgs+=@('-windowed','-ResX=1280','-ResY=720','-RenderOffscreen','-unattended','-nosplash','-nosound','-MenuSettingsSessionOnly',"-FoundationFPS=$fps",('-FoundationProbe="'+$output+'"'),('-abslog="'+(Join-Path $OutputDirectory "runtime-$fps.log")+'"'))
    if(-not $FullCostRendering){$taskArgs+='-ExecCmds="sg.GlobalIlluminationQuality 0,sg.ReflectionQuality 0,sg.ShadowQuality 0,sg.AntiAliasingQuality 0,r.ScreenPercentage 50"'}
    $taskProcess=Start-Process -FilePath $taskExe -ArgumentList $taskArgs -WorkingDirectory $taskRoot -WindowStyle Hidden -PassThru
    if(-not $taskProcess.WaitForExit(180000)){throw "Probe did not finish; inspect owned process $($taskProcess.Id) and $output.log"}
    if(-not(Test-Path -LiteralPath $output)){throw "Probe produced no report: $output"}
    $report=Get-Content -Raw -LiteralPath $output | ConvertFrom-Json
    [pscustomobject]@{Cap=$fps;MeanFPS=$report.observed_mean_fps;Failures=$report.failures;Report=$output}
    if($report.failures -gt 0){throw "Runtime checks failed: $output"}
}
