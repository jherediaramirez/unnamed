# Rebuild Unnamed Player v001 end to end (PowerShell 7). Blender work runs headless in Blender's own Python;
# standalone helpers go through uv. The source GLB is only read. Never touches the Unreal repo.
#
#   pwsh -File scripts\run_all.ps1 [-Preset B_head40] [-SkipPreviews]
param([string]$Preset = 'B_head40', [switch]$SkipPreviews)
$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
$bl = 'C:\dev\tools\blender-5.2.2\blender.exe'
$env:UP_PRESET = $Preset
New-Item -ItemType Directory -Force (Join-Path $root 'logs') | Out-Null

function Run([string]$script, [string[]]$extra = @()) {
    $log = Join-Path $root ('logs\' + [IO.Path]::GetFileNameWithoutExtension($script) + '.log')
    Write-Host "== $script $($extra -join ' ')"
    $a = @('-b', '--factory-startup', '--python', (Join-Path $root "scripts\$script"))
    if ($extra.Count) { $a += '--'; $a += $extra }
    & $bl @a *> $log
    if ($LASTEXITCODE -ne 0 -or (Select-String -Path $log -Pattern 'Traceback' -Quiet)) { throw "$script failed, see $log" }
}

uv run --no-project --with pillow python (Join-Path $root 'scripts\tools\make_textures.py') $root
Run 's00_audit_source.py' @($root)
Run 's00b_uv_layout_probe.py' @($root)
Run 's01_build_master.py' @('--preset', $Preset)
Run 's02_rig_tests.py'
Run 's03_locomotion.py'
Run 's04_air_turns.py'
Run 's05_look_fp.py' $(if ($SkipPreviews) { @('--no-render') } else { @() })
Run 's06_pickaxe.py' $(if ($SkipPreviews) { @('--no-render') } else { @() })
Run 's07_export.py'
Run 's08_roundtrip.py'
Run 's09_review_scene.py'
if (-not $SkipPreviews) {
    Run 's03b_preview_locomotion.py' @('--sheets', '--videos', 'Walk_F,Walk_R,Walk_B,Walk_FR,Walk_BL,Sprint_F,Sprint_R,Sneak_F,Sneak_FR,Sneak_B')
    Run 's04b_preview_air_turns.py'
    foreach ($pp in 'A_uniform', 'C_head36', 'B_head40') { Run 's01_build_master.py' @('--preset', $pp, '--no-save', '--tag', "s01_$pp") }
    uv run --no-project --with pillow python (Join-Path $root 'scripts\tools\compose_pickaxe.py') $root
    uv run --no-project --with pillow python (Join-Path $root 'scripts\tools\compose_sheets.py') $root
}
Write-Host 'done'
