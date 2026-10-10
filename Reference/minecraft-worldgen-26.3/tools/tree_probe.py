"""Measure vanilla tree and big-vegetation features black-box: a headless 26.3 server places each one many times with
/place feature in a superflat world (stone, 3 dirt, grass at Y 59), then the saved region files are read back into one
statistics JSON per probe.

usage: uv run --no-project --python 3.13 python -I Reference/minecraft-worldgen-26.3/tools/tree_probe.py
       {run|read|all|list} [--server-dir D] [--staging D] [--port N] [--summary F] [probe ...]
`list` prints the probes: every minecraft:tree feature in data/, mangrove and tall mangrove on mud under water
(<feature>@mudwater), rooted azalea in an air pocket, the fallen trees, huge mushrooms, bamboo and forest rock, and
oak, fancy oak, dark oak, spruce and mega spruce under a stone ceiling N blocks above the origin (<feature>_ceilN).
`run` starts the server in --server-dir (eula, server.properties and placements.json are written there), turns random
ticks off, places each probe grid^2 times in 32x32 cells (10x10, ceilings 4x4), lets the leaves' scheduled updates run
for 3 s, then saves and stops. `read` decodes the region files and writes tree_<probe>.json into --staging: per
placement what it wrote relative to the origin, the distributions over the placed ones, and `profile`, the per-layer
shape: log columns and logs per dy, leaves (and mushroom cap blocks) per layer counted from the placement's highest log
(from the origin when it has none) as mean per tree, largest Chebyshev radius and occupancy per (dx,dz) as a fraction
of the placed trees, and every other block's dy and radius histograms. Changes outside the cells and the timings go to
run_summary.json, not the probe files.
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
from collections import Counter, defaultdict, deque
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
JAVA = Path(r"C:\Users\jherediaramirez\.gradle\jdks\eclipse_adoptium-25-amd64-windows.2\bin\java.exe")
DATAGEN = Path(r"C:\dev\sandbox\mc-26.3-blocks\datagen")
SERVER_JAR = DATAGEN / "versions" / "26.3" / "server-26.3.jar"
SANDBOX = Path(r"C:\dev\sandbox\tree-probe")
LAYERS = [("minecraft:bedrock", -64, -64), ("minecraft:stone", -63, 55), ("minecraft:dirt", 56, 58),
          ("minecraft:grass_block", 59, 59)]
Y0 = 60
SPACING = 32
X_START = 1024
STRIDE = 512
SEC_LO, SEC_HI = 2, 8  # sections read: Y 32 to 143
EXAMPLES = 2
SETTLE_S = 3
BATCH = 100
LOG_RE = re.compile(r"^\[[\d:.]+\] \[[^\]]*/(\w+)\]: (.*)$")
NOISE_RE = re.compile(r"^(Server empty|Preparing|Saving|Saved|ThreadedAnvil|Time elapsed|Starting|Loading|Done|Stopping|Found new)")


def fail(msg):
    print("FAIL:", msg, file=sys.stderr)
    sys.exit(1)


def layer_state(y):
    for name, lo, hi in LAYERS:
        if lo <= y <= hi:
            return name
    return "minecraft:air"


def tree_ids():
    out = []
    for p in sorted((ROOT / "data/minecraft/worldgen/feature").glob("*.json")):
        if json.loads(p.read_text(encoding="utf-8"))["type"] == "minecraft:tree":
            out.append(p.stem)
    return out


def probes():
    """name -> dict(feature, variant, y, grid, prep: [(dx1, y1, dz1, dx2, y2, dz2, state)]); the order fixes each
    probe's strip of the world."""
    P = {}
    for t in tree_ids():
        P[t] = {"feature": t, "variant": None, "y": Y0, "grid": 10, "prep": []}
    mudwater = [(-10, 56, -10, 10, 59, 10, "minecraft:mud"), (-10, 60, -10, 10, 61, 10, "minecraft:water")]
    for t in ("mangrove", "tall_mangrove"):
        P[t + "@mudwater"] = {"feature": t, "variant": "@mudwater", "y": Y0, "grid": 10, "prep": mudwater,
                              "settle": False}
    P["rooted_azalea_tree"] = {"feature": "rooted_azalea_tree", "variant": None, "y": 49, "grid": 10,
                               "prep": [(-6, 40, -6, 6, 49, 6, "minecraft:air")]}
    for t in ("fallen_birch_tree", "fallen_jungle_tree", "fallen_oak_tree", "fallen_poplar_tree",
              "fallen_spruce_tree", "fallen_super_birch_tree", "huge_brown_mushroom", "huge_red_mushroom",
              "bamboo_some_podzol", "bamboo_no_podzol", "forest_rock"):
        P[t] = {"feature": t, "variant": None, "y": Y0, "grid": 10, "prep": []}
    ceilings = {"oak": (3, 4, 5, 6, 7, 8), "fancy_oak": (4, 5, 6, 8, 10, 13), "dark_oak_leaf_litter": (5, 6, 7, 8, 9, 10, 11),
                "spruce": (4, 5, 6, 7, 8, 9, 10), "mega_spruce": (12, 15, 18, 22, 26, 30)}
    for t, hs in ceilings.items():
        for h in hs:
            P[f"{t}_ceil{h}"] = {"feature": t, "variant": f"_ceil{h}", "y": Y0, "grid": 4, "ceiling": h,
                                 "prep": [(-8, Y0 + h, -8, 8, Y0 + h, 8, "minecraft:stone")]}
    return P


def origin_x(i):
    return X_START + STRIDE * i


def cell_centre(i, gx, gz):
    return origin_x(i) + SPACING * gx + SPACING // 2, SPACING * gz + SPACING // 2


def area_blocks(i, grid):
    x0 = origin_x(i)
    return x0 - 16, -16, x0 + SPACING * grid + 15, SPACING * grid + 15


def write_server_files(server_dir, port):
    server_dir.mkdir(parents=True, exist_ok=True)
    (server_dir / "eula.txt").write_text("eula=true\n")
    gen = {"biome": "minecraft:plains", "features": False, "lakes": False, "structure_overrides": [],
           "layers": [{"block": n, "height": hi - lo + 1} for n, lo, hi in LAYERS]}
    props = {"server-ip": "127.0.0.1", "server-port": port, "online-mode": "false", "enable-rcon": "false",
             "enable-query": "false", "max-tick-time": -1, "spawn-protection": 0, "view-distance": 2,
             "simulation-distance": 2, "region-file-compression": "deflate", "generate-structures": "false",
             "level-seed": 263, "level-name": "world", "level-type": "minecraft\\:flat",
             "generator-settings": json.dumps(gen, separators=(",", ":")), "motd": "tree probe",
             "pause-when-empty-seconds": -1}
    (server_dir / "server.properties").write_text("".join(f"{k}={v}\n" for k, v in props.items()))


class Server:
    def __init__(self, server_dir):
        libs = sorted(str(p) for p in (DATAGEN / "libraries").rglob("*.jar"))
        cp = ";".join([str(SERVER_JAR)] + libs)
        # 2 GB of heap and below-normal priority, so a probe run leaves the machine usable.
        self.proc = subprocess.Popen([str(JAVA), "-Xms1G", "-Xmx2G", "-cp", cp, "net.minecraft.server.Main", "--nogui"],
                                     cwd=server_dir, stdin=subprocess.PIPE, stdout=subprocess.PIPE,
                                     stderr=subprocess.STDOUT, creationflags=subprocess.BELOW_NORMAL_PRIORITY_CLASS)
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
    return [{"cmd": f"forceload {verb} {x1} {z} {x2} {min(z + 16 * rows - 1, z2)}", "kind": "forceload"}
            for z in range(z1, z2 + 1, 16 * rows)]


def run(args, todo):
    server_dir = args.server_dir
    write_server_files(server_dir, args.port)
    for stale in ("world", "placements.json"):
        if (server_dir / stale).exists():
            fail(f"{server_dir / stale} exists; use a fresh server folder")
    P = probes()
    summary = {"probes": {}}
    t0 = time.time()
    server = Server(server_dir)
    placements = []
    try:
        server.wait_for(lambda m: m.startswith("Done ("), 300)
        summary["timings_s"] = {"server_start": round(time.time() - t0, 1)}
        server.ask(["tick freeze"])
        summary["gamerule"] = server.ask(["gamerule random_tick_speed 0"])
        for i, name in enumerate(P):
            if name not in todo:
                continue
            p, t1, stats = P[name], time.time(), {"retries": 0}
            grid = p["grid"]
            run_commands(server, forceload_cmds("add", i, grid), stats)
            prep, items = [], []
            for gz in range(grid):
                for gx in range(grid):
                    cx, cz = cell_centre(i, gx, gz)
                    for (a, y1, b, c, y2, d, st) in p["prep"]:
                        prep.append({"cmd": f"fill {cx + a} {y1} {cz + b} {cx + c} {y2} {cz + d} {st}", "kind": "prep"})
                    items.append({"cmd": f"place feature minecraft:{p['feature']} {cx} {p['y']} {cz}", "kind": "place",
                                  "probe": name, "gx": gx, "gz": gz, "x": cx, "y": p["y"], "z": cz})
            if prep:
                run_commands(server, prep, stats)
            run_commands(server, items, stats)
            if p.get("settle", True):
                # let the leaves' scheduled distance updates run; random ticks are off (gamerule above)
                server.ask(["tick unfreeze"])
                time.sleep(SETTLE_S)
                server.ask(["tick freeze"])
            run_commands(server, forceload_cmds("remove", i, grid), stats)
            counts = Counter(it["result"] for it in items)
            summary["probes"][name] = {"responses": dict(counts), "retries": stats["retries"],
                                       "place_s": round(time.time() - t1, 1)}
            placements += [{k: it[k] for k in ("probe", "gx", "gz", "x", "y", "z", "result", "text")} for it in items]
            (server_dir / "placements.json").write_text(json.dumps({"placements": placements}, indent=0))
            print(f"{name}: {dict(counts)} retries {stats['retries']} {time.time() - t1:.0f}s", flush=True)
        t2 = time.time()
        server.send(["save-all flush"])
        server.wait_for(lambda m: "Saved the game" in m, 900)
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


def decode_indices(bs, n_names):
    data = bs.get("data")
    if data is None:
        return None
    bits = max(4, (n_names - 1).bit_length())
    per, mask = 64 // bits, (1 << bits) - 1
    longs = struct.unpack(f">{len(data) // 8}Q", data)
    shifts = [k * bits for k in range(per)]
    return [(v >> s) & mask for v in longs for s in shifts][:4096]


def scan(regions, i, p):
    """Changes as {(x, y, z): state} against the layers and the probe's prep, plus block entities."""
    grid = p["grid"]
    x1, z1, x2, z2 = area_blocks(i, grid)
    prep_cells = {}
    for gz in range(grid):
        for gx in range(grid):
            cx, cz = cell_centre(i, gx, gz)
            for (a, y1, b, c, y2, d, st) in p["prep"]:
                for x in range(cx + a, cx + c + 1):
                    for y in range(y1, y2 + 1):
                        for z in range(cz + b, cz + d + 1):
                            prep_cells[(x, y, z)] = st
    prep_secs = {(x >> 4, y >> 4, z >> 4) for (x, y, z) in prep_cells}
    changes, bes, ticks = {}, [], {}
    for cz in range(z1 >> 4, (z2 >> 4) + 1):
        for cx in range(x1 >> 4, (x2 >> 4) + 1):
            chunk = regions.chunk(cx, cz)
            if chunk is None:
                fail(f"chunk {cx},{cz} missing")
            for be in chunk.get("block_entities", []):
                bes.append(be)
            for t in chunk.get("block_ticks", []):
                ticks[(t["x"], t["y"], t["z"])] = t.get("i", "?")
            for sec in chunk["sections"]:
                sy = sec["Y"]
                if not SEC_LO <= sy <= SEC_HI:
                    continue
                bs = sec["block_states"]
                names = [state_str(q) for q in bs["palette"]]
                expected = [layer_state(sy * 16 + r) for r in range(16)]
                has_prep = (cx, sy, cz) in prep_secs
                idx = decode_indices(bs, len(names)) or [0] * 4096
                for r in range(16):
                    y = sy * 16 + r
                    e = expected[r]
                    row = idx[r * 256:(r + 1) * 256]
                    if not has_prep and e in names and row.count(names.index(e)) == 256:
                        continue
                    for j, v in enumerate(row):
                        x, z = cx * 16 + (j & 15), cz * 16 + (j >> 4)
                        want = prep_cells.get((x, y, z), e) if has_prep else e
                        if names[v] != want:
                            changes[(x, y, z)] = names[v]
    return changes, bes, ticks


def bid(state):
    return state.split("[", 1)[0].removeprefix("minecraft:")


def props(state):
    if "[" not in state:
        # 26.3 writes a default state as its bare id; leaves default to distance 7, not persistent
        return {"distance": "7", "persistent": "false"} if state.endswith("_leaves") else {}
    return dict(kv.split("=") for kv in state[state.index("[") + 1:-1].split(","))


def is_log(b):
    return b.endswith("_log") or b.endswith("_wood")


def components(cells, diag):
    cells = set(cells)
    if diag:
        nb = [(a, b, c) for a in (-1, 0, 1) for b in (-1, 0, 1) for c in (-1, 0, 1) if (a, b, c) != (0, 0, 0)]
    else:
        nb = [(1, 0, 0), (-1, 0, 0), (0, 1, 0), (0, -1, 0), (0, 0, 1), (0, 0, -1)]
    seen, n = set(), 0
    for c in cells:
        if c in seen:
            continue
        n += 1
        q = [c]
        seen.add(c)
        while q:
            x, y, z = q.pop()
            for a, b, d in nb:
                k = (x + a, y + b, z + d)
                if k in cells and k not in seen:
                    seen.add(k)
                    q.append(k)
    return n


def leaf_bfs(logs, leaves):
    dist = {}
    q = deque()
    for c in logs:
        q.append((c, 0))
    seen = set(logs)
    while q:
        (x, y, z), d = q.popleft()
        for a, b, e in ((1, 0, 0), (-1, 0, 0), (0, 1, 0), (0, -1, 0), (0, 0, 1), (0, 0, -1)):
            k = (x + a, y + b, z + e)
            if k in leaves and k not in seen:
                seen.add(k)
                dist[k] = d + 1
                q.append((k, d + 1))
    return {c: min(7, dist.get(c, 7)) for c in leaves}


def measure(cells, bes, ticks=None):
    """cells: {(dx, dy, dz): state} relative to the origin."""
    r = {"n": len(cells)}
    counts = Counter(bid(s) for s in cells.values())
    r["counts"] = dict(sorted(counts.items()))
    if not cells:
        return r
    xs = [c[0] for c in cells]
    ys = [c[1] for c in cells]
    zs = [c[2] for c in cells]
    r["bbox"] = [min(xs), max(xs), min(ys), max(ys), min(zs), max(zs)]
    r["reach_h"] = max(max(abs(c[0]), abs(c[2])) for c in cells)
    reach_id = defaultdict(int)
    for c, s in cells.items():
        b = bid(s)
        reach_id[b] = max(reach_id[b], max(abs(c[0]), abs(c[2])))
    r["reach_by_id"] = dict(sorted(reach_id.items()))
    logs = {c for c, s in cells.items() if is_log(bid(s))}
    leaves = {c for c, s in cells.items() if bid(s).endswith("_leaves")}
    stems = {c for c, s in cells.items() if bid(s) == "mushroom_stem"}
    roots = {c for c, s in cells.items() if bid(s) in ("mangrove_roots", "muddy_mangrove_roots")}
    r["logs"], r["leaves"] = len(logs), len(leaves)
    if logs:
        r["log_top"] = max(c[1] for c in logs)
        r["log_bottom"] = min(c[1] for c in logs)
        r["origin_column_logs"] = sum(1 for c in logs if c[0] == 0 and c[2] == 0)
        r["log_reach_h"] = max(max(abs(c[0]), abs(c[2])) for c in logs)
        r["log_comp6"] = components(logs, False)
        r["log_comp26"] = components(logs, True)
        if roots:
            r["logroot_comp6"] = components(logs | roots, False)
            r["logroot_comp26"] = components(logs | roots, True)
        # A default state is saved as its bare id, and a log's default axis is y.
        r["log_axis"] = dict(Counter(props(cells[c]).get("axis", "y") for c in logs))
    if stems:
        r["stem_top"] = max(c[1] for c in stems)
    if leaves:
        r["leaf_top"] = max(c[1] for c in leaves)
        r["leaf_reach_h"] = max(max(abs(c[0]), abs(c[2])) for c in leaves)
        pd = {c: int(props(cells[c]).get("distance", "-1")) for c in leaves}
        r["leaf_distance"] = dict(sorted(Counter(pd.values()).items()))
        r["leaf_persistent"] = dict(Counter(props(cells[c]).get("persistent", "?") for c in leaves))
        bfs = leaf_bfs(logs, leaves)
        r["leaf_bfs"] = dict(sorted(Counter(bfs.values()).items()))
        r["leaf_mismatch"] = sum(1 for c in leaves if pd[c] != bfs[c])
        ticks = ticks or {}
        r["leaf_pending_tick"] = sum(1 for c in leaves if c in ticks)
        r["leaf_mismatch_no_tick"] = sum(1 for c in leaves if pd[c] != bfs[c] and c not in ticks)
        r["leaf_bfs7"] = sum(1 for c in leaves if bfs[c] == 7)
        r["leaf_comp6"] = components(leaves | logs, False)
    if bes:
        r["block_entities"] = dict(Counter(be.get("id", "?") for be in bes))
        bees = [len(be.get("bees", [])) for be in bes if "bees" in be]
        if bees:
            r["bees"] = bees
    return r


def dist(values):
    if not values:
        return None
    v = sorted(values)
    n = len(v)

    def pct(q):
        return v[min(n - 1, int(q / 100 * n))]
    return {"n": n, "min": v[0], "p5": pct(5), "p50": pct(50), "p95": pct(95), "max": v[-1],
            "mean": round(sum(v) / n, 3)}


def profile(placed):
    """The per-layer shape over the placed trees: log columns (dx,dz) and logs per dy as shares of the trees; per leaf
    layer (leaves and mushroom cap blocks, dy counted from each placement's highest log, or from the origin when it has
    none) the mean per tree, the largest Chebyshev radius and each (dx,dz)'s occupancy as a share of the trees; every
    other block's total, mean per tree and dy and radius histograms (counts)."""
    n = len(placed)
    cols, logs_dy = Counter(), Counter()
    lay, lay_r = defaultdict(Counter), defaultdict(int)
    deco_dy, deco_r = defaultdict(Counter), defaultdict(Counter)
    for r in placed:
        cells = {c: bid(s) for c, s in r["cells"].items()}
        logs = [c for c, b in cells.items() if is_log(b)]
        top = max((c[1] for c in logs), default=0)
        for c in {(c[0], c[2]) for c in logs}:
            cols[c] += 1
        for c in logs:
            logs_dy[c[1]] += 1
        for c, b in cells.items():
            if b.endswith("_leaves") or b.endswith("_mushroom_block"):
                rel = c[1] - top
                lay[rel][(c[0], c[2])] += 1
                lay_r[rel] = max(lay_r[rel], max(abs(c[0]), abs(c[2])))
            elif not is_log(b):
                deco_dy[b][c[1]] += 1
                deco_r[b][max(abs(c[0]), abs(c[2]))] += 1

    def share(v):
        return round(v / n, 4)

    def xz(c):
        return f"{c[0]},{c[1]}"
    return {"n": n,
            "log_columns": {xz(c): share(v) for c, v in sorted(cols.items())},
            "logs_per_dy": {dy: share(v) for dy, v in sorted(logs_dy.items())},
            "leaf_layers": [{"dy_from_top_log": rel, "mean_per_tree": share(sum(lay[rel].values())),
                             "max_radius": lay_r[rel], "occupancy": {xz(c): share(v) for c, v in sorted(lay[rel].items())}}
                            for rel in sorted(lay)],
            "decorators": {b: {"total": sum(deco_dy[b].values()), "per_tree": share(sum(deco_dy[b].values())),
                               "dy": dict(sorted(deco_dy[b].items())), "radius": dict(sorted(deco_r[b].items()))}
                           for b in sorted(deco_dy)}}


def summarize(name, p, recs):
    placed = [r for r in recs if r["result"] == "placed"]
    s = {"generated_by": "tools/tree_probe.py", "minecraft": "26.3", "probe": name, "feature": "minecraft:" + p["feature"],
         "variant": p["variant"],
         "world": {"layers": [list(layer) for layer in LAYERS], "grid_spacing": SPACING, "random_tick_speed": 0,
                   "settle_s": SETTLE_S if p.get("settle", True) else 0},
         "y": p["y"], "grid": p["grid"], "settled": p.get("settle", True), "prep": p["prep"], "tries": len(recs),
         "responses": dict(Counter(r["result"] for r in recs))}
    if "ceiling" in p:
        s["ceiling_dy"] = p["ceiling"]
    m = [r["m"] for r in placed]
    s["placed_but_empty"] = sum(1 for x in m if x["n"] == 0)
    s["failed_but_changed"] = sum(1 for r in recs if r["result"] != "placed" and r["m"]["n"] > 0)
    for k in ("n", "logs", "leaves", "log_top", "log_bottom", "origin_column_logs", "reach_h", "log_reach_h",
              "leaf_reach_h", "leaf_top", "stem_top", "log_comp6", "log_comp26", "logroot_comp6", "logroot_comp26",
              "leaf_mismatch", "leaf_comp6", "leaf_pending_tick", "leaf_mismatch_no_tick", "leaf_bfs7"):
        d = dist([x[k] for x in m if k in x])
        if d:
            s[k] = d
    for k in ("log_top", "origin_column_logs", "log_comp6", "log_comp26"):
        vals = [x[k] for x in m if k in x]
        if vals:
            s[k + "_hist"] = dict(sorted(Counter(vals).items()))
    bb = [x["bbox"] for x in m if "bbox" in x]
    if bb:
        s["bbox_extremes"] = {"min_dx": min(b[0] for b in bb), "max_dx": max(b[1] for b in bb),
                              "min_dy": min(b[2] for b in bb), "max_dy": max(b[3] for b in bb),
                              "min_dz": min(b[4] for b in bb), "max_dz": max(b[5] for b in bb)}
    tot, pres, reach = Counter(), Counter(), defaultdict(int)
    for x in m:
        for b, c in x.get("counts", {}).items():
            tot[b] += c
            pres[b] += 1
        for b, c in x.get("reach_by_id", {}).items():
            reach[b] = max(reach[b], c)
    s["blocks"] = {b: {"mean_per_tree": round(tot[b] / max(1, len(m)), 3), "share_of_trees": round(pres[b] / max(1, len(m)), 3),
                       "max_reach_h": reach[b], "per_tree": dist([x.get("counts", {}).get(b, 0) for x in m])}
                   for b in sorted(tot)}
    for k in ("leaf_distance", "leaf_bfs", "leaf_persistent", "log_axis", "block_entities", "pending_ticks"):
        c = Counter()
        for x in m:
            c.update(x.get(k, {}))
        if c:
            s[k] = dict(sorted(c.items(), key=lambda kv: str(kv[0])))
    bees = [b for x in m for b in x.get("bees", [])]
    if bees:
        s["bees_per_hive"] = dict(sorted(Counter(bees).items()))
    s["max_reach_h_any_cell"] = max((max(abs(c[0]), abs(c[2])) for r in recs for c in r["cells"]), default=0)
    s["profile"] = profile(placed)
    s["examples"] = [{"origin": [r["x"], r["y"], r["z"]],
                      "cells": sorted([list(c) + [st] for c, st in r["cells"].items()])} for r in placed[:EXAMPLES]]
    s["per_placement"] = [{k: r["m"].get(k) for k in ("n", "logs", "leaves", "log_top", "origin_column_logs", "bbox",
                                                       "reach_h", "log_comp6", "leaf_mismatch")} | {"result": r["result"]}
                          for r in recs]
    return s


def read(args, todo):
    pl = json.loads((args.server_dir / "placements.json").read_text())["placements"]
    regions = Regions(find_region_folder(args.server_dir))
    args.staging.mkdir(parents=True, exist_ok=True)
    reading = {}
    for i, (name, p) in enumerate(probes().items()):
        recs = [r for r in pl if r["probe"] == name]
        if name not in todo or not recs:
            continue
        t0 = time.time()
        changes, bes, ticks = scan(regions, i, p)
        by_cell = defaultdict(dict)
        stray = 0
        for (x, y, z), st in changes.items():
            gx, gz = (x - origin_x(i)) // SPACING, z // SPACING
            if not (0 <= gx < p["grid"] and 0 <= gz < p["grid"]):
                stray += 1
                continue
            cx, cz = cell_centre(i, gx, gz)
            by_cell[(gx, gz)][(x - cx, y - p["y"], z - cz)] = st
        be_cell = defaultdict(list)
        for be in bes:
            gx, gz = (be["x"] - origin_x(i)) // SPACING, be["z"] // SPACING
            be_cell[(gx, gz)].append(be)
        tick_cell = defaultdict(dict)
        for (x, y, z), tid in ticks.items():
            gx, gz = (x - origin_x(i)) // SPACING, z // SPACING
            cx, cz = cell_centre(i, gx, gz)
            tick_cell[(gx, gz)][(x - cx, y - p["y"], z - cz)] = tid
        for r in recs:
            r["cells"] = by_cell.get((r["gx"], r["gz"]), {})
            r["m"] = measure(r["cells"], be_cell.get((r["gx"], r["gz"]), []), tick_cell.get((r["gx"], r["gz"])))
            r["m"]["pending_ticks"] = dict(Counter(tick_cell.get((r["gx"], r["gz"]), {}).values()))
        s = summarize(name, p, recs)
        (args.staging / f"tree_{name}.json").write_text(json.dumps(s, indent=1) + "\n", encoding="utf-8", newline="\n")
        reading[name] = {"stray_changes_outside_cells": stray, "read_s": round(time.time() - t0, 1)}
        print(f"{name}: {s['responses']} reach {s['max_reach_h_any_cell']} stray {stray} {reading[name]['read_s']}s",
              flush=True)
    update_summary(args.summary, {"read": reading})


def main():
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("step", choices=("run", "read", "all", "list"))
    ap.add_argument("probes", nargs="*")
    ap.add_argument("--server-dir", type=Path, default=SANDBOX / "server")
    ap.add_argument("--staging", type=Path, default=SANDBOX / "staging")
    ap.add_argument("--port", type=int, default=25781)
    ap.add_argument("--summary", type=Path)
    args = ap.parse_args()
    names = list(probes())
    if args.step == "list":
        print(len(names), " ".join(names))
        return
    args.summary = args.summary or args.server_dir.parent / "run_summary.json"
    for path in (args.server_dir, args.staging, args.summary):
        if path.resolve().is_relative_to(ROOT):
            fail(f"{path} is inside the reference; probe facts enter it only through build_reference.py --probes")
    todo = set(args.probes) or set(names)
    bad = todo - set(names)
    if bad:
        fail(f"unknown probes {sorted(bad)}")
    if args.step in ("run", "all"):
        run(args, todo)
    if args.step in ("read", "all"):
        read(args, todo)


if __name__ == "__main__":
    main()
