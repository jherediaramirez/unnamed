param([Parameter(Mandatory)][string]$Script, [Parameter(Mandatory)][string]$Tag)
$ErrorActionPreference = 'Stop'
$outDir = 'C:\dev\output\up-pose-clip-investigation-20261016'
$project = 'C:\dev\sandbox\up-posefix\PoseFix.uproject'
if (-not [IO.Path]::GetFullPath($Script).StartsWith($outDir + '\scripts\', [StringComparison]::OrdinalIgnoreCase)) { throw 'Script outside investigation' }
if (-not (Test-Path -LiteralPath $Script -PathType Leaf)) { throw 'Script missing' }
$q = pwsh -NoProfile -File C:\dev\tools\shift-managers\scripts\quiet_window.ps1 -Status 2>&1 | Out-String
if ($LASTEXITCODE -ne 0 -or $q -notmatch '^\s*No quiet window') { throw "DEFERRED: quiet window not clear: $q" }
$others = @(Get-CimInstance Win32_Process -Filter "Name = 'UnrealEditor-Cmd.exe' OR Name = 'UnrealEditor.exe'")
if ($others.Count -gt 0) { throw "DEFERRED: other editor PIDs $($others.ProcessId -join ', ')" }
$runFiles = @(Get-ChildItem -LiteralPath (Join-Path $outDir 'evidence') -Filter 'editor_run_*.json')
if ($runFiles.Count -ge 6) { throw 'Six editor run budget exhausted' }
$index = $runFiles.Count + 1
$stamp = Get-Date -Format 'yyyyMMdd_HHmmss'
$log = Join-Path $outDir "evidence\ue_${index}_${Tag}_${stamp}.log"
$record = Join-Path $outDir "evidence\editor_run_${index}.json"
if ((Test-Path -LiteralPath $log) -or (Test-Path -LiteralPath $record)) { throw 'Evidence already exists' }
$args_ = @("`"$project`"", '-run=pythonscript', "`"-script=$Script`"", "`"-abslog=$log`"", '-unattended', '-nosplash', '-nullrhi', '-nop4')
$p = Start-Process -FilePath 'C:\Program Files\Epic Games\UE_5.8\Engine\Binaries\Win64\UnrealEditor-Cmd.exe' -ArgumentList $args_ -PassThru -WindowStyle Hidden
$p.PriorityClass = 'BelowNormal'
$r = @{index=$index;tag=$Tag;pid=$p.Id;start=(Get-Date -Format o);script=$Script;script_sha256=(Get-FileHash -LiteralPath $Script -Algorithm SHA256).Hash;project=$project;log=$log;quiet_status=$q;other_editors_before=0;priority=$p.PriorityClass.ToString()}
$r | ConvertTo-Json | Set-Content -LiteralPath $record -Encoding utf8
Write-Output "Started run $index PID $($p.Id), $log"
$p.WaitForExit()
$r.exit_code=$p.ExitCode
$r.end=Get-Date -Format o
$r | ConvertTo-Json | Set-Content -LiteralPath $record -Encoding utf8
Write-Output "Finished run $index exit $($p.ExitCode)"
exit $p.ExitCode
