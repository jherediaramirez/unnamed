[CmdletBinding()]
param([string]$EngineRoot='C:\Program Files\Epic Games\UE_5.8',[string]$OutputDirectory)
$ErrorActionPreference='Stop'
$root=Split-Path $PSScriptRoot -Parent
if(-not $OutputDirectory){$OutputDirectory=Join-Path $root ('Tests/Evidence/'+(Get-Date -Format 'yyyy-MM-dd')+'/HudAutomation-'+(Get-Date -Format 'yyyyMMdd-HHmmss'))}
if(Test-Path -LiteralPath $OutputDirectory){throw 'Use a fresh evidence directory.'}
New-Item -ItemType Directory -Path $OutputDirectory | Out-Null
$OutputDirectory=(Resolve-Path -LiteralPath $OutputDirectory).Path
# The expected tests are the ones the source declares, so lanes that add HUD tests change no number here.
$declared=@(& (Join-Path $PSScriptRoot 'ListAutomationTests.ps1') -Prefix 'Unnamed.Hud')
if(-not $declared.Count){throw 'No Unnamed.Hud tests declared in Source/.'}
& "$EngineRoot/Engine/Binaries/Win64/UnrealEditor-Cmd.exe" "$root/Unnamed.uproject" -unattended -nop4 -NullRHI `
    '-ExecCmds=Automation RunTests Unnamed.Hud' '-TestExit=Automation Test Queue Empty' `
    "-ReportExportPath=$OutputDirectory/Report" "-abslog=$OutputDirectory/native.log" "-UserDir=$OutputDirectory/profile/" *> "$OutputDirectory/console.log"
$code=$LASTEXITCODE
if(-not(Test-Path "$OutputDirectory/Report/index.json")){throw "Missing HUD report (exit $code)"}
$report=Get-Content -Raw "$OutputDirectory/Report/index.json" | ConvertFrom-Json
# Every declared HUD test runs and passes, with or without warnings, as in TestVoxel.ps1.
$expected=$declared.Count
$ran=@($report.tests | ForEach-Object fullTestPath)
$missing=@($declared | Where-Object { $_ -cnotin $ran })
$extra=@($ran | Where-Object { $_ -cnotin $declared })
$passed=$report.succeeded+$report.succeededWithWarnings
if($missing.Count -or $extra.Count){throw "Declared and run HUD tests differ ($expected declared, $($ran.Count) ran)$(if($missing.Count){'; not run: '+($missing -join ', ')})$(if($extra.Count){'; not declared: '+($extra -join ', ')}): $OutputDirectory"}
if($code -ne 0 -or $report.failed -ne 0 -or $passed -ne $expected){throw "HUD tests failed ($passed of $expected passed, exit $code): $OutputDirectory"}
"PASS: $expected HUD suites. $OutputDirectory/Report/index.json"
