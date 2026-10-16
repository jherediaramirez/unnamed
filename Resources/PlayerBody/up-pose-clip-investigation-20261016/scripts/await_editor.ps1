param([Parameter(Mandatory)][string]$Script,[Parameter(Mandatory)][string]$Tag,[int]$Minutes=10)
$ErrorActionPreference='Stop'
$deadline=(Get-Date).AddMinutes($Minutes)
$last=''
while ((Get-Date) -lt $deadline) {
    $others=@(Get-CimInstance Win32_Process -Filter "Name = 'UnrealEditor-Cmd.exe' OR Name = 'UnrealEditor.exe'")
    $state=($others.ProcessId -join ',')
    if ($state -ne $last) { Write-Output "Editor lane occupied by: $state"; $last=$state }
    if ($others.Count -eq 0) {
        $q=pwsh -NoProfile -File C:\dev\tools\shift-managers\scripts\quiet_window.ps1 -Status 2>&1 | Out-String
        if ($LASTEXITCODE -eq 0 -and $q -match '^\s*No quiet window') {
            # The launcher rechecks both conditions immediately before starting.
            & (Join-Path $PSScriptRoot 'run_editor.ps1') -Script $Script -Tag $Tag
            exit $LASTEXITCODE
        }
    }
    Start-Sleep -Seconds 5
}
Write-Output 'DEFERRED: editor lane not available before deadline; no editor launched.'
exit 2
