#!/usr/bin/env bash
# usage: strip_sheet.sh <out.png> <title>
cd "C:/dev/output/unnamed-player-blender-v001-20261015/previews/_tmp/pick"
files=""; labels=""
for k in A B C; do for f in 00 03 06 09 12 15; do files="$files strip_${k}_$f.png"; done; labels="$labels|$k start (link)|$k anticipation|$k approach|$k CONTACT f9|$k rebound|$k recovery"; done
uv run --no-project --with pillow python ../../../scripts/tools/sheet.py "$1" $files --cols 6 --width 1680 --labels "${labels:1}" --title "$2"
