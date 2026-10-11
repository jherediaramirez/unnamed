# The shared gate before every heavy launch (a build, an editor, a suite, a bench, a capture): quiet windows (handbook 4.7) hold
# every chain through one file, C:\dev\sandbox\locks\quiet-window.lock, with no HOLD message. RunChecks.ps1 uses it at every launch.
#   & Scripts\QuietWindow.ps1 -Name 'bench round 3'                   # one call before each launch (pwsh -File works too)
#   . Scripts\QuietWindow.ps1; Wait-QuietWindow -Name 'bench round 3'  # or dot-source it once and call the function
# Options: -Lane <worktree folder> -LockFile <path> -CancelFile <path> -Game -Slot -MaxWaitMinutes 120 -PollSeconds 15 -Log {param($Text) ...}
# 1. The cancel file (default C:\dev\sandbox\locks\cancel-<lane>.flag) stops the chain: it throws OperationCanceledException.
# 2. It waits while the lock holds this lane: the window is another lane's, or ours while it is still clearing (runs elsewhere are
#    finishing). Our own open window goes. The lane is this worktree's folder name (Unnamed, Unnamed-items, ...); -Lane overrides it.
# 3. -Game also waits while the CEF lock (cef-e2e.lock) exists, for windowed -game runs; -Slot also while another -game UnrealEditor runs.
# A stale lock is ignored, with a note: its watcher (quiet_window.ps1 -Open, the pid in the lock) is gone, or it is 5 min past its
# expiry; a lock with no watcher (written by hand) holds for 120 min after its last write. Waiting longer than -MaxWaitMinutes throws
# TimeoutException. Either throw stops a PowerShell chain (under pwsh -File: exit 1). On a go it returns a note when there is one
# (inside our window, or a stale lock ignored), else nothing.
# The lock is JSON from quiet_window.ps1: owner, state (clearing|open), reason, id, created/opened/expires (text), expiresUnix,
# watcherPid, watcherStartUnixMs.

# The window's hold on a lane: Hold = wait, Text = why (or the note on a go).
function Get-QuietWindowHold([string]$Lane,[string]$LockFile,[switch]$Again){
    try{$raw=[IO.File]::ReadAllText($LockFile); $written=[IO.File]::GetLastWriteTime($LockFile)}
    catch [IO.FileNotFoundException],[IO.DirectoryNotFoundException]{
        # File.Replace (the flip to open) may leave no file for an instant: look once more before going.
        if(-not $Again -and (Test-Path -LiteralPath (Split-Path $LockFile -Parent))){Start-Sleep -Milliseconds 200; return Get-QuietWindowHold $Lane $LockFile -Again}
        return [pscustomobject]@{Hold=$false; Text=''}
    }
    # quiet_window.ps1 swaps the file in place (File.Replace) when the window opens: a read can fail for a moment.
    catch{return [pscustomobject]@{Hold=$true; Text="quiet-window lock unreadable ($($_.Exception.Message.Trim()))"}}
    $now=[DateTimeOffset]::Now.ToUnixTimeSeconds()
    $lock=$null; try{$lock=$raw | ConvertFrom-Json -ErrorAction Stop}catch{}
    if(-not $lock.owner){
        $age=[int]((Get-Date)-$written).TotalMinutes
        if($age -ge 120){return [pscustomobject]@{Hold=$false; Text="ignored a quiet-window lock with no owner, written $age min ago"}}
        return [pscustomobject]@{Hold=$true; Text="quiet window (a lock with no owner: $("$raw".Trim() -replace '\s+',' '))"}
    }
    $what="quiet window for $($lock.owner) ($($lock.state)$(if($lock.reason){', '+$lock.reason}))"
    if($lock.watcherPid){
        $process=Get-Process -Id ([int]$lock.watcherPid) -ErrorAction SilentlyContinue
        $alive=[bool]$process
        if($alive -and $lock.watcherStartUnixMs){
            # A reused pid has another start time.
            try{$alive=[Math]::Abs([DateTimeOffset]::new($process.StartTime).ToUnixTimeMilliseconds()-[long]$lock.watcherStartUnixMs) -lt 2000}catch{}
        }
        if(-not $alive){return [pscustomobject]@{Hold=$false; Text="ignored a stale lock: $what, its watcher pid $($lock.watcherPid) is gone"}}
    }elseif(((Get-Date)-$written).TotalMinutes -ge 120){
        return [pscustomobject]@{Hold=$false; Text="ignored a stale lock: $what, no watcher, written $([int]((Get-Date)-$written).TotalMinutes) min ago"}
    }
    if($lock.expiresUnix -and $now -gt [long]$lock.expiresUnix+300){
        return [pscustomobject]@{Hold=$false; Text="ignored a stale lock: $what, expired at $($lock.expires)"}
    }
    if($lock.owner -eq $Lane -and $lock.state -eq 'open'){return [pscustomobject]@{Hold=$false; Text="inside our quiet window (open since $($lock.opened)$(if($lock.reason){', '+$lock.reason}))"}}
    [pscustomobject]@{Hold=$true; Text=$what}
}

function Wait-QuietWindow{
    param(
        [string]$Name='launch',
        [string]$Lane=(Split-Path (Split-Path $PSScriptRoot -Parent) -Leaf),
        [string]$LockFile='C:\dev\sandbox\locks\quiet-window.lock',
        [string]$CancelFile,
        [string]$CefLock='C:\dev\sandbox\locks\cef-e2e.lock',
        [switch]$Game,
        [switch]$Slot,
        [double]$MaxWaitMinutes=120,
        [int]$PollSeconds=15,
        [scriptblock]$Log={param($Text) Write-Host $Text})
    if(-not $CancelFile){$CancelFile="C:\dev\sandbox\locks\cancel-$Lane.flag"}
    $deadline=(Get-Date).AddMinutes($MaxWaitMinutes); $reported=''
    while($true){
        if(Test-Path -LiteralPath $CancelFile){throw [OperationCanceledException]::new("before ${Name}: $CancelFile")}
        $window=Get-QuietWindowHold $Lane $LockFile
        $why=@(
            if($window.Hold){$window.Text}
            if(($Game -or $Slot) -and (Test-Path -LiteralPath $CefLock)){'CEF lock: '+"$(Get-Content -LiteralPath $CefLock -Raw -ErrorAction SilentlyContinue)".Trim()}
            if($Slot -and @(Get-CimInstance Win32_Process -Filter "Name like 'UnrealEditor%'" | Where-Object { $_.CommandLine -match '(^|\s)-game(\s|$)' }).Count){'another -game UnrealEditor runs'}) -join '; '
        if(-not $why){break}
        if($why -ne $reported){$null=& $Log "$((Get-Date).ToString('HH:mm:ss')) waiting before ${Name}: $why"; $reported=$why}
        if((Get-Date) -gt $deadline){throw [TimeoutException]::new("gave up after $MaxWaitMinutes min waiting before ${Name}: $why")}
        Start-Sleep -Seconds $PollSeconds
    }
    if($window.Text){$window.Text}
}

if($MyInvocation.InvocationName -ne '.'){Wait-QuietWindow @args}
