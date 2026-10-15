"""Black-box probe of the 26.3 server: attributes, health and loot rolls of 8 mobs via console commands.
Server wrapper copied from Reference/minecraft-worldgen-26.3/tools/feature_probe.py (S1b). Run:
uv run --no-project --python 3.13 python -I mob_probe.py <out_dir>
Writes <out_dir>/mob_probe.json (raw console lines) and <out_dir>/mob_attributes.json (derived, copied to facts/ by hand:
Reference/minecraft-entities-26.3/facts/mob_attributes.json, survival 20a). Without a server: `mob_probe.py --derive
<raw mob_probe.json> <out.json>`. Black box: only server console commands, no game code is read."""
import hashlib, json, queue, re, subprocess, sys, threading, time
from pathlib import Path
JAVA = Path(r"C:\Users\jherediaramirez\.gradle\jdks\eclipse_adoptium-25-amd64-windows.2\bin\java.exe")
DATAGEN = Path(r"C:\dev\sandbox\mc-26.3-blocks\datagen")
SERVER_JAR = DATAGEN / "versions" / "26.3" / "server-26.3.jar"
LOG_RE = re.compile(r"^\[[^\]]*\] \[[^/]*/(\w+)\]: (.*)$")
MOBS = ["cow", "pig", "sheep", "chicken", "zombie", "skeleton", "creeper", "spider"]
ATTRS = ["max_health", "movement_speed", "attack_damage", "attack_knockback", "knockback_resistance", "follow_range",
         "armor", "step_height", "gravity", "safe_fall_distance", "tempt_range", "scale", "jump_strength"]
ROLLS = 64
SERVER_SHA1 = "cc3964451a3b32a0488110d5e3e6ad8d75fe6a66"
NUM = re.compile(r" is (-?[0-9.eE+-]+)$")
def derive(raw):
    """Clean form: per mob, Health and each attribute's base value as a number (null when the mob lacks it)."""
    mobs = {}
    for mob in MOBS:
        attrs = {}
        for a in ATTRS:
            m = NUM.search(raw["attributes"][mob][a][0])
            attrs[a] = float(m.group(1)) if m else None
        h = re.search(r"data: (-?[0-9.]+)f$", raw["health"][mob][0])
        mobs[mob] = {"health": float(h.group(1)), "attributes": attrs}
    return json.dumps({"source": {"server_jar_sha1": SERVER_SHA1}, "mobs": mobs}, indent=1) + "\n"
if len(sys.argv) == 4 and sys.argv[1] == "--derive":
    Path(sys.argv[3]).write_text(derive(json.loads(Path(sys.argv[2]).read_text(encoding="utf-8"))), encoding="utf-8", newline="\n"); sys.exit(0)
def fail(m): print("FAIL", m); sys.exit(1)
class Server:
    def __init__(s, d):
        libs = sorted(str(p) for p in (DATAGEN / "libraries").rglob("*.jar"))
        s.proc = subprocess.Popen([str(JAVA), "-Xms512M", "-Xmx2G", "-cp", ";".join([str(SERVER_JAR)] + libs), "net.minecraft.server.Main", "--nogui"],
                                  cwd=d, stdin=subprocess.PIPE, stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
        s.lines = queue.Queue(); s.log = open(d / "probe-server.log", "w", encoding="utf-8")
        threading.Thread(target=s._pump, daemon=True).start()
    def _pump(s):
        for raw in s.proc.stdout:
            l = raw.decode("utf-8", "replace").rstrip(); s.log.write(l + "\n"); s.log.flush(); s.lines.put(l)
        s.lines.put(None)
    def wait_for(s, pred, timeout):
        seen, end = [], time.time() + timeout
        while time.time() < end:
            try: l = s.lines.get(timeout=1)
            except queue.Empty: continue
            if l is None: fail("server exited")
            m = LOG_RE.match(l)
            if not m or m.group(1) not in ("INFO", "WARN"): continue
            seen.append(m.group(2))
            if pred(m.group(2)): return seen
        fail(f"timeout {seen[-5:]}")
    def send(s, ls): s.proc.stdin.write(("\n".join(ls) + "\n").encode()); s.proc.stdin.flush()
    def ask(s, cmd):
        tag = f"PROBE_{time.time_ns()}"; s.send([cmd, f"say {tag}"])
        return [m for m in s.wait_for(lambda m: m.endswith(tag), 120)[:-1]]
if hashlib.sha1(SERVER_JAR.read_bytes()).hexdigest() != SERVER_SHA1: fail("the server jar differs from the pinned one")
out = Path(sys.argv[1]); d = out / "server"; d.mkdir(parents=True, exist_ok=True)
(d / "eula.txt").write_text("eula=true\n")
gen = {"biome": "minecraft:plains", "features": False, "lakes": False, "structure_overrides": [], "layers": [{"block": "minecraft:stone", "height": 4}]}
props = {"server-ip": "127.0.0.1", "server-port": 25591, "online-mode": "false", "spawn-protection": 0, "view-distance": 2, "simulation-distance": 2,
         "level-seed": 263, "level-type": "minecraft\\:flat", "generator-settings": json.dumps(gen, separators=(",", ":")), "difficulty": "normal",
         "spawn-monsters": "false", "spawn-animals": "false", "pause-when-empty-seconds": -1, "max-tick-time": -1}
(d / "server.properties").write_text("".join(f"{k}={v}\n" for k, v in props.items()))
srv = Server(d); srv.wait_for(lambda m: m.startswith("Done ("), 600)
res = {"attributes": {}, "health": {}, "loot": {}}
srv.ask("gamerule doMobLoot true"); srv.ask("forceload add 0 0")
for i, mob in enumerate(MOBS):
    x = i * 4
    srv.ask(f"summon minecraft:{mob} {x} 0 0 {{NoAI:1b,PersistenceRequired:1b,Tags:[\"p\"]}}")
    sel = f"@e[type=minecraft:{mob},limit=1,tag=p]"
    res["attributes"][mob] = {a: srv.ask(f"attribute {sel} minecraft:{a} base get") for a in ATTRS}
    res["health"][mob] = srv.ask(f"data get entity {sel} Health")
    srv.ask(f"setblock {x} 2 2 minecraft:chest")
    for _ in range(ROLLS): srv.ask(f"loot insert {x} 2 2 kill {sel}")
    res["loot"][mob] = srv.ask(f"data get block {x} 2 2 Items")
(out / "mob_probe.json").write_text(json.dumps(res, indent=1))
(out / "mob_attributes.json").write_text(derive(res), encoding="utf-8", newline="\n")
srv.send(["stop"]); srv.proc.wait(120); print("ok")
