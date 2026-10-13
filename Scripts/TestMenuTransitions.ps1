[CmdletBinding()]
# One menu-transitions run (Source/Unnamed/Menu/MenuTransitionProbe.cpp, dev-only): the game starts on the menu map and the
# probe drives the real page through every menu transition (start, Singleplayer, Play Selected World, the creative inventory,
# ESC and its Options screens, Return to Main Menu, Create New World, Quit), recording each frame's state (CSV) and, unless
# -NoShots, one screenshot per frame. Then Scripts/CheckMenuTransitions.py classes the frames, times the transitions and checks
# each one against the criteria for the switch values the run logged (summary.txt; exit 1 on a failed check).
#   & Scripts\TestMenuTransitions.ps1 -Label shots                     # frames + shots (about 130-700 ms of wall time per frame)
#   & Scripts\TestMenuTransitions.ps1 -Label timing -NoShots -Loops 3  # real-time ms, three trips
#   & Scripts\TestMenuTransitions.ps1 -Label fixed -FixedStep           # -UseFixedTimeStep -FPS=60
#   & Scripts\TestMenuTransitions.ps1 -Label today -Switches 'menu.Title.Keep=0;menu.World.WaitForPaint=0'
param([Parameter(Mandatory)][ValidatePattern('^[A-Za-z0-9_-]+$')][string]$Label,[switch]$NoShots,[ValidateRange(1,10)][int]$Loops=1,
    [switch]$FixedStep,
    # Switches read at BeginPlay, as one ';'-separated string of name=value: each becomes -ini:Engine:[ConsoleVariables]:name=value
    # (-ExecCmds runs only after the first map's BeginPlay).
    [string]$Switches='',
    [string[]]$ExtraArgs=@(),[ValidateRange(320,7680)][int]$Width=2560,[ValidateRange(240,4320)][int]$Height=1440,
    [int]$TimeoutMinutes=15,[string]$OutputDirectory,[switch]$NoCheck)
$ErrorActionPreference='Stop'
$root=Split-Path $PSScriptRoot -Parent
$out=if($OutputDirectory){$OutputDirectory}else{Join-Path $root "Tests\Evidence\$(Get-Date -Format 'yyyy-MM-dd')\Menu-transitions\$Label-$(Get-Date -Format 'yyyyMMdd-HHmmss')"}
if(Test-Path -LiteralPath $out){throw "Evidence folder exists (never overwritten): $out"}
New-Item -ItemType Directory -Path $out | Out-Null
& (Join-Path $PSScriptRoot 'QuietWindow.ps1') -Name "menu-transitions $Label" -Game
$before=@(Get-Process java,javaw,UnrealEditor*,UnrealBuildTool -ErrorAction SilentlyContinue)
$state="Before run $(Get-Date -Format o): Get-Process java,javaw,UnrealEditor*,UnrealBuildTool`n"+
    $(if($before.Count){$before|Format-Table -AutoSize Name,Id,StartTime|Out-String}else{"(none running)`n"})
$arguments=@(('"'+(Join-Path $root 'Unnamed.uproject')+'"'),'-game','-windowed',"-ResX=$Width","-ResY=$Height",'-ForceRes',
    '-RenderOffscreen','-unattended','-nosplash','-nosound','-MenuSettingsSessionOnly',('-TransitionProbe="'+(Join-Path $out 'tr')+'"'),
    ('-abslog="'+(Join-Path $out 'run.log')+'"'),('-UserDir="'+(Join-Path $out 'profile')+'"'),"-TransitionLoops=$Loops")
if($NoShots){$arguments+='-TransitionNoShots'}
if($FixedStep){$arguments+='-UseFixedTimeStep','-FPS=60'}
foreach($pair in @($Switches -split ';' | Where-Object {$_.Trim()})){
    if($pair.Trim() -notmatch '^[A-Za-z0-9_.]+=-?[0-9.]+$'){throw "Bad switch '$pair' (want name=value)"}
    $arguments+="-ini:Engine:[ConsoleVariables]:$($pair.Trim())"
}
$arguments+=$ExtraArgs
$started=Get-Date
$p=Start-Process -FilePath 'C:\Program Files\Epic Games\UE_5.8\Engine\Binaries\Win64\UnrealEditor.exe' -ArgumentList $arguments -PassThru -WindowStyle Hidden
$state+="Started PID $($p.Id) at $($started.ToString('o'))`n"
if(-not $p.WaitForExit($TimeoutMinutes*60000)){Stop-Process -Id $p.Id -Force; $state+="TIMEOUT: killed PID $($p.Id)`n"}
$ended=Get-Date
$state+="Exited $($ended.ToString('o')) code $($p.ExitCode); run took $([int]($ended-$started).TotalSeconds) s; args: $($arguments -join ' ')`n"
$crashes=Join-Path $out 'profile\Saved\Crashes'
$state+="Crash folders: $(@(Get-ChildItem -LiteralPath $crashes -Directory -ErrorAction SilentlyContinue).Count)`n"
Set-Content -LiteralPath (Join-Path $out 'machine-state.txt') -Value $state
$log=Join-Path $out 'run.log'
$webErrors=@(Select-String -LiteralPath $log -Pattern 'LogWebBrowser: Error' -CaseSensitive -ErrorAction SilentlyContinue).Count
$lines=@($state,"LogWebBrowser errors: $webErrors")
$lines+=@(Select-String -LiteralPath $log -Pattern 'TRANSITION PROBE (act|done|event)|Loading screen (shown|hidden)|Pause menu (ready|shown)|Offline menu|Title |Switch |Settings flushed|Quit requested' -ErrorAction SilentlyContinue |
    ForEach-Object { $_.Line -replace '^\[[^\]]*\]\[\s*\d+\]','' })
$verdict=0
if(-not $NoCheck){
    $check=@(& uv run --no-project --python 3.13 --with pillow python -I (Join-Path $PSScriptRoot 'CheckMenuTransitions.py') $out 2>&1)
    $verdict=$LASTEXITCODE
    $lines+='',"CheckMenuTransitions.py exit $verdict",$check
}
if($p.ExitCode -ne 0 -or $webErrors){$verdict=1}
$lines+="OUT $out"
Set-Content -LiteralPath (Join-Path $out 'summary.txt') -Value $lines
$lines | Select-Object -Last 60
"Summary: $(Join-Path $out 'summary.txt')"
exit $verdict
