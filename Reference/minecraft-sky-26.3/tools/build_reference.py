"""Build Reference/minecraft-sky-26.3 from the pinned 26.3 jars, the data generator's registries report and the
black-box day/night facts.

usage (from the repository root):
    uv run --no-project --python 3.13 python -I Reference/minecraft-sky-26.3/tools/build_reference.py
        [--reports C:/dev/sandbox/mc-26.3-blocks/reports/reports] [--sweep <sweep.json>] [--colour <colour_scan.json>]
        [--sums-only]

Inputs:
    the official server jar (SHA-1 pinned below; its bundled server-26.3.jar, SHA-256 pinned): the four timelines,
        the timeline tags, the four dimension types and the two world clocks (data/minecraft/)
    the client jar (SHA-1 pinned below, the block and item references' client): the sun, the eight moon phases and the
        celestials atlas
    the data generator's reports (`--reports`, from the same server jar): registries.json's environment attributes
    scout N's sweep (`--sweep`) and DN0's colour scan (`--colour`): black-box facts from a headless 26.3 server,
        copied byte for byte and pinned by SHA-256 below

Copies are byte for byte. Every generated file comes from derive(), which reads only this folder's copies, so
verify_reference.py re-derives each file from the folder itself. The folder must hold only tools/ and README.md, so an
accepted reference is never overwritten; to rebuild, move the old folder away first. After a README or tool edit,
--sums-only rewrites only SHA256SUMS.txt.

Data only: the game's code is never read. Attribute defaults come from minecraft.wiki ("Environment attribute"), as
listed in README.md; the facts check them black-box.
"""
import argparse
import hashlib
import io
import json
import os
import struct
import zipfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
MINECRAFT_VERSION = "26.3"
SERVER_JAR = Path("C:/dev/sandbox/mc-26.3-blocks/downloads/server.jar")
SERVER_SHA1 = "33680f5f2ac32864d6d7cf5e56a705fdb3e05f4c"
SERVER_INNER = "META-INF/versions/26.3/server-26.3.jar"
SERVER_INNER_SHA256 = "a362163eec5d1612d520772bc16e5b39c09e3b234fdc045f56bf544284ee8ae6"
CLIENT_JAR = Path(os.environ.get("APPDATA", "")) / "ModrinthApp/meta/versions/26.3/26.3.jar"
CLIENT_SHA1 = "e877b6a07acd633fb3bb475002175cec036e7b87"
DEFAULT_REPORTS = Path("C:/dev/sandbox/mc-26.3-blocks/reports/reports")
DEFAULT_SWEEP = Path("C:/dev/sandbox/scout-daynight/probes/sweep.json")
DEFAULT_COLOUR = Path("C:/dev/sandbox/water-dn0/colour/colour_scan.json")

# The data folders copied from the server jar, with the file count each must have.
DATA_FOLDERS = {"data/minecraft/timeline/": 4, "data/minecraft/tags/timeline/": 4,
                "data/minecraft/dimension_type/": 4, "data/minecraft/world_clock/": 2}
CELESTIAL = "assets/minecraft/textures/environment/celestial/"
SUN = CELESTIAL + "sun.png"
ATLAS = "assets/minecraft/atlases/celestials.json"
REGISTRY = "reports/registries-environment_attribute.json"
FACTS = {"facts/sweep.json": "485a077014b364d65d79c5d9bbcfd9a660d2df1df94f2bbac0f487e724f3da39",
         "facts/colour_scan.json": "63352536c08d9df0d612820ce52f9935d849753f4f9c869ed40d1292741983c2"}
FACT_SOURCES = {
    "facts/sweep.json": "scout N's sweep (sweep_probe.py on a headless 26.3 server; the method is in "
                        "Docs/Records/2026-10-10-WaterSky-Scout-N-DayNight.md, section 5)",
    "facts/colour_scan.json": "DN0's colour probe on scout N's harness (Docs/Records/2026-10-10-WaterSky-DN0.md)"}
GENERATED = ["attributes.json", "celestial.json", "manifest.json"]
TOOLS = ["tools/build_reference.py", "tools/verify_reference.py"]

# minecraft.wiki "Environment attribute" (defaults, as scout N's notes record them), for the attributes the sky,
# fog, light and clouds read. The other attributes are left at null: nothing here uses them.
WIKI_DEFAULTS = {
    "minecraft:gameplay/sky_light_level": 15.0,
    "minecraft:visual/ambient_light_color": "#000000",
    "minecraft:visual/block_light_tint": "#ffd88c",
    "minecraft:visual/cloud_color": "#00000000",
    "minecraft:visual/cloud_fog_end_distance": 2048.0,
    "minecraft:visual/cloud_height": 192.33,
    "minecraft:visual/fog_color": "#000000",
    "minecraft:visual/fog_end_distance": 1024.0,
    "minecraft:visual/fog_start_distance": 0.0,
    "minecraft:visual/moon_angle": 0.0,
    "minecraft:visual/moon_phase": "full_moon",
    "minecraft:visual/night_vision_color": "#999999",
    "minecraft:visual/sky_color": "#000000",
    "minecraft:visual/sky_fog_end_distance": 512.0,
    "minecraft:visual/sky_light_color": "#ffffff",
    "minecraft:visual/sky_light_factor": 1.0,
    "minecraft:visual/star_angle": 0.0,
    "minecraft:visual/star_brightness": 0.0,
    "minecraft:visual/sun_angle": 0.0,
    "minecraft:visual/sunrise_sunset_color": "#00000000",
    "minecraft:visual/water_fog_color": "#050533",
    "minecraft:visual/water_fog_end_distance": 96.0,
    "minecraft:visual/water_fog_start_distance": -8.0,
}


def dumps(value):
    return (json.dumps(value, indent=1, ensure_ascii=False) + "\n").encode("utf-8")


def sha256(data):
    return hashlib.sha256(data).hexdigest()


def server_jar():
    """The official server jar's bundled server-26.3.jar, after checking both hashes."""
    data = SERVER_JAR.read_bytes()
    if hashlib.sha1(data).hexdigest() != SERVER_SHA1:
        raise SystemExit("server jar SHA-1 differs from the pinned one")
    inner = zipfile.ZipFile(io.BytesIO(data)).read(SERVER_INNER)
    if sha256(inner) != SERVER_INNER_SHA256:
        raise SystemExit("the server jar's bundled server-26.3.jar differs from the pinned one")
    return zipfile.ZipFile(io.BytesIO(inner))


def client_jar():
    if hashlib.sha1(CLIENT_JAR.read_bytes()).hexdigest() != CLIENT_SHA1:
        raise SystemExit("client jar SHA-1 differs from the pinned one")
    return zipfile.ZipFile(CLIENT_JAR)


def data_names(names):
    """The data files to copy, from a jar's name list; stops if a folder's count differs."""
    out = []
    for folder, count in DATA_FOLDERS.items():
        found = sorted(n for n in names if n.startswith(folder) and n.endswith(".json") and "/" not in n[len(folder):])
        if len(found) != count:
            raise SystemExit(f"{folder}: {len(found)} files, expected {count}")
        out += found
    return out


def png_size(data):
    if data[:8] != b"\x89PNG\r\n\x1a\n" or data[12:16] != b"IHDR":
        raise SystemExit("not a PNG")
    return list(struct.unpack(">II", data[16:24]))


def resolve_tag(read, tag, seen=None):
    """A timeline tag's entries, nested tags expanded in place (file order)."""
    seen = set() if seen is None else seen
    if tag in seen:
        return []
    seen.add(tag)
    out = []
    for value in json.loads(read(f"data/minecraft/tags/timeline/{tag.removeprefix('minecraft:')}.json"))["values"]:
        value = value["id"] if isinstance(value, dict) else value
        out += resolve_tag(read, value[1:], seen) if value.startswith("#") else [value]
    return out


def derive(read, names):
    """The generated files, from the copies alone. `read(name)` returns a copy's bytes; `names` lists the copies."""
    overworld = json.loads(read("data/minecraft/dimension_type/overworld.json"))
    if not isinstance(overworld["timelines"], str) or not overworld["timelines"].startswith("#"):
        raise SystemExit("the overworld's timelines are not a tag")
    timelines = resolve_tag(read, overworld["timelines"][1:])
    tracks = {}
    for timeline in timelines:
        data = json.loads(read(f"data/minecraft/timeline/{timeline.removeprefix('minecraft:')}.json"))
        for attribute, track in data["tracks"].items():
            tracks.setdefault(attribute, []).append(
                {"timeline": timeline, "period_ticks": data.get("period_ticks"),
                 "modifier": track.get("modifier", "override"), "ease": track.get("ease", "linear"),
                 "keyframes": len(track["keyframes"])})
    registry = json.loads(read(REGISTRY))["minecraft:environment_attribute"]
    entries = sorted(registry["entries"].items(), key=lambda kv: kv[1]["protocol_id"])
    unknown = sorted((set(WIKI_DEFAULTS) | set(tracks) | set(overworld["attributes"])) - set(registry["entries"]))
    if unknown:
        raise SystemExit(f"attributes the registry lacks (wiki defaults, tracks or overworld values): {unknown}")
    attributes = []
    for attribute, entry in entries:
        row = {"id": attribute, "protocol_id": entry["protocol_id"], "wiki_default": WIKI_DEFAULTS.get(attribute)}
        if attribute in overworld["attributes"]:
            row["overworld"] = overworld["attributes"][attribute]
        if attribute in tracks:
            row["overworld_timelines"] = tracks[attribute]
        attributes.append(row)
    attributes_json = {"minecraft_version": MINECRAFT_VERSION, "overworld_timelines": timelines,
                       "attributes": attributes}

    moon = json.loads(read("data/minecraft/timeline/moon.json"))
    phases = [k["value"] for k in moon["tracks"]["minecraft:visual/moon_phase"]["keyframes"]]
    def texture(name):
        data = read(name)
        return {"texture": name, "size": png_size(data), "png_sha256": sha256(data)}
    celestial_json = {"minecraft_version": MINECRAFT_VERSION, "atlas": ATLAS, "sun": texture(SUN),
                      "moon_phases": [{"index": i, "phase": p, **texture(f"{CELESTIAL}moon/{p}.png")}
                                      for i, p in enumerate(phases)]}
    expected_moon = {f"{CELESTIAL}moon/{p}.png" for p in phases}
    present_moon = {n for n in names if n.startswith(CELESTIAL + "moon/")}
    if expected_moon != present_moon:
        raise SystemExit(f"moon textures differ from moon.json's phases: {sorted(expected_moon ^ present_moon)}")

    sweep = json.loads(read("facts/sweep.json"))
    day = json.loads(read("data/minecraft/timeline/day.json"))
    manifest = {
        "minecraft_version": MINECRAFT_VERSION,
        "server_sha1": SERVER_SHA1,
        "server_inner": SERVER_INNER,
        "server_inner_sha256": SERVER_INNER_SHA256,
        "client_sha1": CLIENT_SHA1,
        "report": "data generator --reports, registries.json (minecraft:environment_attribute), from the server jar "
                  "above",
        "facts": {name: {"sha256": digest, "source": FACT_SOURCES[name]} for name, digest in FACTS.items()},
        "counts": {
            "timelines": sum(n.startswith("data/minecraft/timeline/") for n in names),
            "timeline_tags": sum(n.startswith("data/minecraft/tags/timeline/") for n in names),
            "dimension_types": sum(n.startswith("data/minecraft/dimension_type/") for n in names),
            "world_clocks": sum(n.startswith("data/minecraft/world_clock/") for n in names),
            "environment_attributes": len(attributes),
            "day_tracks": len(day["tracks"]),
            "moon_phases": len(phases),
            "sweep_ticks": len(sweep["ticks"]),
            "sweep_moon_rows": len(sweep["moon"]),
        },
        "files": {"copies": sorted(names), "generated": sorted(GENERATED), "reports": [REGISTRY],
                  "facts": sorted(FACTS), "tools": TOOLS},
    }
    return {"attributes.json": dumps(attributes_json), "celestial.json": dumps(celestial_json),
            "manifest.json": dumps(manifest)}


def check_sums_only():
    """--sums-only is for a README or tool edit: every other listed file must still have its listed hash."""
    listed = ROOT / "SHA256SUMS.txt"
    if not listed.exists():
        raise SystemExit("no SHA256SUMS.txt: build the reference instead")
    for line in listed.read_text(encoding="utf-8").splitlines():
        digest, name = line.split("  ", 1)
        if name != "README.md" and not name.startswith("tools/") and (
                not (ROOT / name).is_file() or sha256((ROOT / name).read_bytes()) != digest):
            raise SystemExit(f"--sums-only refused: {name} changed; only README.md and tools/ may change")


def write_sums():
    files = sorted(p.relative_to(ROOT).as_posix() for p in ROOT.rglob("*")
                   if p.is_file() and p.name != "SHA256SUMS.txt" and "__pycache__" not in p.parts)
    (ROOT / "SHA256SUMS.txt").write_text(
        "".join(f"{sha256((ROOT / f).read_bytes())}  {f}\n" for f in files), encoding="utf-8", newline="\n")
    print(f"SHA256SUMS.txt: {len(files)} files")


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--reports", type=Path, default=DEFAULT_REPORTS)
    parser.add_argument("--sweep", type=Path, default=DEFAULT_SWEEP)
    parser.add_argument("--colour", type=Path, default=DEFAULT_COLOUR)
    parser.add_argument("--sums-only", action="store_true")
    args = parser.parse_args()
    if args.sums_only:
        check_sums_only()
        write_sums()
        return
    extra = [p.name for p in ROOT.iterdir() if p.name not in ("tools", "README.md")]
    if extra:
        raise SystemExit(f"refusing to overwrite an accepted reference ({extra}); move the folder away first")
    server, client = server_jar(), client_jar()
    copies = {name: server.read(name) for name in data_names(server.namelist())}
    for name in [SUN, ATLAS] + sorted(n for n in client.namelist()
                                      if n.startswith(CELESTIAL + "moon/") and n.endswith(".png")):
        copies[name] = client.read(name)
    registry = json.loads((args.reports / "registries.json").read_text(encoding="utf-8"))
    copies[REGISTRY] = (json.dumps({"minecraft:environment_attribute": registry["minecraft:environment_attribute"]},
                                   indent=2) + "\n").encode("utf-8")
    for name, path in (("facts/sweep.json", args.sweep), ("facts/colour_scan.json", args.colour)):
        data = path.read_bytes()
        if sha256(data) != FACTS[name]:
            raise SystemExit(f"{path}: SHA-256 differs from the pinned {name}")
        copies[name] = data
    names = [n for n in copies if n.startswith(("data/", "assets/"))]
    generated = derive(lambda name: copies[name], names)
    for name, data in list(copies.items()) + list(generated.items()):
        (ROOT / name).parent.mkdir(parents=True, exist_ok=True)
        (ROOT / name).write_bytes(data)
    print(f"built: {len(generated)} generated files, {len(copies)} copies")
    write_sums()


if __name__ == "__main__":
    main()
