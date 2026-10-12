[CmdletBinding()]
param([string]$EngineRoot='C:\Program Files\Epic Games\UE_5.8')
$ErrorActionPreference='Stop'
$root=Split-Path $PSScriptRoot -Parent
# Evidence is committed under Tests\Evidence\<date>\ (see Tests\evidence.mjs).
$evidence=New-Item -ItemType Directory -Force (Join-Path $root ('Tests\Evidence\'+(Get-Date -Format 'yyyy-MM-dd')))
$report=Join-Path $evidence ('MenuAutomation-'+(Get-Date -Format 'yyyyMMdd-HHmmss'))
& "$EngineRoot\Engine\Binaries\Win64\UnrealEditor-Cmd.exe" "$root\Unnamed.uproject" -unattended -nop4 -NullRHI `
    '-ExecCmds=Automation RunTests Unnamed.Menu' '-TestExit=Automation Test Queue Empty' `
    "-ReportExportPath=$report" "-abslog=$report.log" *> "$report.console.log"
$code=$LASTEXITCODE
if(-not(Test-Path -LiteralPath "$report\index.json")){throw "Missing menu report (exit $code). See $report.log"}
$result=Get-Content -LiteralPath "$report\index.json" -Raw | ConvertFrom-Json
if($code -ne 0 -or $result.failed -ne 0 -or $result.succeeded -lt 2 -or @($result.tests | Where-Object {$_.state -ne 'Success'}).Count){
    throw "Menu automation failed (exit $code). See $report\index.json"
}
"PASS: $($result.succeeded) native menu tests. Report: $report\index.json"
