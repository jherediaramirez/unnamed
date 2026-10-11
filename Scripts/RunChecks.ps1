[CmdletBinding()]
param(
    [Parameter(Mandatory)][ValidateSet('quick','final','merge')][string]$Profile,
    [string]$ExpectedHashes='C:\dev\tools\shift-managers\expected-hashes-main.txt',
    [ValidateSet('off','smoke')][string]$Flight='off',
    # The native step's allocator, for crash hunts: stomp = -stompmalloc (slow, catches use-after-free), ansi = -ansimalloc.
    [ValidateSet('default','stomp','ansi')][string]$Allocator='default',
    [int]$LaunchTimeoutMinutes=60,
    [string[]]$Skip=@(),
    [switch]$KeepGoing,
    [string]$Base,
    [string]$CancelFile,
    [int]$MaxWaitMinutes=120,
    # The quiet window's lock; tests point it at a sandbox file, never at the real one.
    [string]$QuietLock='C:\dev\sandbox\locks\quiet-window.lock',
    [string]$EngineRoot='C:\Program Files\Epic Games\UE_5.8')
# The shared check runner: one command instead of each session's own chain.
#   & Scripts\RunChecks.ps1 -Profile quick|final|merge [-ExpectedHashes <file>] [-Flight smoke] [-Skip <steps>] [-KeepGoing] [-Allocator stomp|ansi] [-LaunchTimeoutMinutes 60]
# Profiles:
#   quick  build; TestVoxel; Mesher.Digest against the expected hashes; npm test; source-check.
#   final  build -DisableAdaptiveUnity; the full native set (every declared Unnamed test must run; a run that leaves no report is
#          retried once, and every crash is counted); TestVoxel; the radius-8 hashes and Mesher.Digest against the expected
#          hashes; the Playground runtime (all = the probe, interaction, tour; the fps is a figure, not a gate); create-world;
#          npm test; source-check; the Reference verifiers.
#   merge  final, plus HEAD's parents and a scan for conflict markers and unmerged files.
# -Flight smoke adds CaptureVoxel's flight: it completes, with no errors besides the engine's LogPython ones. -Skip leaves steps
# out (the summary names them); -KeepGoing runs the rest after a failure (by default the first failure stops the run).
# Before every launch, through Scripts\QuietWindow.ps1 (the gate every chain shares): stop if the cancel file exists (default
# C:\dev\sandbox\locks\cancel-<worktree folder>.flag), wait while the quiet-window lock (-QuietLock) holds this worktree (another
# lane's window, or ours while it clears; inside our own open window the launch goes and its line says so), and log the machine
# state. Windowed -game runs also wait while the CEF lock (cef-e2e.lock) exists; create-world takes it once no other -game
# UnrealEditor runs, and removes it afterwards.
# Every suite gets a fresh folder under Tests/Evidence/<today>: <Kind>-yyyyMMdd-HHmmss, the runtime's Playground-<suite>/Runtime-*.
# The summary (Checks-<profile>-<stamp>/summary.txt, made after the build so it is never older than the DLLs) gives pass or fail per
# step; the hashes against the expected file (lines '<key> 0x<hash>'; a missing file fails the step; hashes.txt has every hash
# the run produced, in that format); the time chain (the newest source changed since the merge base with -Base, by default the main
# folder's branch, < its DLL < each suite); and every folder, ending with the list for verify_lane.ps1 -Suites. Exit 0 = green, 1 = red, 2 = cancelled.
$ErrorActionPreference='Stop'
# pwsh -File passes 'a,b' as one string.
$Skip=@($Skip -split ',' | ForEach-Object { $_.Trim() } | Where-Object { $_ })
$steps='merge','build','native','voxel','hashes','runtime','runtime-all','runtime-interaction','runtime-tour','createworld','flight','npm','sourcecheck','verifiers'
foreach($name in $Skip){if($name -notin $steps){throw "-Skip takes $($steps -join ', '); not $name"}}
$root=Split-Path $PSScriptRoot -Parent
. (Join-Path $PSScriptRoot 'QuietWindow.ps1')
$locks='C:\dev\sandbox\locks'; $cefLock="$locks\cef-e2e.lock"
if(-not(Test-Path -LiteralPath $locks)){New-Item -ItemType Directory -Path $locks | Out-Null}
if(-not $CancelFile){$CancelFile=Join-Path $locks "cancel-$(Split-Path $root -Leaf).flag"}
# The lanes' "main" is the branch checked out in the repository's main folder (the first worktree), not the main branch.
if(-not $Base){
    foreach($line in @(git -C $root worktree list --porcelain)){
        if(-not $line){break}
        if($line -match '^HEAD (\w+)$'){$Base=$Matches[1]}
        if($line -match '^branch refs/heads/(.+)$'){$Base=$Matches[1]}
    }
}
$lane="$(Split-Path $root -Leaf) ($(git -C $root branch --show-current))"
$evidence=Join-Path $root ('Tests\Evidence\'+(Get-Date -Format 'yyyy-MM-dd'))
$editorCmd="$EngineRoot\Engine\Binaries\Win64\UnrealEditor-Cmd.exe"
$started=Get-Date
$buffer=[Collections.Generic.List[string]]::new()
$run=$null; $summary=$null; $buildLog=$null
$suites=[Collections.Generic.List[string]]::new()
$others=[Collections.Generic.List[string]]::new()
$results=[ordered]@{}
$hashes=[ordered]@{}

function Say([string]$Text){
    Write-Host $Text
    if($script:summary){Add-Content -LiteralPath $script:summary -Value $Text}else{$script:buffer.Add($Text)}
}
function T($Time){$Time.ToString('HH:mm:ss')}
function Machine{
    $list=@(Get-Process java,javaw,UnrealEditor* -ErrorAction SilentlyContinue | ForEach-Object { "$($_.Name)#$($_.Id)" })
    $list+=@(Get-Process UnrealBuildTool,ShaderCompileWorker -ErrorAction SilentlyContinue | Group-Object Name | ForEach-Object { "$($_.Name) x$($_.Count)" })
    if($list.Count){$list -join ' '}else{'none'}
}
# A fresh evidence folder name; the suites make their own folders and refuse existing ones.
function NewSuite([string]$Kind){
    do{$path=Join-Path $evidence "$Kind-$(Get-Date -Format 'yyyyMMdd-HHmmss')"; if(Test-Path -LiteralPath $path){Start-Sleep -Seconds 1}}while(Test-Path -LiteralPath $path)
    $path
}
function Keep([string]$Path){if(Test-Path -LiteralPath $Path){$script:suites.Add($Path)}}
function OpenRun{
    if($script:run){return}
    $script:run=NewSuite "Checks-$Profile"
    New-Item -ItemType Directory -Path $script:run | Out-Null
    $script:summary=Join-Path $script:run 'summary.txt'
    Set-Content -LiteralPath $script:summary -Value $script:buffer
    if($script:buildLog -and (Test-Path -LiteralPath $script:buildLog)){
        Move-Item -LiteralPath $script:buildLog -Destination (Join-Path $script:run 'build.log')
        $script:buildLog=Join-Path $script:run 'build.log'
        Say "   build log: $script:buildLog"
    }
}
# Before every launch: the cancel file, the quiet window, and for -game runs the CEF lock (-Slot: also no other -game run).
function Gate([string]$Name,[switch]$Game,[switch]$Slot){
    $window=Wait-QuietWindow -Name $Name -Lane (Split-Path $root -Leaf) -LockFile $QuietLock -CancelFile $CancelFile -CefLock $cefLock -Game:$Game -Slot:$Slot -MaxWaitMinutes $MaxWaitMinutes -Log {param($Text) Say "   $Text"}
    Say "== $Name at $(T (Get-Date)); machine: $(Machine)$(if($window){"; $window"})"
    StartWatch $Name
}
# Each launch gets -LaunchTimeoutMinutes: then a watchdog stops this worktree's UnrealEditor processes started since the launch
# (a hung editor, or a hung node or npm waiting on one), and the step fails, naming them.
function StartWatch([string]$Name){
    StopWatch
    $script:watchName=$Name; $script:watchFlag=Join-Path ([IO.Path]::GetTempPath()) "RunChecks-timeout-$([guid]::NewGuid()).txt"
    $script:watch=Start-ThreadJob -ArgumentList $LaunchTimeoutMinutes,"$root\Unnamed.uproject",(Get-Date).AddSeconds(-5),$script:watchFlag -ScriptBlock {
        param($Minutes,$Project,$Since,$Flag)
        Start-Sleep -Seconds ($Minutes*60)
        $stuck=@(Get-CimInstance Win32_Process -Filter "Name like 'UnrealEditor%'" | Where-Object { "$($_.CommandLine)".Replace('/','\') -like "*$Project*" -and $_.CreationDate -ge $Since })
        Set-Content -LiteralPath $Flag -Value $(if($stuck.Count){($stuck | ForEach-Object { "$($_.Name)#$($_.ProcessId)" }) -join ' '}else{'no editor of this worktree'})
        foreach($process in $stuck){Stop-Process -Id $process.ProcessId -Force -ErrorAction SilentlyContinue}
    }
}
function StopWatch{
    if($script:watch){Stop-Job -Job $script:watch; Remove-Job -Job $script:watch -Force; $script:watch=$null}
    if($script:watchFlag -and (Test-Path -LiteralPath $script:watchFlag)){
        $stopped="$(Get-Content -LiteralPath $script:watchFlag -Raw)".Trim()
        # The cause goes first in the step's line.
        $text="$script:watchName timed out after $LaunchTimeoutMinutes min; stopped: $stopped"
        $script:stepBad=@($text)+$script:stepBad; Say "   FAIL: $text"
        Remove-Item -LiteralPath $script:watchFlag -Force -ErrorAction SilentlyContinue
    }
    $script:watchFlag=$null
}
function Bad([string]$Text){$script:stepBad+=$Text; Say "   FAIL: $Text"}
function Step([string]$Name,[scriptblock]$Body){
    if($Name -in $Skip -or ($Name -split '-')[0] -in $Skip){$script:results[$Name]='SKIPPED (-Skip)'; Say "-- ${Name}: SKIPPED (-Skip)"; return}
    $script:stepBad=@(); $script:stepNote=''; $clock=[Diagnostics.Stopwatch]::StartNew()
    try{$null=& $Body}
    catch [OperationCanceledException]{StopWatch; Say "CANCELLED $($_.Exception.Message)"; Finish 2}
    catch{Bad "threw: $($_.Exception.Message)"}
    StopWatch
    $verdict=if($script:stepBad.Count){'FAIL'}else{'PASS'}
    $script:results[$Name]="$verdict ($([int]$clock.Elapsed.TotalSeconds) s$script:stepNote)$(if($script:stepBad.Count){': '+$script:stepBad[0]+$(if($script:stepBad.Count -gt 1){" (+$($script:stepBad.Count-1) more above)"})})"
    Say "-- ${Name}: $($script:results[$Name])"
    if($script:stepBad.Count -and -not $KeepGoing){Finish 1}
}
function AddHash([string]$Key,[string]$Value){
    if($script:hashes.Contains($Key) -and $script:hashes[$Key] -ne $Value){$script:hashes[$Key]="CONFLICT $($script:hashes[$Key]) / $Value"}else{$script:hashes[$Key]=$Value}
}
function TimeChain{
    Say 'Time chain:'
    $mergeBase=git -C $root merge-base $Base HEAD 2>$null
    if(-not $mergeBase){Say "  FLAG: no merge base with $Base"; $script:chainBad=$true; return}
    $changed=@(git -C $root diff --name-only $mergeBase -- Source)+@(git -C $root ls-files --others --exclude-standard -- Source)
    $dlls=@{}
    foreach($module in 'UnnamedVoxel','Unnamed'){
        $dll=Get-Item -LiteralPath "$root\Binaries\Win64\UnrealEditor-$module.dll" -ErrorAction SilentlyContinue
        if(-not $dll){Say "  FLAG ${module}: no DLL"; $script:chainBad=$true; continue}
        $dlls[$module]=$dll.LastWriteTime
        $newest=$changed | Where-Object { $_ -match "^Source/$module/.*\.(cpp|h)$" } | ForEach-Object { Get-Item -LiteralPath (Join-Path $root $_) -ErrorAction SilentlyContinue } | Sort-Object LastWriteTime -Descending | Select-Object -First 1
        if(-not $newest){Say "  ${module}: no source changed since $Base ($($mergeBase.Substring(0,7))); DLL $(T $dll.LastWriteTime)"; continue}
        $ok=$newest.LastWriteTime -lt $dll.LastWriteTime
        if(-not $ok){$script:chainBad=$true}
        Say "  $(if(-not $ok){'FLAG '})${module}: newest changed source $($newest.Name) $(T $newest.LastWriteTime) $(if($ok){'<'}else{'>= (rebuild)'}) DLL $(T $dll.LastWriteTime)"
    }
    foreach($suite in $script:suites){
        $made=(Get-Item -LiteralPath $suite).CreationTime
        $late=@($dlls.Keys | Where-Object { $dlls[$_] -ge $made } | Sort-Object)
        if($late.Count){$script:chainBad=$true}
        Say "  $(if($late.Count){'FLAG '})$(T $made) $(if($late.Count){'< DLL '+($late -join ', ')+' (ran on an older build)'}else{'> both DLLs'}): $suite"
    }
}
function Finish([int]$Code){
    OpenRun
    Say ''
    TimeChain
    if($Code -eq 0 -and @($script:results.Values | Where-Object { $_ -like 'FAIL*' }).Count){$Code=1}
    if($Code -eq 0 -and $script:chainBad){$Code=1; Say '  the time chain is red'}
    Say 'Steps:'
    foreach($key in $script:results.Keys){Say ('  {0,-20} {1}' -f $key,$script:results[$key])}
    Say "Run and log folder: $script:run"
    foreach($other in $script:others){Say "Also kept (not a suite): $other"}
    Say "Suites for verify_lane.ps1 -Suites: '$($script:suites -join ',')'"
    Say "RESULT: $(switch($Code){0{'GREEN'}1{'RED'}default{'CANCELLED'}}) after $([int]((Get-Date)-$started).TotalMinutes) min; machine: $(Machine)"
    Say "Summary: $script:summary"
    exit $Code
}

Say "RunChecks -Profile $Profile, $lane, HEAD $(git -C $root log -1 --format='%h %s'), started $(Get-Date -Format 'yyyy-MM-dd HH:mm:ss')"
Say "worktree changes: $(@(git -C $root status --porcelain).Count); cancel file $CancelFile; quiet-window lock $QuietLock; CEF lock $cefLock"
Say "expected hashes $ExpectedHashes; flight $Flight; native allocator $Allocator; skip $(if($Skip.Count){$Skip -join ','}else{'none'}); keep going $([bool]$KeepGoing)"

if($Profile -eq 'merge'){
    Step 'merge' {
        $commits=@(git -C $root rev-list --parents -n 1 HEAD) -split ' ' | ForEach-Object { $_.Substring(0,7) }
        Say "   HEAD $($commits[0]) $(if($commits.Count -gt 2){'merges '+($commits[1..($commits.Count-1)] -join ' + ')}else{'is not a merge commit'})"
        $unmerged=@(git -C $root ls-files -u)
        $markers=@(git -C $root grep -n -I -E '^(<<<<<<<|>>>>>>>)( |$)' 2>$null)
        Say "   unmerged index entries $($unmerged.Count), conflict-marker lines $($markers.Count)"
        if($unmerged.Count){Bad "$($unmerged.Count) unmerged index entries"}
        if($markers.Count){Bad "conflict markers: $(($markers | Select-Object -First 5) -join '; ')"}
    }
}

Step 'build' {
    Gate 'build'
    $script:buildLog=Join-Path ([IO.Path]::GetTempPath()) "RunChecks-build-$([guid]::NewGuid()).log"
    $flags=@('-WaitMutex')+@(if($Profile -ne 'quick'){'-DisableAdaptiveUnity'})
    & "$EngineRoot\Engine\Build\BatchFiles\Build.bat" UnnamedEditor Win64 Development "-Project=$root\Unnamed.uproject" @flags *> $script:buildLog
    $code=$LASTEXITCODE
    Say "   Build.bat $($flags -join ' '): exit $code; $((Select-String -LiteralPath $script:buildLog -Pattern '^Result: |Target is up to date|Total execution time' | Select-Object -Last 1).Line)"
    if($code -ne 0){Bad "build exit $code"}
}
OpenRun

if($Profile -ne 'quick'){
    Step 'native' {
        $declared=@(& "$PSScriptRoot\ListAutomationTests.ps1" -Prefix 'Unnamed')
        $crashes=0
        foreach($attempt in 1,2){
            Gate "native Unnamed (attempt $attempt)"
            $dir=NewSuite 'Native'
            New-Item -ItemType Directory -Path $dir | Out-Null
            $userDir=Join-Path ([IO.Path]::GetTempPath()) ('UnnamedChecksProfile-'+[guid]::NewGuid())
            $clock=[Diagnostics.Stopwatch]::StartNew()
            $allocatorFlag=@(switch($Allocator){'stomp'{'-stompmalloc'}'ansi'{'-ansimalloc'}})
            & $editorCmd "$root\Unnamed.uproject" -unattended -nop4 -NullRHI '-ExecCmds=Automation RunTests Unnamed' '-TestExit=Automation Test Queue Empty' `
                "-ReportExportPath=$dir\Report" "-abslog=$dir\native.log" "-UserDir=$userDir/" @allocatorFlag *> "$dir\console.log"
            $code=$LASTEXITCODE
            Remove-Item -LiteralPath $userDir -Recurse -Force -ErrorAction SilentlyContinue
            if(Test-Path -LiteralPath "$dir\Report\index.json"){break}
            $crashes++
            $script:others.Add($dir)
            $sign=Select-String -LiteralPath "$dir\native.log" -Pattern 'Critical error|Fatal error|Unhandled Exception|EXCEPTION_|heap corruption' -ErrorAction SilentlyContinue | Select-Object -First 1
            Say "   CRASH: attempt $attempt left no report (exit $code, $([int]$clock.Elapsed.TotalSeconds) s)$(if($sign){': '+($sign.Line -replace '^.*\]\[\s*\d+\]','')}): $dir"
        }
        $script:stepNote="; native crashes $crashes"
        if($crashes -ge 2){Bad 'no report after 2 attempts'; return}
        Keep $dir
        $report=Get-Content -Raw -LiteralPath "$dir\Report\index.json" | ConvertFrom-Json
        $ran=@($report.tests | ForEach-Object fullTestPath)
        $missing=@($declared | Where-Object { $_ -cnotin $ran }); $extra=@($ran | Where-Object { $_ -cnotin $declared })
        $passed=$report.succeeded+$report.succeededWithWarnings
        Say "   native: $passed of $($declared.Count) declared passed (with warnings $($report.succeededWithWarnings)), failed $($report.failed), not run $($report.notRun), exit $code, $([int]$clock.Elapsed.TotalSeconds) s, attempt ${attempt}, crashes ${crashes}: $dir"
        foreach($test in @($report.tests | Where-Object state -eq 'Fail' | Select-Object -First 8)){Say "   failed: $($test.fullTestPath)"}
        if($missing.Count){Bad "declared but not run ($($missing.Count)): $(($missing | Select-Object -First 8) -join ', ')"}
        if($extra.Count){Bad "ran but not declared ($($extra.Count)): $(($extra | Select-Object -First 8) -join ', ')"}
        if($code -ne 0 -or $report.failed -ne 0 -or $passed -ne $declared.Count){Bad "native: $passed of $($declared.Count) passed, $($report.failed) failed, exit $code"}
    }
}

Step 'voxel' {
    Gate 'TestVoxel'
    $dir=NewSuite 'VoxelAutomation'
    $log=Join-Path $run 'testvoxel.log'
    $output=try{& "$PSScriptRoot\TestVoxel.ps1" -EngineRoot $EngineRoot -OutputDirectory $dir *>&1 | Out-String}catch{Bad $_.Exception.Message; ''}
    Set-Content -LiteralPath $log -Value $output
    Keep $dir
    $verdict=@($output -split "`r?`n" | Where-Object { $_ -match '^PASS: ' })
    if($verdict.Count){Say "   $($verdict[0]) (log $log)"}elseif(-not $script:stepBad.Count){Bad "no PASS line from TestVoxel (log $log)"}
    # Mesher.Digest's values, for the hash step.
    foreach($match in @(Select-String -LiteralPath "$dir\native.log" -Pattern 'LogAutomationController: (.+): mesh digest (0x[0-9a-f]{16})' -ErrorAction SilentlyContinue)){
        AddHash "digest $($match.Matches[0].Groups[1].Value)" $match.Matches[0].Groups[2].Value
    }
}

Step 'hashes' {
    if($Profile -ne 'quick'){
        # The three radius-8 presets in one headless editor (as the lanes' r8 scripts): no test matches the filter, so the
        # editor may not exit by itself once the hashes are out; it then gets 30 s before it is stopped.
        Gate 'radius-8 hashes'
        $dir=NewSuite 'Radius8'
        New-Item -ItemType Directory -Path $dir | Out-Null
        $userDir=Join-Path ([IO.Path]::GetTempPath()) ('UnnamedChecksProfile-'+[guid]::NewGuid())
        $commands=@(foreach($preset in 'overworld','large_biomes','amplified'){"voxel.Bench Columns 8 $preset"})+'Automation RunTests Unnamed.Voxel.NoSuchTestRunChecks'
        $process=Start-Process -FilePath $editorCmd -WindowStyle Hidden -PassThru -ArgumentList @("$root\Unnamed.uproject",'-unattended','-nop4','-NullRHI','-nosplash',
            ('"-ExecCmds='+($commands -join ', ')+'"'),'"-TestExit=Automation Test Queue Empty"',"-abslog=$dir\native.log","-UserDir=$userDir/")
        $deadline=(Get-Date).AddMinutes(25); $hashesOut=$false
        try{
            # Each preset logs its block hashes, then its biome hashes. Bench editors have sat idle after them (items S5, S7).
            while(-not $process.HasExited -and (Get-Date) -lt $deadline){
                Start-Sleep -Seconds 5
                if(@(Select-String -LiteralPath "$dir\native.log" -Pattern 'LogUnnamedVoxel: Display: Biome hashes:' -ErrorAction SilentlyContinue).Count -ge 3){$hashesOut=$true; $null=$process.WaitForExit(30000); break}
            }
        }finally{
            $byItself=$process.HasExited
            if(-not $byItself){Stop-Process -Id $process.Id -Force -ErrorAction SilentlyContinue}
            Remove-Item -LiteralPath $userDir -Recurse -Force -ErrorAction SilentlyContinue
        }
        $ending=if($byItself){'the editor exited by itself'}elseif($hashesOut){'the runner stopped the editor 30 s after the hashes'}else{'the runner stopped the editor at the 25-min limit'}
        Keep $dir
        $preset=$null
        foreach($line in Get-Content -LiteralPath "$dir\native.log" -ErrorAction SilentlyContinue){
            if($line -match 'LogUnnamedVoxel: Display: Voxel column bench, (\w+),'){$preset=$Matches[1]}
            elseif($preset -and $line -match 'LogUnnamedVoxel: Display: .*Block hashes, (\d+) columns: row by row on one thread (0x[0-9a-f]+), backwards across workers (0x[0-9a-f]+), (\d+) columns differ'){
                AddHash "r8 $preset" $(if($Matches[1] -eq '289' -and $Matches[2] -eq $Matches[3] -and $Matches[4] -eq '0'){$Matches[2]}else{"MISMATCH $($Matches[2]) / $($Matches[3]), $($Matches[4]) of $($Matches[1]) columns differ"})
                $preset=$null
            }
        }
        $radius=@($hashes.Keys | Where-Object { $_ -like 'r8 *' })
        Say "   radius 8: $(($radius | ForEach-Object { "$($_.Substring(3)) $($hashes[$_])" }) -join ', '); ${ending}: $dir"
        if($radius.Count -ne 3){Bad "radius 8 gave $($radius.Count) of 3 presets"}
        foreach($key in $radius){if($hashes[$key] -notmatch '^0x[0-9a-f]{16}$'){Bad "${key}: $($hashes[$key])"}}
    }
    $all=Join-Path $run 'hashes.txt'
    Set-Content -LiteralPath $all -Value @($hashes.Keys | ForEach-Object { "$_ $($hashes[$_])" })
    if(-not(Test-Path -LiteralPath $ExpectedHashes)){Bad "the expected-hash file is missing: $ExpectedHashes"; return}
    $expected=[ordered]@{}
    foreach($line in Get-Content -LiteralPath $ExpectedHashes){
        if($line -match '^\s*(#|$)'){continue}
        if($line -match '^\s*(.+?)\s+(0x[0-9a-fA-F]{16})\s*$'){$expected[$Matches[1]]=$Matches[2].ToLowerInvariant()}else{Bad "unreadable line in ${ExpectedHashes}: $line"}
    }
    if(-not $expected.Count){Bad "no hashes in $ExpectedHashes"}
    $equal=0; $notRun=0
    foreach($key in $expected.Keys){
        if($Profile -eq 'quick' -and $key -like 'r8 *'){$notRun++; continue}
        if($hashes[$key] -eq $expected[$key]){$equal++}else{Bad "${key}: expected $($expected[$key]), got $(if($hashes[$key]){$hashes[$key]}else{'nothing'})"}
    }
    $script:stepNote="; $equal of $($expected.Count) equal"
    Say "   $equal of $($expected.Count) expected hashes equal$(if($notRun){", $notRun radius-8 not run in quick"}) ($ExpectedHashes); every hash this run produced ($($hashes.Count)): $all"
}

if($Profile -ne 'quick'){
    foreach($suite in 'all','interaction','tour'){
        Step "runtime-$suite" {
            Gate "runtime $suite" -Game
            $dir=NewSuite "Playground-$suite\Runtime"
            $log=Join-Path $run "runtime-$suite.log"
            try{& "$PSScriptRoot\TestFoundationRuntime.ps1" -Editor -Suite $suite -FrameRates 60 -OutputDirectory $dir *> $log}catch{Bad $_.Exception.Message}
            Keep $dir
            $json=Join-Path $dir 'runtime-60.json'
            if(-not(Test-Path -LiteralPath $json)){Bad "no runtime-60.json (log $log)"; return}
            $report=Get-Content -Raw -LiteralPath $json | ConvertFrom-Json
            $script:stepNote="; $([math]::Round($report.observed_mean_fps,1)) fps"
            Say "   runtime ${suite}: $(@($report.checks).Count) checks, $($report.failures) failures, mean $([math]::Round($report.observed_mean_fps,1)) fps at the 60 cap (a figure, not a gate): $dir"
            if($report.failures -or $null -eq $report.failures -or -not @($report.checks).Count){Bad "runtime failures $($report.failures), checks $(@($report.checks).Count)"}
        }
    }

    Step 'createworld' {
        # The CEF slot: create-world attaches to 127.0.0.1:18946, which every windowed -game run opens.
        while($true){
            Gate 'create-world (CEF slot)' -Slot
            try{$stream=[IO.File]::Open($cefLock,[IO.FileMode]::CreateNew,[IO.FileAccess]::Write); break}
            catch [IO.IOException],[UnauthorizedAccessException]{Say '   the CEF lock appeared meanwhile; waiting again'}
        }
        $mine="$lane RunChecks create-world.mjs $(Get-Date -Format o) pid $PID"
        $code=$null
        try{
            try{$writer=[IO.StreamWriter]::new($stream); $writer.Write($mine)}finally{if($writer){$writer.Dispose()}else{$stream.Dispose()}}
            $dir=NewSuite 'CreateWorld'
            $log=Join-Path $run 'create-world.log'
            Push-Location (Join-Path $root 'Tests')
            try{node create-world.mjs --editor "--output=$dir" *> $log; $code=$LASTEXITCODE}finally{Pop-Location}
        }finally{
            # Ours, or ours half-written (empty or a prefix of our text) when the write failed.
            if((Test-Path -LiteralPath $cefLock) -and $mine.StartsWith("$(Get-Content -LiteralPath $cefLock -Raw -ErrorAction SilentlyContinue)")){Remove-Item -LiteralPath $cefLock -Force -ErrorAction SilentlyContinue}
        }
        $stillMine=(Test-Path -LiteralPath $cefLock) -and "$(Get-Content -LiteralPath $cefLock -Raw -ErrorAction SilentlyContinue)" -eq $mine
        Keep $dir
        $file=Join-Path $dir 'results.json'
        $checks=if(Test-Path -LiteralPath $file){@((Get-Content -Raw -LiteralPath $file | ConvertFrom-Json).checks)}else{@()}
        $pass=@($checks | Where-Object pass).Count; $fail=$checks.Count-$pass
        Say "   create-world: exit $code, $pass checks pass, $fail fail; our CEF lock removed $(-not $stillMine) (log $log): $dir"
        if($code -ne 0 -or $fail -or -not $checks.Count){Bad "create-world exit $code, $pass pass, $fail fail"}
        if($stillMine){Bad "our CEF lock is still there: $cefLock"}
    }

    if($Flight -eq 'smoke'){
        Step 'flight' {
            Gate 'flight (smoke)' -Game
            $dir=NewSuite 'VoxelFlight'
            $log=Join-Path $run 'flight.log'
            try{& "$PSScriptRoot\CaptureVoxel.ps1" -Flight -OutputDirectory $dir *> $log}catch{Bad $_.Exception.Message}
            Keep $dir
            # The engine's EditorToolset plugin logs a LogPython traceback at every start; any other error fails the smoke check.
            $errors=@(Select-String -LiteralPath (Join-Path $dir 'game.log') -Pattern '\]Log\w+: Error: ' -ErrorAction SilentlyContinue | Where-Object { $_.Line -notmatch '\]LogPython: Error: ' })
            Say "   flight: $((Select-String -LiteralPath $log -Pattern '^PASS: ' | Select-Object -First 1).Line); $($errors.Count) error lines besides LogPython: $dir"
            if($errors.Count){Bad "$($errors.Count) error lines, first: $($errors[0].Line -replace '^.*\]\[\s*\d+\]','')"}
        }
    }
}

Step 'npm' {
    Gate 'npm test'
    $log=Join-Path $run 'npm.log'
    Push-Location (Join-Path $root 'Tests'); try{npm test *> $log; $code=$LASTEXITCODE}finally{Pop-Location}
    $counts=@(Select-String -LiteralPath $log -Pattern '^\S+ (tests|pass|fail) (\d+)\s*$' | ForEach-Object { "$($_.Matches[0].Groups[1].Value) $($_.Matches[0].Groups[2].Value)" })
    Say "   npm test: exit $code; $($counts -join ', '): $log"
    if($code -ne 0){Bad "npm test exit $code"}elseif(-not @($counts -match '^pass [1-9]').Count){Bad 'npm test reported no passing tests'}
}

Step 'sourcecheck' {
    Gate 'source-check'
    $log=Join-Path $run 'source-check.log'
    Push-Location (Join-Path $root 'Tests'); try{node source-check.mjs *> $log; $code=$LASTEXITCODE}finally{Pop-Location}
    Say "   source-check: exit $code; $(Get-Content -LiteralPath $log | Where-Object { $_.Trim() } | Select-Object -Last 1): $log"
    if($code -ne 0){Bad "source-check exit $code"}
}

if($Profile -ne 'quick'){
    Step 'verifiers' {
        $verifiers=@(Get-ChildItem -Path (Join-Path $root 'Reference\*\tools\verify_reference.py'))
        if(-not $verifiers.Count){Bad 'no Reference\*\tools\verify_reference.py found'}
        foreach($verifier in $verifiers){
            $name=$verifier.Directory.Parent.Name
            Gate "verifier $name"
            $log=Join-Path $run "verify-$name.log"
            Push-Location $root; try{uv run --no-project --python 3.13 --with pillow python -I "Reference/$name/tools/verify_reference.py" *> $log; $code=$LASTEXITCODE}finally{Pop-Location}
            Say "   ${name}: exit $code; $(Get-Content -LiteralPath $log | Where-Object { $_.Trim() } | Select-Object -Last 1)"
            if($code -ne 0){Bad "$name verifier exit $code (log $log)"}
        }
    }
}

Finish 0
