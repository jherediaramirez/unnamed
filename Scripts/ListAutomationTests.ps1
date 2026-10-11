[CmdletBinding()]
param([string]$Prefix='Unnamed',[string]$SourceRoot)
# Lists the automation tests Source/ declares under a prefix ('Unnamed.Voxel' = that name and every 'Unnamed.Voxel.*'), sorted.
# The suites compare this list with the tests that ran instead of a hard-coded count. Only IMPLEMENT_SIMPLE_AUTOMATION_TEST
# at the start of a line, outside /* */ comments, counts (a // one doesn't); complex tests and specs run a variable number of
# tests per declaration, so one under the prefix stops the count. #if 0 blocks aren't understood.
$ErrorActionPreference='Stop'
if(-not $SourceRoot){$SourceRoot=Join-Path (Split-Path $PSScriptRoot -Parent) 'Source'}
$declaration='(?m)^[ \t]*(IMPLEMENT_\w+?_AUTOMATION_TEST|BEGIN_DEFINE_SPEC|DEFINE_SPEC)\s*\(\s*\w+\s*,\s*(?:\w+\s*,\s*)?"([^"]+)"'
$under='^'+[regex]::Escape($Prefix)+'(\.|$)'
$names=foreach($file in Get-ChildItem -LiteralPath $SourceRoot -Recurse -File -Include *.cpp,*.h){
    $text=[regex]::Replace([IO.File]::ReadAllText($file.FullName),'(?s)/\*.*?\*/','')
    foreach($match in [regex]::Matches($text,$declaration)){
        $name=$match.Groups[2].Value
        if($name -cnotmatch $under){continue}
        if($match.Groups[1].Value -cne 'IMPLEMENT_SIMPLE_AUTOMATION_TEST'){throw "Can't count $name from the source ($($match.Groups[1].Value)): $($file.FullName)"}
        $name
    }
}
@($names | Sort-Object -Unique -CaseSensitive)
