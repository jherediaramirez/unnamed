# Compiles and runs the ladder placement probe (LadderPlacementProbe.java) against the 26.3 server jar and writes
# ladder-placement-26.3.json next to this script. Scratch output (classes, level, run.log) goes to -WorkDir.
param(
    [string]$WorkDir = 'C:\dev\sandbox\b4b-blocks\probe\work',
    [string]$Jdk = 'C:\Users\jherediaramirez\.gradle\jdks\eclipse_adoptium-25-amd64-windows.2\bin',
    [string]$Datagen = 'C:\dev\sandbox\mc-26.3-blocks\datagen',
    [string]$Out = "$PSScriptRoot\ladder-placement-26.3.json"
)
$ErrorActionPreference = 'Stop'
$libs = Get-ChildItem -Recurse -Filter *.jar "$Datagen\libraries" | Sort-Object FullName | ForEach-Object FullName
$cp = (@("$Datagen\versions\26.3\server-26.3.jar") + $libs) -join ';'
New-Item -ItemType Directory -Force "$WorkDir\classes" | Out-Null
& "$Jdk\javac.exe" -cp $cp -d "$WorkDir\classes" "$PSScriptRoot\LadderPlacementProbe.java"
if ($LASTEXITCODE -ne 0) { throw 'javac failed' }
Push-Location $WorkDir
try {
    & "$Jdk\java.exe" -cp "$WorkDir\classes;$cp" LadderPlacementProbe "$WorkDir\work" $Out *> "$WorkDir\run.log"
    if ($LASTEXITCODE -ne 0) { throw "probe failed ($LASTEXITCODE), see $WorkDir\run.log" }
} finally { Pop-Location }
"wrote $Out"
