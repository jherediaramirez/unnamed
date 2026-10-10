"""Measure vanilla ore features black-box: a headless 26.3 server places each one many times with /place feature in a
layered superflat world, then the saved region files are read back into one statistics JSON per ore.

usage: uv run --no-project --python 3.13 python -I Reference/minecraft-worldgen-26.3/tools/feature_probe.py
       {run|read|all} [--server-dir D] [--staging D] [--port N] [--grid N] [--summary F] [ore ...]
`run` starts the server in --server-dir (eula, server.properties and placements.json are written there), places each ore
`--grid`^2 times per band (deepslate, tuff, stone, air) in 32x32 cells, then saves and stops. `read` decodes the region
files and writes <ore>.json into --staging; a control cell per ore (gold blocks filled by command) must read back as
exactly 27 blocks per band, with no other change. Contamination counts go to run_summary.json, not the ore files.
"""
import argparse
import json
import math
import queue
import re
import struct
import subprocess
import sys
import threading
import time
import zlib
from collections import Counter, defaultdict
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
JAVA = Path(r"C:\Users\jherediaramirez\.gradle\jdks\eclipse_adoptium-25-amd64-windows.2\bin\java.exe")
DATAGEN = Path(r"C:\dev\sandbox\mc-26.3-blocks\datagen")
SERVER_JAR = DATAGEN / "versions" / "26.3" / "server-26.3.jar"
SANDBOX = Path(r"C:\dev\sandbox\s1-feature-probe")
ORES = ("ore_andesite ore_clay ore_coal ore_coal_buried ore_copper_large ore_copper_small ore_diamond_buried "
        "ore_diamond_large ore_diamond_medium ore_diamond_small ore_diorite ore_dirt ore_emerald ore_gold "
        "ore_gold_buried ore_granite ore_gravel ore_infested ore_iron ore_iron_small ore_lapis ore_lapis_buried "
        "ore_redstone ore_tuff").split()
LAYERS = [("minecraft:bedrock", -64, -64), ("minecraft:deepslate", -63, -17), ("minecraft:tuff", -16, 15),
          ("minecraft:stone", 16, 83), ("minecraft:air", 84, 84), ("minecraft:stone", 85, 111)]
BANDS = {"deepslate": (-40, -63, -21), "tuff": (0, -20, 21), "stone": (44, 22, 63), "air": (84, 64, 111)}
WINDOW_EDGES = {w for _, lo, hi in BANDS.values() for w in (lo, hi)}
SPACING = 32
GRID_DEFAULT = 10
ORE_STRIDE = 512
ORE_X_START = 1024
CONTROL_BLOCK = "minecraft:gold_block"
BATCH = 100
LOG_RE = re.compile(r"^\[[\d:.]+\] \[[^\]]*/(\w+)\]: (.*)$")
NOISE_RE = re.compile(r"^(Server empty|Preparing|Saving|Saved|ThreadedAnvil|Time elapsed|Starting|Loading|Done|Stopping|Found new)")
PERCENTILES = (5, 25, 50, 75, 95)


def fail(msg):
    sys.exit(f"PROBE FAIL: {msg}")


def layer_state(y):
    for name, lo, hi in LAYERS:
        if lo <= y <= hi:
            return name
    return "minecraft:air"


def ore_origin(i):
    return ORE_X_START + ORE_STRIDE * i


def cell_centre(i, gx, gz):
    return ore_origin(i) + SPACING * gx + SPACING // 2, SPACING * gz + SPACING // 2


def area_blocks(i, grid):
    x0 = ore_origin(i)
    return x0 - 16, -16, x0 + SPACING * (grid + 1) + 15, SPACING * grid + 15


def band_of(y):
    for band, (_, lo, hi) in BANDS.items():
        if lo <= y <= hi:
            return band
    return None


# ---------------------------------------------------------------- server

def write_server_files(server_dir, port):
    server_dir.mkdir(parents=True, exist_ok=True)
    (server_dir / "eula.txt").write_text("eula=true\n")
    gen = {"biome": "minecraft:plains", "features": False, "lakes": False, "structure_overrides": [],
           "layers": [{"block": n, "height": hi - lo + 1} for n, lo, hi in LAYERS]}
    props = {"server-ip": "127.0.0.1", "server-port": port, "online-mode": "false", "enable-rcon": "false",
             "enable-query": "false", "max-tick-time": -1, "spawn-protection": 0, "view-distance": 2,
             "simulation-distance": 2, "region-file-compression": "deflate", "generate-structures": "false",
             "level-seed": 263, "level-name": "world", "level-type": "minecraft\\:flat",
             "generator-settings": json.dumps(gen, separators=(",", ":")), "motd": "feature probe", "pause-when-empty-seconds": -1}
    (server_dir / "server.properties").write_text("".join(f"{k}={v}\n" for k, v in props.items()))


class Server:
    def __init__(self, server_dir):
        libs = sorted(str(p) for p in (DATAGEN / "libraries").rglob("*.jar"))
        cp = ";".join([str(SERVER_JAR)] + libs)
        self.proc = subprocess.Popen([str(JAVA), "-Xms1G", "-Xmx3G", "-cp", cp, "net.minecraft.server.Main", "--nogui"],
                                     cwd=server_dir, stdin=subprocess.PIPE, stdout=subprocess.PIPE,
                                     stderr=subprocess.STDOUT)
        self.lines = queue.Queue()
        self.log = open(server_dir / "probe-server.log", "w", encoding="utf-8")
        self.pump = threading.Thread(target=self._pump, daemon=True)
        self.pump.start()

    def _pump(self):
        for raw in self.proc.stdout:
            line = raw.decode("utf-8", "replace").rstrip()
            self.log.write(line + "\n")
            self.log.flush()
            self.lines.put(line)
        self.lines.put(None)

    def wait_for(self, pred, timeout):
        """Return the INFO messages up to and including the first one satisfying pred."""
        seen, end = [], time.time() + timeout
        while time.time() < end:
            try:
                line = self.lines.get(timeout=1)
            except queue.Empty:
                continue
            if line is None:
                fail("server exited early; see probe-server.log")
            m = LOG_RE.match(line)
            if not m or m.group(1) != "INFO":
                continue
            seen.append(m.group(2))
            if pred(m.group(2)):
                return seen
        fail(f"timeout waiting for the server; last messages {seen[-5:]}")

    def send(self, lines):
        self.proc.stdin.write(("\n".join(lines) + "\n").encode())
        self.proc.stdin.flush()

    def ask(self, cmds, timeout=600):
        """Send commands in order and return one response per command."""
        tag = f"PROBE_{time.time_ns()}"
        self.send(cmds + [f"say {tag}"])
        seen = self.wait_for(lambda m: m.endswith(tag), timeout)[:-1]
        out = [m for m in seen if not NOISE_RE.match(m)]
        if len(out) != len(cmds):
            fail(f"{len(cmds)} commands got {len(out)} responses: {cmds[:2]} ... {out[:3]}")
        return out

    def stop(self, timeout=120):
        try:
            self.send(["stop"])
            self.proc.wait(timeout)
        except Exception:
            self.proc.kill()
        # The pump drains the last lines into the log before it closes.
        self.pump.join(10)
        self.log.close()


def classify(kind, text):
    text = text.removeprefix("System chat: ")
    if "not loaded" in text:
        return "retry"
    if kind == "place":
        if text.startswith("Placed"):
            return "placed"
        if text.startswith("Failed"):
            return "failed"
    elif kind == "fill":
        if "27" in text and "ill" in text:
            return "filled"
    elif not re.search(r"fail|cannot|invalid|unknown|incorrect|maximum", text, re.I):
        return "ok"
    fail(f"unexpected response to a {kind} command: {text}")


def run_commands(server, items, stats):
    """items: dicts with cmd and kind; fills in result and text. Retries commands whose chunk is not loaded yet."""
    pending = list(items)
    for attempt in range(60):
        retry = []
        for k in range(0, len(pending), BATCH):
            part = pending[k:k + BATCH]
            for it, text in zip(part, server.ask([it["cmd"] for it in part])):
                it["text"], it["result"] = text, classify(it["kind"], text)
                if it["result"] == "retry":
                    retry.append(it)
        stats["retries"] += len(retry)
        if not retry:
            return
        pending = retry
        time.sleep(2)
    fail(f"{len(pending)} commands still not loaded after retries, e.g. {pending[0]['cmd']}")


def forceload_cmds(verb, i, grid):
    x1, z1, x2, z2 = area_blocks(i, grid)
    width = (x2 - x1 + 1) // 16
    rows = max(1, 256 // width)
    cmds = []
    for z in range(z1, z2 + 1, 16 * rows):
        zz = min(z + 16 * rows - 1, z2)
        cmds.append({"cmd": f"forceload {verb} {x1} {z} {x2} {zz}", "kind": "forceload"})
    return cmds


def run(args, ores):
    server_dir, grid = args.server_dir, args.grid
    write_server_files(server_dir, args.port)
    for stale in ("world", "placements.json"):
        if (server_dir / stale).exists():
            fail(f"{server_dir / stale} exists; use a fresh server folder")
    summary = {"grid": grid, "ores": {}}
    t0 = time.time()
    server = Server(server_dir)
    try:
        server.wait_for(lambda m: m.startswith("Done ("), 300)
        summary["timings_s"] = {"server_start": round(time.time() - t0, 1)}
        server.ask(["tick freeze"])
        placements = []
        for i, ore in enumerate(ORES):
            if ore not in ores:
                continue
            t1, stats = time.time(), {"retries": 0}
            run_commands(server, forceload_cmds("add", i, grid), stats)
            fills = []
            for band, (y, _, _) in BANDS.items():
                cx, cz = cell_centre(i, grid, 0)
                fills.append({"cmd": f"fill {cx - 1} {y - 1} {cz - 1} {cx + 1} {y + 1} {cz + 1} {CONTROL_BLOCK}",
                              "kind": "fill"})
            run_commands(server, fills, stats)
            items = []
            for gz in range(grid):
                for gx in range(grid):
                    cx, cz = cell_centre(i, gx, gz)
                    for band, (y, _, _) in BANDS.items():
                        items.append({"cmd": f"place feature minecraft:{ore} {cx} {y} {cz}", "kind": "place",
                                      "ore": ore, "gx": gx, "gz": gz, "band": band, "x": cx, "y": y, "z": cz})
            run_commands(server, items, stats)
            run_commands(server, forceload_cmds("remove", i, grid), stats)
            counts = Counter(it["result"] for it in items)
            summary["ores"][ore] = {"responses": dict(counts), "retries": stats["retries"],
                                    "place_s": round(time.time() - t1, 1)}
            placements += [{k: it[k] for k in ("ore", "gx", "gz", "band", "x", "y", "z", "result", "text")}
                           for it in items]
            (server_dir / "placements.json").write_text(json.dumps({"grid": grid, "placements": placements}, indent=1))
            print(f"{ore}: {dict(counts)} retries {stats['retries']} {time.time() - t1:.0f}s", flush=True)
        t2 = time.time()
        server.send(["save-all flush"])
        server.wait_for(lambda m: "Saved the game" in m, 600)
        summary["timings_s"]["save"] = round(time.time() - t2, 1)
    finally:
        server.stop()
    summary["timings_s"]["run_total"] = round(time.time() - t0, 1)
    update_summary(args.summary, {"run": summary})


def update_summary(path, part):
    data = json.loads(path.read_text()) if path.exists() else {}
    data.update(part)
    path.write_text(json.dumps(data, indent=1) + "\n")


# ---------------------------------------------------------------- reading

def parse_nbt(buf):
    pos = 0
    sizes = {1: 1, 2: 2, 3: 4, 4: 8, 5: 4, 6: 8}
    fmts = {1: ">b", 2: ">h", 3: ">i", 4: ">q", 5: ">f", 6: ">d"}

    def payload(t):
        nonlocal pos
        if t in fmts:
            v = struct.unpack_from(fmts[t], buf, pos)[0]
            pos += sizes[t]
            return v
        if t == 8:
            n = struct.unpack_from(">H", buf, pos)[0]
            pos += 2 + n
            return buf[pos - n:pos].decode("utf-8", "replace")
        if t in (7, 11, 12):
            n = struct.unpack_from(">i", buf, pos)[0]
            size = n * {7: 1, 11: 4, 12: 8}[t]
            pos += 4 + size
            return bytes(buf[pos - size:pos])
        if t == 9:
            et, n = struct.unpack_from(">bi", buf, pos)
            pos += 5
            return [payload(et) for _ in range(n)]
        out = {}
        while True:
            et = buf[pos]
            pos += 1
            if et == 0:
                return out
            n = struct.unpack_from(">H", buf, pos)[0]
            pos += 2
            name = buf[pos:pos + n].decode("utf-8", "replace")
            pos += n
            out[name] = payload(et)

    assert buf[0] == 10
    pos = 3 + struct.unpack_from(">H", buf, 1)[0]
    return payload(10)


def state_str(p):
    # 26.3 writes a default state as its id alone, and any other as {"id", "properties"} (older worlds: "Name",
    # "Properties"). A palette mixing the two wraps the bare ids as {"": id}.
    if isinstance(p, str):
        return p
    if "" in p:
        return p[""]
    name, props = p.get("id", p.get("Name")), p.get("properties", p.get("Properties"))
    if not props:
        return name
    return name + "[" + ",".join(f"{k}={props[k]}" for k in sorted(props)) + "]"


def section_changes(sec, chunk_x, chunk_z):
    """Yield (x, y, z, state) for every block of a section that differs from the layer table."""
    bs = sec["block_states"]
    names = [state_str(p) for p in bs["palette"]]
    sy = sec["Y"]
    expected = [layer_state(sy * 16 + r) for r in range(16)]
    if set(names) <= set(expected):
        return
    data = bs.get("data")
    if data is None:
        idx = [0] * 4096
    else:
        bits = max(4, (len(names) - 1).bit_length())
        per, mask = 64 // bits, (1 << bits) - 1
        longs = struct.unpack(f">{len(data) // 8}Q", data)
        shifts = [k * bits for k in range(per)]
        idx = [(v >> s) & mask for v in longs for s in shifts][:4096]
    for r in range(16):
        e = expected[r]
        row = idx[r * 256:(r + 1) * 256]
        if e in names and row.count(names.index(e)) == 256:
            continue
        ei = names.index(e) if e in names else -1
        for j, v in enumerate(row):
            if v != ei:
                yield chunk_x * 16 + (j & 15), sy * 16 + r, chunk_z * 16 + (j >> 4), names[v]


class Regions:
    def __init__(self, folder):
        self.folder, self.files = folder, {}

    def chunk(self, cx, cz):
        key = (cx >> 5, cz >> 5)
        if key not in self.files:
            path = self.folder / f"r.{key[0]}.{key[1]}.mca"
            self.files[key] = path.read_bytes() if path.exists() else None
        raw = self.files[key]
        if raw is None:
            return None
        entry = struct.unpack_from(">I", raw, 4 * ((cx & 31) + (cz & 31) * 32))[0]
        if entry == 0:
            return None
        off = (entry >> 8) * 4096
        length, comp = struct.unpack_from(">iB", raw, off)
        if comp != 2:
            fail(f"chunk {cx},{cz} uses compression {comp}, expected zlib")
        return parse_nbt(zlib.decompress(raw[off + 5:off + 4 + length]))


def find_region_folder(server_dir):
    for sub in ("world/dimensions/minecraft/overworld/region", "world/region"):
        if (server_dir / sub).is_dir() and any((server_dir / sub).glob("r.*.mca")):
            return server_dir / sub
    fail(f"no overworld region folder under {server_dir / 'world'}")


def scan_ore(regions, i, grid):
    """All changes in the ore's forceloaded area as (x, y, z, state)."""
    x1, z1, x2, z2 = area_blocks(i, grid)
    changes = []
    for cz in range(z1 >> 4, (z2 >> 4) + 1):
        for cx in range(x1 >> 4, (x2 >> 4) + 1):
            chunk = regions.chunk(cx, cz)
            if chunk is None:
                fail(f"chunk {cx},{cz} missing from the region files")
            for sec in chunk["sections"]:
                if -4 <= sec["Y"] <= 6:
                    changes += section_changes(sec, cx, cz)
    return changes


def dist(values):
    values = sorted(values)
    n = len(values)
    mean = sum(values) / n
    std = math.sqrt(sum((v - mean) ** 2 for v in values) / n)

    def pct(p):
        pos = (n - 1) * p / 100
        lo = int(pos)
        hi = min(lo + 1, n - 1)
        return round(values[lo] + (values[hi] - values[lo]) * (pos - lo), 4)
    hist = Counter(values)
    return {"mean": round(mean, 4), "std": round(std, 4), "min": values[0], "max": values[-1],
            "percentiles": {str(p): pct(p) for p in PERCENTILES},
            "histogram": {str(k): hist[k] for k in sorted(hist)}}


def sorted_counts(counter):
    return {str(k): counter[k] for k in sorted(counter)}


def band_stats(cells, placed, failed):
    """cells: one list of (dx, dy, dz, state) per placement (empty for a failed one)."""
    states = sorted({c[3] for cell in cells for c in cell})
    out = {"placements": len(cells), "placed": placed, "failed": failed,
           "blocks": {"total": dist([len(c) for c in cells])}}
    for s in states:
        out["blocks"][s] = dist([sum(1 for c in cell if c[3] == s) for cell in cells])
    out["extent"] = {}
    for k, name in enumerate(("dx", "dy", "dz")):
        spans, lo, hi = Counter(), None, None
        for cell in cells:
            vals = [c[k] for c in cell]
            spans[max(vals) - min(vals) + 1 if vals else 0] += 1
            if vals:
                lo = min(vals) if lo is None else min(lo, min(vals))
                hi = max(vals) if hi is None else max(hi, max(vals))
        out["extent"][name] = {"min": lo, "max": hi, "span_histogram": {str(s): spans[s] for s in sorted(spans)}}
    for k, name in enumerate(("by_dx", "by_dy", "by_dz")):
        out[name] = sorted_counts(Counter(c[k] for cell in cells for c in cell))
    return out


def read_ore(regions, i, grid, ore, responses, summary):
    x0 = ore_origin(i)
    cells = defaultdict(list)
    contamination = Counter()
    byband_y = defaultdict(lambda: defaultdict(Counter))
    replaced = defaultdict(Counter)
    for x, y, z, state in scan_ore(regions, i, grid):
        gx, gz = (x - x0) // SPACING, z // SPACING
        in_cell = x >= x0 and z >= 0 and ((gx < grid and gz < grid) or (gx == grid and gz == 0))
        if not in_cell:
            contamination["outside_cells"] += 1
            continue
        dx, dz = x - (x0 + SPACING * gx + 16), z - (SPACING * gz + 16)
        band = band_of(y)
        if band is None:
            contamination["outside_windows"] += 1
            continue
        if dx in (-16, 15) or dz in (-16, 15):
            contamination["cell_edge"] += 1
        if y in WINDOW_EDGES:
            contamination["window_edge"] += 1
        cells[(gx, gz, band)].append((dx, y - BANDS[band][0], dz, state))
        if gx < grid:
            byband_y[band][y][state] += 1
            replaced[band][layer_state(y)] += 1
    controls = {}
    for band, (y, _, _) in BANDS.items():
        got = sorted(cells[(grid, 0, band)])
        want = sorted((dx, dy, dz, CONTROL_BLOCK) for dx in (-1, 0, 1) for dy in (-1, 0, 1) for dz in (-1, 0, 1))
        controls[band] = {"gold_blocks": sum(1 for c in got if c[3] == CONTROL_BLOCK), "changes": len(got),
                          "ok": got == want}
    if not all(c["ok"] for c in controls.values()):
        summary["controls"], summary["contamination"] = controls, dict(contamination)
        return None
    out_bands, adj, rest, plane = {}, Counter(), Counter(), Counter()
    for band in BANDS:
        mine = [r for r in responses if r["band"] == band]
        lists = [cells[(r["gx"], r["gz"], band)] for r in mine]
        st = band_stats(lists, sum(r["result"] == "placed" for r in mine), sum(r["result"] == "failed" for r in mine))
        st["replaced"] = dict(sorted(replaced[band].items()))
        st["by_y"] = {str(y): dict(sorted(byband_y[band][y].items())) for y in sorted(byband_y[band])}
        out_bands[band] = st
        if band in ("air", "stone"):
            for lst in lists:
                for _, dy, _, _ in lst:
                    key = "adjacent" if abs(dy) == 1 else "rest" if abs(dy) >= 2 else "plane"
                    (adj if key == "adjacent" else rest if key == "rest" else plane)[band] += 1
    a_ratio = adj["air"] / rest["air"] if rest["air"] else None
    s_ratio = adj["stone"] / rest["stone"] if rest["stone"] else None
    ratio = round(a_ratio / s_ratio, 4) if a_ratio is not None and s_ratio else None
    feature = json.loads((ROOT / "data/minecraft/worldgen/feature" / f"{ore}.json").read_text())
    summary["controls"], summary["contamination"] = controls, dict(contamination)
    return {"generated_by": "tools/feature_probe.py", "minecraft": "26.3", "feature": f"minecraft:{ore}",
            "config": {"size": feature.get("size"),
                       "discard_chance_on_air_exposure": feature.get("discard_chance_on_air_exposure")},
            "world": {"layers": [list(layer) for layer in LAYERS], "grid_spacing": SPACING,
                      "bands": {b: {"origin_y": o, "window": [lo, hi]} for b, (o, lo, hi) in BANDS.items()}},
            "note": "vanilla's random differs per call: compare distributions, not blocks",
            "bands": out_bands,
            "exposure": {"air": {"adjacent": adj["air"], "rest": rest["air"], "plane": plane["air"]},
                         "solid": {"adjacent": adj["stone"], "rest": rest["stone"]}, "ratio": ratio}}


def read(args, ores):
    meta = json.loads((args.server_dir / "placements.json").read_text())
    grid = meta["grid"]
    regions = Regions(find_region_folder(args.server_dir))
    args.staging.mkdir(parents=True, exist_ok=True)
    reading, bad = {}, []
    for i, ore in enumerate(ORES):
        mine = [p for p in meta["placements"] if p["ore"] == ore]
        if ore not in ores or not mine:
            continue
        t0, summary = time.time(), {}
        result = read_ore(regions, i, grid, ore, mine, summary)
        summary["read_s"] = round(time.time() - t0, 1)
        reading[ore] = summary
        if result is None:
            bad.append(ore)
        else:
            (args.staging / f"{ore}.json").write_text(json.dumps(result, indent=1) + "\n")
        print(f"{ore}: controls ok={result is not None} contamination={summary['contamination']} "
              f"{summary['read_s']}s", flush=True)
    update_summary(args.summary, {"read": reading})
    if bad:
        fail(f"control cells did not read back as 27 gold blocks per band: {', '.join(bad)}")


def main():
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("step", choices=("run", "read", "all"))
    ap.add_argument("ores", nargs="*")
    ap.add_argument("--server-dir", type=Path, default=SANDBOX / "server")
    ap.add_argument("--staging", type=Path, default=SANDBOX / "staging")
    ap.add_argument("--port", type=int, default=25771)
    ap.add_argument("--grid", type=int, default=GRID_DEFAULT)
    ap.add_argument("--summary", type=Path)
    args = ap.parse_args()
    args.summary = args.summary or args.server_dir.parent / "run_summary.json"
    for path in (args.server_dir, args.staging, args.summary):
        if path.resolve().is_relative_to(ROOT):
            fail(f"{path} is inside the reference; probe facts enter it only through build_reference.py --probes")
    ores =[o if o in ORES else f"ore_{o}" for o in args.ores] or ORES
    for o in ores:
        if o not in ORES:
            fail(f"unknown ore {o}")
    if args.step in ("run", "all"):
        run(args, ores)
    if args.step in ("read", "all"):
        read(args, ores)


if __name__ == "__main__":
    main()
