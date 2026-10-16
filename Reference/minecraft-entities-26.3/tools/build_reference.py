"""Build Reference/minecraft-entities-26.3: entity textures copied from the pinned 26.3 client jar, as stand-ins (Jesus,
2026-10-14: "Use any needed texture from Java Minecraft as a stand in so we can replace later"; Docs/StandIns.md).

usage (from the repository root):
    uv run --no-project --python 3.13 --with pillow python -I Reference/minecraft-entities-26.3/tools/build_reference.py --facts <dir>

Input (read-only): the 26.3 client jar (SHA-1 pinned below, the same pin as Reference/minecraft-items-26.3). TEXTURES
lists the jar entries copied, in order; a later step appends entries (mob textures, survival track T6) and never
reorders or removes one, so earlier entries and their files never move. LOOT lists the jar's data/minecraft/loot_table
entries (the client jar carries data/) copied the same way, byte for byte, append only.

facts/ comes from tools/EntityModelFacts.java (entity_models.json: the jar's baked model parts and cubes, public API
only) and tools/mob_probe.py (mob_attributes.json: a headless 26.3 server console probe, pinned server jar SHA-1 below).
Both are produced by their tools and only hashed here: run them, then pass their output with --facts <dir>, which copies
the two files into facts/ (a rebuild keeps them byte-identical).

The folder must hold only tools/ and README.md, so an accepted reference is never overwritten; to rebuild, move the old
folder away first. textures.json comes from derive(), which verify_reference.py runs again from the copied files.
Data only: the game code is never read.
"""
import hashlib
import io
import json
import os
import sys
import zipfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
CLIENT_SHA1 = "e877b6a07acd633fb3bb475002175cec036e7b87"
CLIENT_JAR = Path(os.environ.get("APPDATA", "")) / "ModrinthApp/meta/versions/26.3/26.3.jar"
# Jar entries under assets/minecraft/textures/, copied to the same path here. Append only.
TEXTURES = [
    # Survival step 12 (XP orbs): a 64 x 64 sheet of 16 x 16 orb icons (value report `xp.value_icons`).
    "entity/experience/experience_orb.png",
    # Survival step 20a (mobs): the first mobs' textures (sheep 64 x 32, the rest 64 x 64 or smaller sheets).
    "entity/cow/cow_temperate.png", "entity/cow/cow_cold.png", "entity/cow/cow_warm.png",
    "entity/pig/pig_temperate.png", "entity/pig/pig_cold.png", "entity/pig/pig_warm.png",
    "entity/sheep/sheep.png", "entity/sheep/sheep_wool.png", "entity/sheep/sheep_wool_undercoat.png",
    "entity/chicken/chicken_temperate.png", "entity/chicken/chicken_cold.png", "entity/chicken/chicken_warm.png",
    "entity/zombie/zombie.png", "entity/skeleton/skeleton.png", "entity/creeper/creeper.png",
    "entity/spider/spider.png", "entity/spider/spider_eyes.png",
    # Survival T2 19c (bow): the arrow entity's 32 x 32 texture (plain arrows only).
    "entity/projectiles/arrow.png",
]
SERVER_SHA1 = "cc3964451a3b32a0488110d5e3e6ad8d75fe6a66"  # server-26.3.jar of the probe; recorded in facts/mob_attributes.json
PREFIX = "assets/minecraft/textures/"
LOOT_PREFIX = "data/minecraft/loot_table/"
# Jar entries under data/minecraft/loot_table/, copied to the same path here. Append only.
LOOT = [
    # Survival step 20a (mobs): the eight first mobs and the 16 sheep colours.
    *(f"entities/{mob}.json" for mob in ("cow", "pig", "sheep", "chicken", "zombie", "skeleton", "creeper", "spider")),
    *(f"entities/sheep/{colour}.json" for colour in (
        "white", "orange", "magenta", "light_blue", "yellow", "lime", "pink", "gray", "light_gray", "cyan", "purple",
        "blue", "brown", "green", "red", "black")),
    # Survival T6 20e1 (shearing): the shears' sheep table and its 16 colour tables.
    "shearing/sheep.json",
    *(f"shearing/sheep/{colour}.json" for colour in (
        "white", "orange", "magenta", "light_blue", "yellow", "lime", "pink", "gray", "light_gray", "cyan", "purple",
        "blue", "brown", "green", "red", "black")),
]
RECIPE_PREFIX = "data/minecraft/recipe/"
# Jar entries under data/minecraft/recipe/, copied to the same path here. Append only.
RECIPES = [
    # Survival step 20a (mobs): the furnace recipes of the meats the mobs drop, the results of their tables'
    # furnace_smelt (ImportMobs.py).
    *(f"cooked_{meat}.json" for meat in ("beef", "porkchop", "mutton", "chicken")),
]
GENERATED = ["textures.json"]
FACTS = ["facts/entity_models.json", "facts/mob_attributes.json"]  # produced by their tools, only hashed here
TOOLS = ["tools/build_reference.py", "tools/verify_reference.py", "tools/EntityModelFacts.java", "tools/mob_probe.py"]


def sha1(data):
    return hashlib.sha1(data).hexdigest()


def sha256(data):
    return hashlib.sha256(data).hexdigest()


def png_size(data):
    """Width and height from a PNG's IHDR (no decoder needed)."""
    if data[:8] != b"\x89PNG\r\n\x1a\n" or data[12:16] != b"IHDR":
        raise SystemExit("not a PNG")
    return int.from_bytes(data[16:20], "big"), int.from_bytes(data[20:24], "big")


def derive(read):
    """textures.json's bytes; `read(name)` returns a TEXTURES entry's bytes."""
    entries = []
    for name in TEXTURES:
        data = read(name)
        width, height = png_size(data)
        entries.append({"name": name, "sha1": sha1(data), "size": len(data), "width": width, "height": height})
    return (json.dumps({"source": {"client_jar_sha1": CLIENT_SHA1}, "textures": entries}, indent=1) + "\n").encode()


def write_sums():
    files = sorted(p.relative_to(ROOT).as_posix() for p in ROOT.rglob("*")
                   if p.is_file() and p.name != "SHA256SUMS.txt" and "__pycache__" not in p.parts)
    (ROOT / "SHA256SUMS.txt").write_text(
        "".join(f"{sha256((ROOT / f).read_bytes())}  {f}\n" for f in files), encoding="utf-8", newline="\n")
    print(f"SHA256SUMS.txt: {len(files)} files")


def main():
    facts = Path(sys.argv[sys.argv.index("--facts") + 1]) if "--facts" in sys.argv else None
    extra = [p.name for p in ROOT.iterdir() if p.name not in ("tools", "README.md", "__pycache__")]
    if extra:
        raise SystemExit(f"refusing to overwrite an accepted reference ({extra}); move the folder away first")
    jar_bytes = CLIENT_JAR.read_bytes()
    if sha1(jar_bytes) != CLIENT_SHA1:
        raise SystemExit("the client jar differs from the pinned one")
    with zipfile.ZipFile(io.BytesIO(jar_bytes)) as jar:
        copies = {name: jar.read(PREFIX + name) for name in TEXTURES}
        loot = {name: jar.read(LOOT_PREFIX + name) for name in LOOT}
        recipes = {name: jar.read(RECIPE_PREFIX + name) for name in RECIPES}
    if facts is None or not all((facts / Path(f).name).is_file() for f in FACTS):
        raise SystemExit("facts missing: run tools/EntityModelFacts.java and tools/mob_probe.py, then pass --facts <dir>")
    for name, data in copies.items():
        path = ROOT / PREFIX / name
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_bytes(data)
    for name, data in loot.items():
        path = ROOT / LOOT_PREFIX / name
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_bytes(data)
    for name, data in recipes.items():
        path = ROOT / RECIPE_PREFIX / name
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_bytes(data)
    for name in FACTS:
        (ROOT / name).parent.mkdir(exist_ok=True)
        (ROOT / name).write_bytes((facts / Path(name).name).read_bytes())
    (ROOT / "textures.json").write_bytes(derive(lambda name: copies[name]))
    print(f"built: textures.json, {len(copies)} textures, {len(loot)} loot tables, {len(recipes)} recipes, "
          f"{len(FACTS)} facts")
    write_sums()


if __name__ == "__main__":
    main()
