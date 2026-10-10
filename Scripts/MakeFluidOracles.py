"""Write Source/Unnamed/Tests/VoxelFluidOracles.json, the fluid tests' oracle data, from scout M's black-box probes
(water and sky track; facts in Docs/Records/2026-10-10-WaterSky-Scout-M-Fluids.md).

Inputs, in scout M's probes folder: oracles.json (make_oracles.py's output: per scenario the settled cells with the
checkpoint of their last change, the removed cells, and five full series) and fluid_probe.py (the scenario
definitions: setup, fluids placed at t0, actions, box, and the checkpoint ticks). The output keeps every scenario,
strips the "minecraft:" prefixes, and stores the series as changes between checkpoints. It records both inputs'
SHA-256.

usage: uv run --no-project --python 3.13 python -I Scripts/MakeFluidOracles.py [<probes folder>]
"""
import hashlib
import importlib.util
import json
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
OUT = ROOT / "Source" / "Unnamed" / "Tests" / "VoxelFluidOracles.json"
PROBES = Path(sys.argv[1]) if len(sys.argv) > 1 else Path(r"C:\dev\sandbox\scout-water\probes")
SERIES = ("w_plane", "w_slab", "w_drain", "l_plane0", "l_drain0")


def short(name):
    return name.removeprefix("minecraft:")


def op(o):
    return [o[0], *o[1:-1], short(o[-1])]


def sha256(path):
    return hashlib.sha256(path.read_bytes()).hexdigest().upper()


def main():
    spec = importlib.util.spec_from_file_location("fluid_probe", PROBES / "fluid_probe.py")
    probe = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(probe)
    oracles = json.loads((PROBES / "oracles.json").read_text())
    defined = {s["name"]: s for s in probe.scenarios()} | {s["name"]: s for s in probe.scenarios2()}
    if set(defined) != set(oracles):
        sys.exit(f"scenario lists differ: {sorted(set(defined) ^ set(oracles))}")

    # Each run's checkpoints, as fluid_probe.py's run() takes them: CHECKPOINTS plus every action tick, up to the end.
    runs = {}
    for name, o in oracles.items():
        run = runs.setdefault(o["run"], {"end": 0, "actions": set()})
        run["end"] = max(run["end"], o["observed_to"])
        run["actions"].update(int(t) for t in defined[name]["actions"])
    checkpoints = {run: sorted(p for p in set(probe.CHECKPOINTS) | r["actions"] if p <= r["end"])
                   for run, r in runs.items()}

    lines = []
    for name, o in oracles.items():
        s = defined[name]
        if [list(r) for r in s["box"]] != o["box"] or {str(t): [list(x) for x in v] for t, v in s["actions"].items()} != o["actions"]:
            sys.exit(f"{name}: the definition and the oracle disagree")
        out = {"name": name, "run": o["run"], "note": o["note"], "box": o["box"],
               "setup": [op(x) for x in s["setup"]], "fluids": [op(x) for x in s["fluids"]],
               "actions": [[int(t), [op(x) for x in v]] for t, v in sorted(s["actions"].items())],
               "observed_to": o["observed_to"], "settled_by": o["settled_by"],
               "cells": [[x, y, z, short(b), t] for x, y, z, b, t in o["cells"]], "removed": o["removed"]}
        if name in SERIES:
            # Changes between consecutive checkpoints; "" = the cell is air or stone again.
            series, before = [], {}
            for t in sorted(int(k) for k in o["series"]):
                now = {(x, y, z): short(b) for x, y, z, b in o["series"][str(t)]}
                changes = [[*k, now.get(k, "")] for k in sorted(set(before) | set(now)) if before.get(k) != now.get(k)]
                if changes:
                    series.append([t, changes])
                before = now
            out["series"] = series
        lines.append(json.dumps(out, separators=(",", ":")))

    head = {"about": "Scout M's fluid oracles (26.3 server, black-box). Made by Scripts/MakeFluidOracles.py; do not edit.",
            "inputs": {"oracles.json": sha256(PROBES / "oracles.json"), "fluid_probe.py": sha256(PROBES / "fluid_probe.py")},
            "floor_top": probe.FLOOR_Y, "checkpoints": checkpoints}
    text = "{\n" + ",\n".join(f"{json.dumps(k)}:{json.dumps(v, separators=(',', ':'))}" for k, v in head.items())
    text += ',\n"scenarios":[\n' + ",\n".join(lines) + "\n]}\n"
    OUT.write_bytes(text.encode("utf-8"))
    print(f"{OUT.relative_to(ROOT)}: {len(lines)} scenarios, {len(text):,} bytes")


if __name__ == "__main__":
    main()
