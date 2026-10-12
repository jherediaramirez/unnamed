# Crash dumps under an editor's temporary profile: with -UserDir, a crash writes its dump and log to <UserDir>\Saved\Crashes\UECC-*.
# Every runner that deletes such a profile closes it through here (RunChecks.ps1's native step and radius-8 hashes, TestVoxel.ps1):
#   . (Join-Path $PSScriptRoot 'CrashFolders.ps1')
#   $closed=Close-Profile -UserDir $userDir -Destination $dir   # .Crashes: crash folders kept in $dir\Crashes; .Kept: the profile, if kept
# The profile is deleted only once every crash file is in <Destination>\Crashes (same relative paths and sizes). When the copy fails
# (a locked file, a full disk) the profile stays, with a warning, and Kept names it for the CRASH or throw line. A failed copy never
# throws: a green step stays green.
function Close-Profile{
    param([Parameter(Mandatory)][string]$UserDir,[Parameter(Mandatory)][string]$Destination)
    $crashes=Join-Path $UserDir 'Saved\Crashes'
    $folders=@(Get-ChildItem -LiteralPath $crashes -Directory -ErrorAction SilentlyContinue)
    if($folders.Count){
        $target=Join-Path $Destination 'Crashes'
        try{
            if(-not(Test-Path -LiteralPath $target -PathType Container)){New-Item -ItemType Directory -Path $target -ErrorAction Stop | Out-Null}
            foreach($folder in $folders){Copy-Item -LiteralPath $folder.FullName -Destination $target -Recurse -Force -ErrorAction Stop}
            $source=(Get-Item -LiteralPath $crashes).FullName
            foreach($file in @(Get-ChildItem -LiteralPath $crashes -File -Recurse -ErrorAction Stop | Where-Object { $_.DirectoryName -ne $source })){
                $copy=Get-Item -LiteralPath (Join-Path $target $file.FullName.Substring($source.Length+1)) -ErrorAction SilentlyContinue
                if(-not $copy -or $copy.Length -ne $file.Length){throw "$($file.FullName) did not arrive whole"}
            }
        }catch{
            Write-Warning "Close-Profile: could not keep $crashes in $target ($($_.Exception.Message)); the profile stays at $UserDir"
            return [pscustomobject]@{Crashes=0; Kept=$UserDir}
        }
    }
    Remove-Item -LiteralPath $UserDir -Recurse -Force -ErrorAction SilentlyContinue
    [pscustomobject]@{Crashes=$folders.Count; Kept=$null}
}
