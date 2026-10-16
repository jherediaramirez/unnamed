param([Parameter(Mandatory)][string]$Script, [Parameter(Mandatory)][string]$Tag)
# One UnrealEditor-Cmd run for M0: quiet-window check, hidden, BelowNormal, WaitForExit. Never touches other editors.
$ErrorActionPreference = 'Stop'
$E = 'C:\dev\output\unnamed-player-export-v002c-20261016'
$q = pwsh -NoProfile -File C:\dev\tools\shift-managers\scripts\quiet_window.ps1 -Status 2>&1 | Out-String
"quiet_window: $q"
if ($q -notmatch '^\s*No quiet window') { throw "quiet window not clear; not starting: $q" }
$editor = 'C:\Program Files\Epic Games\UE_5.8\Engine\Binaries\Win64\UnrealEditor-Cmd.exe'
$log = Join-Path 'C:\dev\output\unnamed-player-export-v002c-20261016\evidence\final_20261016_015709' ("ue_{0}_{1}.log" -f $Tag, (Get-Date -Format 'yyyyMMdd_HHmmss'))
if (Test-Path $log) { throw "log exists" }
$argList = @('"C:\dev\sandbox\fpbody-m0\FpBodyM0.uproject"', '-run=pythonscript', "`"-script=$Script`"", "-abslog=`"$log`"", '-unattended', '-nosplash', '-nullrhi')
$p = Start-Process -FilePath $editor -ArgumentList $argList -PassThru -WindowStyle Hidden
$p.PriorityClass = 'BelowNormal'
$p.WaitForExit()
"exit $($p.ExitCode) log $log"
