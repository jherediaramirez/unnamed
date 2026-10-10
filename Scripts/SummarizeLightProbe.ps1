[CmdletBinding()]
param([Parameter(Mandatory)][string]$Folder)
# Summarizes the GPU profiles of a CaptureVoxel.ps1 -Lights run (FVoxelLightShots logs "LightShots ProfileGPU <case>"
# before each ProfileGPU dump) into lights-passes.txt: per case, the passes that prove which light path ran.
# - MegaLights: inclusive ms of the "MegaLights" events; they appear only with MegaLights on and lights shown.
# - Per-light deferred passes ("RenderLight Light::...: Voxel world"): the base case's one (the sun) plus one per
#   probe light drawn outside MegaLights, so vsm and none must add exactly the lights whose reach meets the view
#   frustum (the driver counts them) and megalights none.
# - VSM: "VirtualShadowMapProjection" events: the sun's, plus one pass for all shadowed local lights (vsm only), and
#   ShadowDepths' inclusive ms.
# A case without its signature is invalid, not cheap.
$ErrorActionPreference='Stop'
$log=Get-Content -LiteralPath (Join-Path $Folder 'game.log')
$marks=@($log | Select-String -Pattern 'LightShots ProfileGPU (\S+): (\d+) lights shown, (\d+) in view, MegaLights (\d)')
if(-not $marks){throw "No LightShots ProfileGPU markers in $Folder"}
$rows=@()
for($k=0;$k -lt $marks.Count;$k++){
    $case=$marks[$k].Matches[0].Groups[1].Value
    $shown=[int]$marks[$k].Matches[0].Groups[2].Value
    $inView=[int]$marks[$k].Matches[0].Groups[3].Value
    $mega=[int]$marks[$k].Matches[0].Groups[4].Value
    $from=$marks[$k].LineNumber
    $to=if($k+1 -lt $marks.Count){$marks[$k+1].LineNumber-1}else{$log.Count}
    $s=@{Mega=0.0;MegaEvents=0;Shadow=0.0;Projections=0;PerLight=0;Batched=0;Frame=''}
    for($i=$from;$i -lt $to;$i++){
        $line=$log[$i]
        if($line -match 'LightShots ') {break}
        if(-not $s.Frame -and $line -match 'Frame Time\s*:\s*([\d.]+)ms'){$s.Frame=$Matches[1]}
        if($line -notmatch 'LogRHI: Display:'){continue}
        $parts=$line -split '┃'
        if($parts.Count -lt 4){continue}
        $name=$parts[3].Trim()
        $ms=if($parts[2] -match '([\d.]+) ms'){[double]$Matches[1]}else{0}
        if($name -match '^MegaLights($|\s|\()'){$s.Mega+=$ms;$s.MegaEvents++}
        elseif($name -eq 'ShadowDepths'){$s.Shadow+=$ms}
        elseif($name -like 'VirtualShadowMapProjection(*'){$s.Projections++}
        elseif($name -match '^RenderLight Light::.*: Voxel world'){$s.PerLight++}
        elseif($name -eq 'BatchedLights'){$s.Batched++}
    }
    $rows+=[pscustomobject]@{Case=$case;Shown=$shown;InView=$inView;MegaLights=$mega;MegaMs=$s.Mega;MegaEvents=$s.MegaEvents;ShadowDepthsMs=$s.Shadow;VsmProjections=$s.Projections;PerLight=$s.PerLight;Batched=$s.Batched;FrameMs=$s.Frame}
}
$base=$rows | Where-Object Case -eq 'base' | Select-Object -First 1
if(-not $base){throw 'No base case'}
$out=@('Light probe GPU profiles (one ProfileGPU per case, round 1)','',
    '| Case | Shown (in view) | MegaLights cvar | MegaLights events (ms) | VSM projections | ShadowDepths ms | Per-light passes (probe lights) | BatchedLights | Profiled frame ms | Signature |',
    '|---|---|---|---|---|---|---|---|---|---|')
foreach($r in $rows){
    $count=if($r.Case -match '-(\d+)$'){[int]$Matches[1]}else{0}
    $lights=$r.PerLight-$base.PerLight
    $mega=$r.Case -like 'megalights-*'
    $ok=(($r.MegaEvents -gt 0) -eq $mega) -and ($r.MegaLights -eq [int]($mega -or $r.Case -eq 'floor'))
    $ok=$ok -and ($lights -eq $(if($mega -or $count -eq 0){0}else{$r.InView}))
    $ok=$ok -and ($r.VsmProjections -eq $base.VsmProjections+[int]($r.Case -like 'vsm-*'))
    $note=if($count -and $r.Shown -ne $count){" (shown $($r.Shown) of $count)"}else{''}
    $out+=('| {0} | {1} ({13}) | {2} | {3} ({4:N3}) | {5} | {6:N3} | {7} ({12}) | {8} | {9} | {10}{11} |' -f $r.Case,$r.Shown,$r.MegaLights,$r.MegaEvents,$r.MegaMs,$r.VsmProjections,$r.ShadowDepthsMs,$r.PerLight,$r.Batched,$r.FrameMs,$(if($ok){'valid'}else{'INVALID'}),$note,$lights,$r.InView)
}
$out | Set-Content -LiteralPath (Join-Path $Folder 'lights-passes.txt') -Encoding utf8
$out
