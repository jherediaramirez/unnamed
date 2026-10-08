# Copies the Opus/Haiku agent team files into .claude so Claude Code loads them.
# Rerun after editing anything under Scripts\AgentTeam. See Docs\AgentTeam.md.
$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
$source = Join-Path $PSScriptRoot 'AgentTeam'
foreach ($kind in 'agents', 'commands') {
    $target = Join-Path $root ".claude\$kind"
    New-Item -ItemType Directory -Force -Path $target | Out-Null
    Get-ChildItem (Join-Path $source $kind) -Filter *.md | ForEach-Object {
        Copy-Item $_.FullName -Destination $target -Force
        Write-Host "Installed .claude\$kind\$($_.Name)"
    }
}
