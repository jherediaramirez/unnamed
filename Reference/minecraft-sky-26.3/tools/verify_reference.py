"""Verify Reference/minecraft-sky-26.3: file hashes, re-derivation of every generated file, copies against the jars and
the report, the pinned facts, counts and spot values, and the timelines against the black-box facts.

usage: uv run --no-project --python 3.13 python -I Reference/minecraft-sky-26.3/tools/verify_reference.py
Hashes, re-derivation, spot values and the facts checks need only this folder. The jar comparisons run when the pinned
jars are present, the report comparison when the data generator's report folder is.
"""
import hashlib
import importlib.util
import json
import math
import struct
import sys
import zipfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location("build_reference", Path(__file__).with_name("build_reference.py"))
build = importlib.util.module_from_spec(spec)
spec.loader.exec_module(build)
errors = []


def check(condition, message):
    if not condition:
        errors.append(message)


def load(name):
    return json.loads((ROOT / name).read_text(encoding="utf-8"))


# 1. Every file is listed with its SHA-256, and nothing else is in the folder.
sums = {}
for line in (ROOT / "SHA256SUMS.txt").read_text(encoding="utf-8").splitlines():
    digest, _, name = line.partition("  ")
    check(len(digest) == 64 and name and name not in sums, f"SHA256SUMS.txt: bad or repeated line {line!r}")
    sums[name] = digest
on_disk = {p.relative_to(ROOT).as_posix() for p in ROOT.rglob("*")
           if p.is_file() and p.name != "SHA256SUMS.txt" and "__pycache__" not in p.parts}
check(on_disk == set(sums), f"file list differs from SHA256SUMS.txt: +{sorted(on_disk - set(sums))[:5]} "
                            f"-{sorted(set(sums) - on_disk)[:5]}")
for name, digest in sums.items():
    path = ROOT / name
    if path.is_file():
        check(hashlib.sha256(path.read_bytes()).hexdigest() == digest, f"hash mismatch: {name}")

# 2. Re-derive every generated file from the folder's own copies; the file list is exactly what the build writes.
copies = sorted(n for n in sums if n.startswith(("data/", "assets/")))
generated = {}
try:
    generated = build.derive(lambda name: (ROOT / name).read_bytes(), copies)
except (SystemExit, OSError, KeyError, ValueError) as error:
    errors.append(f"re-derivation failed: {error!r}")
for name, data in generated.items():
    check((ROOT / name).is_file() and (ROOT / name).read_bytes() == data, f"re-derived file differs: {name}")
expected = set(copies) | set(build.GENERATED) | set(build.FACTS) | set(build.TOOLS) | {build.REGISTRY, "README.md"}
check(expected == set(sums), f"files differ from the build's list: +{sorted(set(sums) - expected)[:5]} "
                             f"-{sorted(expected - set(sums))[:5]}")
manifest = load("manifest.json")
check(manifest["files"]["copies"] == copies, "manifest's copy list differs from the folder")

# 3. The facts are the pinned files.
for name, digest in build.FACTS.items():
    check((ROOT / name).is_file() and hashlib.sha256((ROOT / name).read_bytes()).hexdigest() == digest,
          f"facts file differs from its pinned SHA-256: {name}")

# 4. Every copy equals its jar entry (data: the server jar's, and the client jar holds the same bytes).
jar_checked = 0
if build.SERVER_JAR.exists():
    server = build.server_jar()
    check(sorted(build.data_names(server.namelist())) == [n for n in copies if n.startswith("data/")],
          "the server jar's data files differ from the copies")
    for name in copies:
        if name.startswith("data/"):
            jar_checked += 1
            check((ROOT / name).read_bytes() == server.read(name), f"differs from the server jar: {name}")
else:
    print(f"note: server jar absent ({build.SERVER_JAR}); server comparisons skipped")
if build.CLIENT_JAR.exists():
    client = build.client_jar()
    names = set(client.namelist())
    for name in copies:
        jar_checked += 1
        check(name in names and (ROOT / name).read_bytes() == client.read(name), f"differs from the client jar: {name}")
    moons = sorted(n for n in names if n.startswith(build.CELESTIAL + "moon/") and n.endswith(".png"))
    check(moons == [n for n in copies if n.startswith(build.CELESTIAL + "moon/")], "client jar's moon textures differ")
else:
    print(f"note: client jar absent ({build.CLIENT_JAR}); client comparisons skipped")

# 5. The registry copy equals the data generator's output.
report_checked = 0
if build.DEFAULT_REPORTS.exists():
    registry = json.loads((build.DEFAULT_REPORTS / "registries.json").read_text(encoding="utf-8"))
    check(load(build.REGISTRY) == {"minecraft:environment_attribute": registry["minecraft:environment_attribute"]},
          "environment attribute registry differs from registries.json")
    report_checked = 1
else:
    print(f"note: report folder absent ({build.DEFAULT_REPORTS}); report comparison skipped")

# 6. Counts and spot values (scout N, section 3).
day = load("data/minecraft/timeline/day.json")
moon = load("data/minecraft/timeline/moon.json")
attributes = load("attributes.json")
celestial = load("celestial.json")
check(manifest["counts"] == {"timelines": 4, "timeline_tags": 4, "dimension_types": 4, "world_clocks": 2,
                             "environment_attributes": 51, "day_tracks": 18, "moon_phases": 8, "sweep_ticks": 65,
                             "sweep_moon_rows": 16}, f"counts differ: {manifest['counts']}")
check(day["period_ticks"] == 24000 and moon["period_ticks"] == 192000, "periods: day 24,000, moon 192,000")
markers = {k: (v["ticks"] if isinstance(v, dict) else v) for k, v in day["time_markers"].items()}
shown = sorted(k for k, v in day["time_markers"].items() if isinstance(v, dict) and v.get("show_in_commands"))
check(markers == {"minecraft:day": 1000, "minecraft:noon": 6000, "minecraft:night": 13000,
                  "minecraft:midnight": 18000, "minecraft:wake_up_from_sleep": 0,
                  "minecraft:roll_village_siege": 18000}, f"day markers differ: {markers}")
check(shown == ["minecraft:day", "minecraft:midnight", "minecraft:night", "minecraft:noon"], "markers in commands")
check(attributes["overworld_timelines"] == ["minecraft:villager_schedule", "minecraft:day", "minecraft:moon",
                                            "minecraft:early_game"], "the overworld's timelines (#in_overworld)")
by_id = {a["id"]: a for a in attributes["attributes"]}
check(by_id["minecraft:visual/sky_color"].get("overworld") == "#78a7ff" and
      by_id["minecraft:visual/fog_color"].get("overworld") == "#c0d8ff" and
      by_id["minecraft:visual/ambient_light_color"].get("overworld") == "#0a0a0a", "overworld base colours")
check([p["phase"] for p in celestial["moon_phases"]] ==
      ["full_moon", "waning_gibbous", "third_quarter", "waning_crescent", "new_moon", "waxing_crescent",
       "first_quarter", "waxing_gibbous"], "moon phase order")
check(all(t["size"] == [32, 32] for t in [celestial["sun"]] + celestial["moon_phases"]), "celestial textures 32 x 32")
for clock in ("overworld", "the_end"):
    check(load(f"data/minecraft/world_clock/{clock}.json") == {}, f"world clock {clock} has no fields")

# 7. The timelines against scout N's sweep, read as README.md says: keyframes sorted by tick (file order kept for equal
# ticks); between the last keyframe at or before t and the next one after it, around the period; the progress eased
# (linear, constant, or the cubic Bezier through (x1, y1) and (x2, y2): x(s) = progress, value y(s)); the value lerped;
# then the modifier applied to the attribute's base (the wiki default, or the overworld's value) in single precision.
def f32(x):
    return struct.unpack("<f", struct.pack("<f", x))[0]


def bezier(x1, y1, x2, y2, u):
    def coord(s, a, b):
        return 3 * (1 - s) ** 2 * s * a + 3 * (1 - s) * s * s * b + s ** 3
    lo, hi = 0.0, 1.0
    for _ in range(60):
        mid = (lo + hi) / 2
        lo, hi = (mid, hi) if coord(mid, x1, x2) < u else (lo, mid)
    return coord((lo + hi) / 2, y1, y2)


def sample(timeline, attribute, t):
    track = timeline["tracks"][attribute]
    keys = sorted(track["keyframes"], key=lambda k: k["ticks"])
    period = timeline.get("period_ticks")
    t = t % period if period else t
    before = [k for k in keys if k["ticks"] <= t]
    after = [k for k in keys if k["ticks"] > t]
    if not period and not (before and after):
        return (before or keys)[-1 if before else 0]["value"]
    p, pt = (before[-1], before[-1]["ticks"]) if before else (keys[-1], keys[-1]["ticks"] - period)
    n, nt = (after[0], after[0]["ticks"]) if after else (keys[0], keys[0]["ticks"] + period)
    ease = track.get("ease", "linear")
    known = ease in ("linear", "constant") or (isinstance(ease, dict) and list(ease) == ["cubic_bezier"])
    check(known, f"{attribute}: unknown ease {ease!r}")
    if ease == "constant" or not isinstance(p["value"], (int, float)) or isinstance(p["value"], bool):
        return p["value"]
    u = (t - pt) / (nt - pt)
    if isinstance(ease, dict):
        u = bezier(*ease["cubic_bezier"], u)
    return f32(p["value"] + (n["value"] - p["value"]) * u)


def modified(attribute, t):
    track = sample(day, attribute, t)
    base = by_id[attribute].get("overworld", by_id[attribute]["wiki_default"])
    modifier = day["tracks"][attribute].get("modifier", "override")
    rules = {"override": lambda: track, "multiply": lambda: f32(base * track), "maximum": lambda: max(base, track)}
    check(modifier in rules, f"{attribute}: modifier {modifier!r} is not read here")
    return rules.get(modifier, rules["override"])()


sweep = load("facts/sweep.json")
for row in sweep["ticks"]:
    t = row["tick"]
    for attribute, tolerance in (("sun_angle", .01), ("moon_angle", .01), ("star_angle", .01),
                                 ("star_brightness", 1e-5), ("sky_light_factor", 1e-5), ("sky_light_level", 1e-5)):
        full = ("minecraft:gameplay/" if attribute == "sky_light_level" else "minecraft:visual/") + attribute
        value = modified(full, t)
        check(abs(value - row[attribute]) <= tolerance, f"sweep: {attribute} at {t}: {value:.5f}, measured "
                                                        f"{row[attribute]}")
    light = math.ceil(modified("minecraft:gameplay/sky_light_level", t))
    check(light == row["light"], f"sweep: open-sky light at {t}: {light}, measured {row['light']}")
    check(row["detector_inverted"] == 15 - row["light"], f"sweep: inverted detector at {t}")
    check(row["detector"] <= row["light"], f"sweep: detector above the light at {t}")
for row in sweep["moon"]:
    check(sample(moon, "minecraft:visual/moon_phase", row["tick"]) == row["phase"], f"sweep: moon at {row['tick']}")

# 8. DN0's colour probe against the data: the controls hold, and every plateau edge sits at a keyframe of its track: a
# plateau still matches at the keyframe that ends it and matches from the one that starts it (README, "Colours").
colour = load("facts/colour_scan.json")
check(all(c["matches"] == c["expected"] for c in colour["controls"]) and len(colour["controls"]) == 5,
      "colour probe: controls")
check(len(colour["edges"]) == 10, "colour probe: 10 edges")
for edge in colour["edges"]:
    keys = sorted(day["tracks"][edge["attr"]]["keyframes"], key=lambda k: k["ticks"])
    ticks = [k["ticks"] for k in keys]
    tick = edge["data_keyframe"] % 24000
    ends = edge["search"].endswith("ends")
    check(ends or edge["search"].endswith("starts"), f"colour probe: {edge['search']}: neither an end nor a start")
    check(tick in ticks, f"colour probe: {edge['search']}: not a keyframe tick")
    if tick in ticks:
        # The plateau side of the keyframe holds the same value; the other side changes.
        i = ticks.index(tick)
        inner, outer = keys[(i - 1) % len(keys) if ends else (i + 1) % len(keys)], keys[(i + 1) % len(keys) if ends else (i - 1) % len(keys)]
        check(inner["value"] == keys[i]["value"] != outer["value"],
              f"colour probe: {edge['search']}: keyframe {tick} doesn't bound a plateau on that side")
    late = 1 if ends else 0
    check(edge["first_tick_changed"] == edge["data_keyframe"] + late,
          f"colour probe: {edge['search']}: {edge['first_tick_changed']}, expected {edge['data_keyframe'] + late}")
# The dawn ramp wraps on the first day too: white only from the 133 key, at total 0 as at 24,000.
check({r["total"]: r["white_sky_is_ffffff"] for r in colour["tick0"]} ==
      {0: False, 1: False, 132: False, 133: True, 24000: False, 24133: True}, "colour probe: the white sky near tick 0")

if errors:
    print("REFERENCE FAIL")
    print("\n".join(errors[:40]))
    sys.exit(1)
partial = "" if jar_checked == 38 and report_checked else " (PARTIAL: a jar or the report folder is absent)"
print(f"REFERENCE OK{partial}: {len(sums)} files hashed, {len(generated)} generated files re-derived, {jar_checked} copies "
      f"compared with the jars, {report_checked} with the data generator's report; {len(day['tracks'])} day tracks, "
      f"{len(attributes['attributes'])} attributes, {len(celestial['moon_phases'])} moon phases; the timelines agree "
      f"with the sweep at {len(sweep['ticks'])} ticks and {len(sweep['moon'])} moon rows, and with the colour probe's "
      f"{len(colour['edges'])} edges")
