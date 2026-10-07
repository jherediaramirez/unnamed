[CmdletBinding()]
param([string]$EngineRoot='C:\Program Files\Epic Games\UE_5.8')
$ErrorActionPreference='Stop'
$root=Split-Path $PSScriptRoot -Parent
# Evidence is committed under Tests\Evidence\<date>\ (see Tests\evidence.mjs).
$evidence=New-Item -ItemType Directory -Force (Join-Path $root ('Tests\Evidence\'+(Get-Date -Format 'yyyy-MM-dd')))
$report=Join-Path $evidence ('InputAutomation-'+(Get-Date -Format 'yyyyMMdd-HHmmss'))
& "$EngineRoot\Engine\Binaries\Win64\UnrealEditor-Cmd.exe" "$root\Unnamed.uproject" -unattended -nop4 -NullRHI `
    '-ExecCmds=Automation RunTests Unnamed.Input' '-TestExit=Automation Test Queue Empty' `
    "-ReportExportPath=$report" "-abslog=$report.log" "-UserDir=$report-runtime/" *> "$report.console.log"
$exitCode=$LASTEXITCODE
if(!(Test-Path -LiteralPath "$report\index.json")){throw "No native report. Exit $exitCode; see $report.log"}
$result=Get-Content -LiteralPath "$report\index.json" -Raw | ConvertFrom-Json
if($exitCode -ne 0 -or $result.failed -ne 0 -or $result.succeeded -ne 3){throw "Input automation failed: $report\index.json"}
"PASS: 3 native input suites. Report: $report\index.json"
