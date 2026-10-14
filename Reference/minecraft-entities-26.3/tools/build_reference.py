"""Build Reference/minecraft-entities-26.3: entity textures copied from the pinned 26.3 client jar, as stand-ins (Jesus,
2026-10-14: "Use any needed texture from Java Minecraft as a stand in so we can replace later"; Docs/StandIns.md).

usage (from the repository root):
    uv run --no-project --python 3.13 --with pillow python -I Reference/minecraft-entities-26.3/tools/build_reference.py

Input (read-only): the 26.3 client jar (SHA-1 pinned below, the same pin as Reference/minecraft-items-26.3). TEXTURES
lists the jar entries copied, in order; a later step appends entries (mob textures, survival track T6) and never
reorders or removes one, so earlier entries and their files never move.

The folder must hold only tools/ and README.md, so an accepted reference is never overwritten; to rebuild, move the old
folder away first. textures.json comes from derive(), which verify_reference.py runs again from the copied files.
Data only: the game code is never read.
"""
import hashlib
import io
import json
import os
import zipfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
CLIENT_SHA1 = "e877b6a07acd633fb3bb475002175cec036e7b87"
CLIENT_JAR = Path(os.environ.get("APPDATA", "")) / "ModrinthApp/meta/versions/26.3/26.3.jar"
# Jar entries under assets/minecraft/textures/, copied to the same path here. Append only.
TEXTURES = [
    # Survival step 12 (XP orbs): a 64 x 64 sheet of 16 x 16 orb icons (value report `xp.value_icons`).
    "entity/experience/experience_orb.png",
]
PREFIX = "assets/minecraft/textures/"
GENERATED = ["textures.json"]
TOOLS = ["tools/build_reference.py", "tools/verify_reference.py"]


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
    extra = [p.name for p in ROOT.iterdir() if p.name not in ("tools", "README.md", "__pycache__")]
    if extra:
        raise SystemExit(f"refusing to overwrite an accepted reference ({extra}); move the folder away first")
    jar_bytes = CLIENT_JAR.read_bytes()
    if sha1(jar_bytes) != CLIENT_SHA1:
        raise SystemExit("the client jar differs from the pinned one")
    with zipfile.ZipFile(io.BytesIO(jar_bytes)) as jar:
        copies = {name: jar.read(PREFIX + name) for name in TEXTURES}
    for name, data in copies.items():
        path = ROOT / PREFIX / name
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_bytes(data)
    (ROOT / "textures.json").write_bytes(derive(lambda name: copies[name]))
    print(f"built: textures.json, {len(copies)} textures")
    write_sums()


if __name__ == "__main__":
    main()
