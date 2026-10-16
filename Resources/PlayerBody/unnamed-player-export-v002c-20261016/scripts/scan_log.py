"""Attribute Interchange errors/ensures in an UnrealEditor-Cmd log to the source file being imported.
    uv run --no-project --python 3.13 python -I scan_log.py <log> <out.json>"""
import json
import re
import sys

cur = None
hits = {}
pat = re.compile(r"Ensure condition failed|LogInterchange\w*: Error|bake range|Baked Transforms|AnimationPayloadFile")
for line in open(sys.argv[1], encoding="utf-8", errors="replace"):
    if "LogInit: Display:" in line:      # end-of-run replay of earlier errors
        continue
    m = re.search(r"Interchange start importing source \[(.+?)\]", line)
    if m:
        cur = m.group(1).replace("\\", "/").split("/")[-1]
        hits.setdefault(cur, [])
        continue
    if pat.search(line):
        hits.setdefault(cur or "<none>", []).append(line.strip()[30:260])
out = {"log": sys.argv[1], "files_imported": len(hits), "files_with_issues": {k: v for k, v in hits.items() if v}}
json.dump(out, open(sys.argv[2], "w"), indent=1)
print(json.dumps({"files_imported": out["files_imported"],
                  "issues": {k: len(v) for k, v in out["files_with_issues"].items()}}))
