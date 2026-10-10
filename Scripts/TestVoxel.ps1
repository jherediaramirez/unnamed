[CmdletBinding()]
param([string]$EngineRoot='C:\Program Files\Epic Games\UE_5.8',[string]$OutputDirectory)
$ErrorActionPreference='Stop'
$root=Split-Path $PSScriptRoot -Parent
if(-not $OutputDirectory){$OutputDirectory=Join-Path $root ('Tests/Evidence/'+(Get-Date -Format 'yyyy-MM-dd')+'/VoxelAutomation-'+(Get-Date -Format 'yyyyMMdd-HHmmss'))}
if(Test-Path -LiteralPath $OutputDirectory){throw 'Use a fresh evidence directory.'}
New-Item -ItemType Directory -Path $OutputDirectory | Out-Null
$OutputDirectory=(Resolve-Path -LiteralPath $OutputDirectory).Path
# The editor's per-run user folder is a cache (an asset registry of ~120 MB), not evidence: keep it out of the record.
$profileDirectory=Join-Path ([IO.Path]::GetTempPath()) ('UnnamedVoxelProfile-'+[guid]::NewGuid())
& "$EngineRoot/Engine/Binaries/Win64/UnrealEditor-Cmd.exe" "$root/Unnamed.uproject" -unattended -nop4 -NullRHI `
    '-ExecCmds=voxel.Info,voxel.Seed,voxel.Bench Noise,Automation RunTests Unnamed.Voxel' '-TestExit=Automation Test Queue Empty' `
    "-ReportExportPath=$OutputDirectory/Report" "-abslog=$OutputDirectory/native.log" "-UserDir=$profileDirectory/" *> "$OutputDirectory/console.log"
$code=$LASTEXITCODE
Remove-Item -LiteralPath $profileDirectory -Recurse -Force -ErrorAction SilentlyContinue
if(-not(Test-Path "$OutputDirectory/Report/index.json")){throw "Missing voxel report (exit $code)"}
$report=Get-Content -Raw "$OutputDirectory/Report/index.json" | ConvertFrom-Json
# Every voxel test, passing with or without warnings (an editor HTTP timeout lands on whichever test is running).
$expected=121
$passed=$report.succeeded+$report.succeededWithWarnings
# The console commands run before the tests: keep their output (settings, dev seed, timings) next to the report.
Select-String -Path "$OutputDirectory/native.log" -Pattern 'LogUnnamedVoxel: Display: ' | ForEach-Object { $_.Line -replace '^.*LogUnnamedVoxel: Display: ','' } | Set-Content "$OutputDirectory/voxel-console.txt"
if($code -ne 0 -or $report.failed -ne 0 -or $passed -ne $expected){throw "Voxel tests failed ($passed of $expected passed): $OutputDirectory"}
"PASS: $expected voxel suites. $OutputDirectory/Report/index.json"
Get-Content "$OutputDirectory/voxel-console.txt"
