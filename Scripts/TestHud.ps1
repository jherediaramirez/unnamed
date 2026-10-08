[CmdletBinding()]
param([string]$EngineRoot='C:\Program Files\Epic Games\UE_5.8',[string]$OutputDirectory)
$ErrorActionPreference='Stop'
$root=Split-Path $PSScriptRoot -Parent
if(-not $OutputDirectory){$OutputDirectory=Join-Path $root ('Tests/Evidence/'+(Get-Date -Format 'yyyy-MM-dd')+'/HudAutomation-'+(Get-Date -Format 'yyyyMMdd-HHmmss'))}
if(Test-Path -LiteralPath $OutputDirectory){throw 'Use a fresh evidence directory.'}
New-Item -ItemType Directory -Path $OutputDirectory | Out-Null
$OutputDirectory=(Resolve-Path -LiteralPath $OutputDirectory).Path
& "$EngineRoot/Engine/Binaries/Win64/UnrealEditor-Cmd.exe" "$root/Unnamed.uproject" -unattended -nop4 -NullRHI `
    '-ExecCmds=Automation RunTests Unnamed.Hud' '-TestExit=Automation Test Queue Empty' `
    "-ReportExportPath=$OutputDirectory/Report" "-abslog=$OutputDirectory/native.log" "-UserDir=$OutputDirectory/profile/" *> "$OutputDirectory/console.log"
$code=$LASTEXITCODE
if(-not(Test-Path "$OutputDirectory/Report/index.json")){throw "Missing HUD report (exit $code)"}
$report=Get-Content -Raw "$OutputDirectory/Report/index.json" | ConvertFrom-Json
if($code -ne 0 -or $report.failed -ne 0 -or $report.succeeded -ne 7){throw "HUD tests failed: $OutputDirectory"}
"PASS: 7 HUD suites. $OutputDirectory/Report/index.json"
