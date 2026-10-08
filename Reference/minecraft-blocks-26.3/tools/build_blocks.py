import csv
import json
import sys
import zipfile
from collections import defaultdict
from pathlib import Path

REPORTS = Path(r"C:\dev\sandbox\mc-26.3-blocks\reports\reports")
JAR = Path(r"C:\Users\jherediaramirez\AppData\Roaming\ModrinthApp\meta\versions\26.3\26.3.jar")
OUT = Path(r"C:\dev\sandbox\mc-26.3-blocks\out")
REG_CSV = OUT / "blocks_registry.csv"
TAG_CSV = OUT / "block_tags.csv"

reg = json.loads((REPORTS / "registries.json").read_text(encoding="utf-8"))
bl = json.loads((REPORTS / "blocks.json").read_text(encoding="utf-8"))
block_reg = reg["minecraft:block"]["entries"]  # keeps file order
problems = []

# ---------- registry CSV ----------
rows = []
for key, val in block_reg.items():
    proto = val["protocol_id"]
    if not key.startswith("minecraft:"):
        problems.append(f"non-minecraft namespace: {key}")
    name = key.split(":", 1)[1]
    b = bl.get(key)
    if b is None:
        problems.append(f"missing in blocks.json: {key}")
        continue
    states = b["states"]
    ids = [s["id"] for s in states]
    defaults = [s["id"] for s in states if s.get("default")]
    if len(defaults) != 1:
        problems.append(f"{key}: {len(defaults)} default states")
    default_id = defaults[0] if defaults else ""
    mn, mx = min(ids), max(ids)
    if mx - mn + 1 != len(ids) or len(set(ids)) != len(ids):
        problems.append(f"{key}: state ids not contiguous/unique (min {mn} max {mx} count {len(ids)})")
    props = b.get("properties", {})
    prop_str = ";".join(f"{pn}=" + "|".join(vals) for pn, vals in props.items())
    for pn, vals in props.items():
        for v in vals:
            if "|" in v or ";" in v or "=" in v:
                problems.append(f"{key}: special char in property value {pn}={v}")
    rows.append([proto, name, default_id, mn, mx, len(ids), prop_str])

with REG_CSV.open("w", encoding="utf-8", newline="") as f:
    w = csv.writer(f, lineterminator="\n")
    w.writerow(["protocol_id", "id", "default_state_id", "min_state_id", "max_state_id", "state_count", "properties"])
    w.writerows(rows)

total_states = sum(r[5] for r in rows)
protos = [r[0] for r in rows]
print(f"registry blocks: {len(rows)}  total states: {total_states}")
print(f"protocol_id range: {min(protos)}..{max(protos)}, unique: {len(set(protos)) == len(protos)}")
ext = [k for k in bl if k not in block_reg]
print(f"blocks.json entries not in registry: {len(ext)} {ext[:5]}")

# ---------- block tags ----------
z = zipfile.ZipFile(JAR)
PREFIX = "data/minecraft/tags/block/"
tag_values = {}
for n in z.namelist():
    if n.startswith(PREFIX) and n.endswith(".json"):
        tname = n[len(PREFIX):-5]
        obj = json.loads(z.read(n).decode("utf-8"))
        tag_values[tname] = obj.get("values", [])
namespaces = sorted({n.split("/")[1] for n in z.namelist() if n.startswith("data/") and n.count("/") >= 2 and n.split("/")[2] == "tags"})
print("tag namespaces in jar:", namespaces)
print("block tag files:", len(tag_values))

block_ids = {r[1] for r in rows}
unknown_members = defaultdict(set)
missing_tags = set()


def tag_members(tname, stack):
    """Return set of block ids (short) reachable from tag tname (short name)."""
    if tname in stack:
        problems.append(f"cycle through #{tname}")
        return set()
    if tname not in tag_values:
        missing_tags.add(tname)
        return set()
    stack.add(tname)
    out = set()
    for v in tag_values[tname]:
        required = True
        if isinstance(v, dict):
            required = v.get("required", True)
            v = v.get("id")
        if not isinstance(v, str):
            continue
        if v.startswith("#"):
            ref = v[1:]
            if ref.startswith("minecraft:"):
                ref = ref[len("minecraft:"):]
            out |= tag_members(ref, stack)
        else:
            nm = v[len("minecraft:"):] if v.startswith("minecraft:") else v
            if nm in block_ids:
                out.add(nm)
            elif required:
                unknown_members[tname].add(nm)
    stack.discard(tname)
    return out


tags_of = defaultdict(set)
tag_sizes = {}
for tname in sorted(tag_values):
    members = tag_members(tname, set())
    tag_sizes[tname] = len(members)
    for b in members:
        tags_of[b].add(tname)

if missing_tags:
    problems.append(f"referenced tags with no file: {sorted(missing_tags)}")
for t, nm in unknown_members.items():
    problems.append(f"tag {t} lists non-registry blocks: {sorted(nm)}")

# Output: only blocks with at least one tag, in registry order
tag_rows = []
for r in rows:
    name = r[1]
    if tags_of.get(name):
        tag_rows.append([name, ";".join(sorted(tags_of[name]))])

with TAG_CSV.open("w", encoding="utf-8", newline="") as f:
    w = csv.writer(f, lineterminator="\n")
    w.writerow(["id", "tags"])
    w.writerows(tag_rows)

print(f"blocks with >=1 tag: {len(tag_rows)} of {len(rows)}")
print(f"distinct tags used: {len({t for v in tags_of.values() for t in v})} (tag files: {len(tag_values)})")
print("problems:", len(problems))
for p in problems[:40]:
    print("  -", p)
print("sample:")
for r in tag_rows[:3]:
    print("  ", r)
