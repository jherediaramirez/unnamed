[CmdletBinding()]
param([string]$EngineRoot='C:\Program Files\Epic Games\UE_5.8')
$ErrorActionPreference='Stop'
$taskRoot=Split-Path $PSScriptRoot -Parent
$taskReport=Join-Path $taskRoot ('Tests\InputAutomation-'+(Get-Date -Format 'yyyyMMdd-HHmmss'))
& "$EngineRoot\Engine\Binaries\Win64\UnrealEditor-Cmd.exe" "$taskRoot\Unnamed.uproject" -unattended -nop4 -NullRHI `
    '-ExecCmds=Automation RunTests Unnamed.Input' '-TestExit=Automation Test Queue Empty' `
    "-ReportExportPath=$taskReport" "-abslog=$taskReport.log" "-UserDir=$taskReport-runtime/" *> "$taskReport.console.log"
$taskExitCode=$LASTEXITCODE
if(!(Test-Path -LiteralPath "$taskReport\index.json")){throw "No native report. Exit $taskExitCode; see $taskReport.log"}
$taskResult=Get-Content -LiteralPath "$taskReport\index.json" -Raw | ConvertFrom-Json
if($taskExitCode -ne 0 -or $taskResult.failed -ne 0 -or $taskResult.succeeded -ne 3){throw "Input automation failed: $taskReport\index.json"}
"PASS: 3 native input suites. Report: $taskReport\index.json"
