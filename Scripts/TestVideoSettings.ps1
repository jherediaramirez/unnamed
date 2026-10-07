[CmdletBinding()]
param([string]$EngineRoot='C:\Program Files\Epic Games\UE_5.8')
$ErrorActionPreference='Stop'
$root=Split-Path $PSScriptRoot -Parent
# Evidence is committed under Tests\Evidence\<date>\ (see Tests\evidence.mjs).
$evidence=New-Item -ItemType Directory -Force (Join-Path $root ('Tests\Evidence\'+(Get-Date -Format 'yyyy-MM-dd')))
$report=Join-Path $evidence ('VideoAutomation-'+(Get-Date -Format 'yyyyMMdd-HHmmss'))
& "$EngineRoot\Engine\Binaries\Win64\UnrealEditor-Cmd.exe" "$root\Unnamed.uproject" -unattended -nop4 -NullRHI `
    '-ExecCmds=Automation RunTests Unnamed.Menu.VideoSettings' '-TestExit=Automation Test Queue Empty' `
    "-ReportExportPath=$report" "-abslog=$report.log" *> "$report.console.log"
$result=Get-Content -LiteralPath "$report\index.json" -Raw | ConvertFrom-Json
if($result.failed -ne 0 -or $result.succeeded -ne 1 -or $result.tests[0].state -ne 'Success'){
    throw "Video automation failed. See $report\index.json"
}
"PASS: native settings validation. Report: $report\index.json"
