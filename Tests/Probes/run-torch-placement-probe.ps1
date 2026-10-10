# Compiles and runs the torch placement probe (TorchPlacementProbe.java) against the 26.3 server jar, then checks the
# rule with check_rule.py. Everything it writes goes to -WorkDir; compare its output with torch-placement-26.3.json
# here (the accepted matrix) rather than overwriting it.
param(
    [string]$WorkDir = 'C:\dev\sandbox\t3-torch-probe-run',
    [string]$Jdk = 'C:\Users\jherediaramirez\.gradle\jdks\eclipse_adoptium-25-amd64-windows.2\bin',
    [string]$Datagen = 'C:\dev\sandbox\mc-26.3-blocks\datagen'
)
$ErrorActionPreference = 'Stop'
$libs = Get-ChildItem -Recurse -Filter *.jar "$Datagen\libraries" | Sort-Object FullName | ForEach-Object FullName
$cp = (@("$Datagen\versions\26.3\server-26.3.jar") + $libs) -join ';'
New-Item -ItemType Directory -Force "$WorkDir\classes" | Out-Null
$out = "$WorkDir\torch-placement-26.3.json"
& "$Jdk\javac.exe" -cp $cp -d "$WorkDir\classes" "$PSScriptRoot\TorchPlacementProbe.java"
if ($LASTEXITCODE -ne 0) { throw 'javac failed' }
Push-Location $WorkDir
try {
    & "$Jdk\java.exe" -cp "$WorkDir\classes;$cp" TorchPlacementProbe "$WorkDir\work" $out *> "$WorkDir\run.log"
    if ($LASTEXITCODE -ne 0) { throw "probe failed ($LASTEXITCODE), see $WorkDir\run.log" }
} finally { Pop-Location }
uv run --no-project --python 3.13 python -I "$PSScriptRoot\check_rule.py" $out
if ($LASTEXITCODE -ne 0) { throw 'check_rule.py failed' }
$new = (Get-FileHash -Algorithm SHA256 $out).Hash
$accepted = (Get-FileHash -Algorithm SHA256 "$PSScriptRoot\torch-placement-26.3.json").Hash
"$new $out"
if ($new -ne $accepted) { throw "differs from the accepted matrix ($accepted)" }
'identical to the accepted matrix'
