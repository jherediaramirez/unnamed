[CmdletBinding()]
param(
    [ValidatePattern('^[A-Za-z0-9_-]+\.zip$')][string]$OutputName='Independent_Menu_Review_Packet.zip',
    [ValidatePattern('^[A-Za-z0-9_-]+\.json$')][string]$ManifestName='IndependentMenuReviewBundleManifest.json'
)
$ErrorActionPreference='Stop'
$root=Split-Path $PSScriptRoot -Parent
$output=Join-Path $root "Docs\Archive\$OutputName"
$manifestPath=Join-Path $root "Docs\Archive\$ManifestName"
if((Test-Path -LiteralPath $output) -or (Test-Path -LiteralPath $manifestPath)){
    throw 'Review snapshot already exists. Choose new output/manifest names to preserve it.'
}

$files=@('README.md','Unnamed.uproject','.gitignore','Content\Menu\Web\index.html')
foreach($folder in @('Source','Scripts','MenuWeb','Config')){
    $files+=Get-ChildItem -LiteralPath (Join-Path $root $folder) -File -Recurse |
        ForEach-Object {[IO.Path]::GetRelativePath($root,$_.FullName)}
}
$files+=@(
    'Docs\Architecture.md','Docs\VideoSettings.md','Docs\QuitGame.md',
    'Docs\Verification.md','Docs\KnownDifferences.md','Docs\ControlRegistry.json',
    'Docs\ControlRegistry.md','Docs\FunctionalBacklog.md','Docs\ScreenInventory.md',
    'Docs\Archive\Independent_Menu_Review_Prompt.txt',
    'Docs\Unreal_Menu_Interface_Prompt.txt',
    'Reference\OriginalHashes.json','Reference\minecraft-menu\index.html',
    'Reference\minecraft-menu\README.md','Reference\minecraft-menu\reference-inventory.md',
    'Reference\minecraft-menu\qa-report.md','Reference\minecraft-menu\qa-results.json',
    'Reference\minecraft-menu\assets\asset-provenance.json',
    'Tests\package.json','Tests\package-lock.json','Tests\Evidence\2026-10-07\NoRealmsUnitTests.tap',
    'Tests\Evidence\2026-10-07\NoRealmsGraphicsAPI.json','Tests\Evidence\2026-10-07\NoRealmsQuitResults.json',
    'Tests\Evidence\2026-10-07\NoRealmsArtifactManifest.json','Tests\Evidence\2026-10-07\QuitArtifactManifest.json',
    'Tests\Evidence\2026-10-07\QuitEditorSafetyResults.json','Tests\Evidence\2026-10-07\QuitBuild.log',
    'Tests\Evidence\2026-10-07\AdditionalControls.json','Tests\Evidence\2026-10-07\VideoResetControls.json',
    'Tests\Evidence\2026-10-07\MenuAutomation-20261007-091836\index.json'
)
$files+=Get-ChildItem -LiteralPath (Join-Path $root 'Tests') -File -Filter '*.mjs' |
    ForEach-Object {[IO.Path]::GetRelativePath($root,$_.FullName)}
$files+=Get-ChildItem -LiteralPath (Join-Path $root 'Reference\minecraft-menu') -File -Filter '*.jpg' |
    ForEach-Object {[IO.Path]::GetRelativePath($root,$_.FullName)}
foreach($capture in @('no-realms-packaged-1280x720','no-realms-packaged-1920x1080','no-realms-video-1280x720','no-realms-video-1920x1080')){
    $files+=Get-ChildItem -LiteralPath (Join-Path $root "Tests\Evidence\2026-10-07\Captures\$capture") -File -Recurse |
        ForEach-Object {[IO.Path]::GetRelativePath($root,$_.FullName)}
}
$files=$files | Sort-Object -Unique
# Validate the entire inclusion list before creating an immutable snapshot.
foreach($relative in $files){
    if(-not (Test-Path -LiteralPath (Join-Path $root $relative) -PathType Leaf)){
        throw "Required review input is missing: $relative"
    }
}

$archive=[IO.Compression.ZipFile]::Open($output,[IO.Compression.ZipArchiveMode]::Create)
$entries=@()
try{
    foreach($relative in $files){
        $bytes=[IO.File]::ReadAllBytes((Join-Path $root $relative))
        $sanitized=$relative -eq 'Config\DefaultEngine.ini'
        if($sanitized){
            $text=[Text.Encoding]::UTF8.GetString($bytes)
            $text=[regex]::Replace($text,'(?m)^SecurityToken=[^\r\n]*','SecurityToken=REDACTED_FOR_REVIEW')
            $bytes=[Text.Encoding]::UTF8.GetBytes($text)
        }
        $name=$relative.Replace('\','/')
        $entry=$archive.CreateEntry($name,[IO.Compression.CompressionLevel]::Optimal)
        $stream=$entry.Open()
        try{$stream.Write($bytes,0,$bytes.Length)}finally{$stream.Dispose()}
        $entries+=[pscustomobject]@{path=$name;bytes=$bytes.Length;sha256=[Convert]::ToHexString([Security.Cryptography.SHA256]::HashData($bytes));sanitized=$sanitized}
    }
    $manifest=[pscustomobject]@{
        created=(Get-Date).ToString('o');purpose='Independent read-only source/evidence review; not a runnable game'
        archive=$OutputName;root=$root
        exclusions=@('Executable packages','Binaries/caches','Private Saved/preferences/worlds','Git directory','node_modules','Full binary asset mirror')
        sanitization='AndroidFileServer SecurityToken redacted in config copy only; actual project unchanged'
        entries=@($entries)
    }
    $json=$manifest | ConvertTo-Json -Depth 6
    $json | Set-Content -LiteralPath $manifestPath -Encoding utf8
    $entry=$archive.CreateEntry("Docs/$ManifestName")
    $writer=[IO.StreamWriter]::new($entry.Open(),[Text.UTF8Encoding]::new($false))
    try{$writer.Write($json)}finally{$writer.Dispose()}
}finally{$archive.Dispose()}
$readback=[IO.Compression.ZipFile]::OpenRead($output)
try{
    foreach($expected in $entries){
        $stream=$readback.GetEntry($expected.path).Open()
        try{$hash=[Convert]::ToHexString([Security.Cryptography.SHA256]::HashData($stream))}finally{$stream.Dispose()}
        if($hash -ne $expected.sha256){throw "Review archive readback mismatch: $($expected.path)"}
    }
}finally{$readback.Dispose()}
Get-Item -LiteralPath $output | Select-Object FullName,Length
Get-FileHash -LiteralPath $output -Algorithm SHA256
Write-Output "Verified $($entries.Count) review entries plus their manifest."
