"""The break import's data, derived from Reference/minecraft-blocks-26.3 with the standard library only (breaking
effects: crack strip, sound tables, particle textures; Docs/Breaking.md).

Importable from UE's Python (ImportBreakAssets.py) and runnable standalone, which prints a summary and runs the
self-checks (uv run --no-project --python 3.13 python -I Scripts/Editor/BreakImportData.py):

    --write-strip <path>   write the crack strip PNG (16 x 320, 20 frames) and print its layer hashes
    --decode-wav <outdir>  decode every needed .ogg to 16-bit PCM WAV (needs soundfile; the fallback sound route:
                           uv run --no-project --python 3.13 --with soundfile python -I <this file> --decode-wav <dir>)

The strip: frames 0-9 are the carved stages, frames 10-19 vanilla's own (plain) stages. Both are grey in R, G and B.
"""
import argparse
import csv
import hashlib
import json
import struct
import sys
import zlib
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
REF = ROOT / "Reference/minecraft-blocks-26.3"
ASSETS = REF / "assets/minecraft"

STAGES = 10
SIZE = 16
# Mirrors FoundationBreakEffects.h (CrackGrey, LitGrey, ShadowGrey, NeutralGrey).
NEUTRAL, CRACK, LIT, SHADOW = 128, 60, 170, 90
# Vanilla's cumulative crack texel counts per destroy stage (self-check).
EXPECTED_CRACK_TEXELS = [4, 10, 18, 28, 41, 55, 70, 100, 129, 145]
# block_particles.json lists shelf_mushroom_particle as outside textures.json; the block texture array has no layer
# for it, so the particle falls back to the mushroom's first texture.
OUTSIDE_FALLBACK = "shelf_mushroom_stage0"
# Tint flags every run must reproduce (block id -> default state's particle is on a tinted face).
EXPECTED_TINT = {"grass_block": False, "oak_leaves": True, "short_grass": True, "stone": False, "dandelion": False}

_reads = {}  # relative path -> sha256 of every input file read, for the source digest


def _read(path):
    data = Path(path).read_bytes()
    resolved = Path(path).resolve()
    if resolved.is_relative_to(REF):  # inputs only; a PNG read from elsewhere (a test) is not part of the digest
        _reads[resolved.relative_to(ROOT).as_posix()] = hashlib.sha256(data).hexdigest()
    return data


def _json(path):
    return json.loads(_read(path).decode("utf-8"))


# ---------------------------------------------------------------------------------------------------- PNG decoding


def decode_png(path):
    """(width, height, RGBA bytes) of a non-interlaced PNG of any colour type and bit depth."""
    data = _read(path)
    if data[:8] != b"\x89PNG\r\n\x1a\n":
        raise ValueError("%s is not a PNG" % path)
    pos, header, palette, trns, idat = 8, None, None, None, []
    while pos < len(data):
        length, kind = struct.unpack(">I4s", data[pos:pos + 8])
        body = data[pos + 8:pos + 8 + length]
        pos += 12 + length
        if kind == b"IHDR":
            header = struct.unpack(">IIBBBBB", body)
        elif kind == b"PLTE":
            palette = body
        elif kind == b"tRNS":
            trns = body
        elif kind == b"IDAT":
            idat.append(body)
        elif kind == b"IEND":
            break
    width, height, depth, ctype, _, _, interlace = header
    if interlace:
        raise ValueError("%s is interlaced" % path)
    channels = {0: 1, 2: 3, 3: 1, 4: 2, 6: 4}[ctype]
    bits = channels * depth
    stride = (width * bits + 7) // 8
    bpp = max(1, bits // 8)
    raw = zlib.decompress(b"".join(idat))
    if len(raw) != height * (stride + 1):
        raise ValueError("%s has the wrong data length" % path)
    rows, previous = [], bytearray(stride)
    for y in range(height):
        base = y * (stride + 1)
        kind, line = raw[base], bytearray(raw[base + 1:base + 1 + stride])
        for i in range(stride):
            a = line[i - bpp] if i >= bpp else 0
            b = previous[i]
            c = previous[i - bpp] if i >= bpp else 0
            if kind == 0:
                add = 0
            elif kind == 1:
                add = a
            elif kind == 2:
                add = b
            elif kind == 3:
                add = (a + b) >> 1
            elif kind == 4:
                p = a + b - c
                pa, pb, pc = abs(p - a), abs(p - b), abs(p - c)
                add = a if pa <= pb and pa <= pc else (b if pb <= pc else c)
            else:
                raise ValueError("%s: bad PNG filter %d" % (path, kind))
            line[i] = (line[i] + add) & 255
        rows.append(line)
        previous = line

    def samples(line):
        """The row's samples as ints of `depth` bits."""
        if depth == 8:
            return list(line)
        if depth == 16:
            return [(line[i] << 8) | line[i + 1] for i in range(0, len(line), 2)]
        mask, out = (1 << depth) - 1, []
        for byte in line:
            for shift in range(8 - depth, -1, -depth):
                out.append((byte >> shift) & mask)
        return out

    scale = 255 / ((1 << depth) - 1)

    def to8(value):
        return value if depth == 8 else (value >> 8 if depth == 16 else round(value * scale))

    key = None
    if trns and ctype == 0:
        key = (struct.unpack(">H", trns[:2])[0],)
    elif trns and ctype == 2:
        key = struct.unpack(">HHH", trns[:6])
    out = bytearray()
    for line in rows:
        s = samples(line)
        for x in range(width):
            if ctype == 0:
                g = to8(s[x])
                a = 0 if key and s[x] == key[0] else 255
                out += bytes((g, g, g, a))
            elif ctype == 2:
                v = s[x * 3:x * 3 + 3]
                a = 0 if key and tuple(v) == key else 255
                out += bytes((to8(v[0]), to8(v[1]), to8(v[2]), a))
            elif ctype == 3:
                i = s[x]
                a = trns[i] if trns and i < len(trns) else 255
                out += bytes((palette[i * 3], palette[i * 3 + 1], palette[i * 3 + 2], a))
            elif ctype == 4:
                g = to8(s[x * 2])
                out += bytes((g, g, g, to8(s[x * 2 + 1])))
            else:
                out += bytes(to8(v) for v in s[x * 4:x * 4 + 4])
    return width, height, bytes(out)


def _png_chunk(kind, body):
    return struct.pack(">I", len(body)) + kind + body + struct.pack(">I", zlib.crc32(kind + body) & 0xFFFFFFFF)


def encode_png_rgba(width, height, rgba):
    rows = b"".join(b"\x00" + rgba[y * width * 4:(y + 1) * width * 4] for y in range(height))
    return (b"\x89PNG\r\n\x1a\n" + _png_chunk(b"IHDR", struct.pack(">IIBBBBB", width, height, 8, 6, 0, 0, 0)) +
            _png_chunk(b"IDAT", zlib.compress(rows, 9)) + _png_chunk(b"IEND", b""))


# ---------------------------------------------------------------------------------------------------- crack strip


def load_stages():
    """Per destroy stage: (crack mask as 256 bools, grey of each texel, crack texel count)."""
    stages = []
    for n in range(STAGES):
        width, height, rgba = decode_png(ASSETS / ("textures/block/destroy_stage_%d.png" % n))
        if (width, height) != (SIZE, SIZE):
            raise ValueError("destroy_stage_%d is %dx%d, not 16x16" % (n, width, height))
        mask = [rgba[i * 4 + 3] > 127 for i in range(SIZE * SIZE)]
        grey = []
        for i, crack in enumerate(mask):
            r, g, b = rgba[i * 4:i * 4 + 3]
            if crack and not (r == g == b):
                raise ValueError("destroy_stage_%d crack texel %d is not grey: %s" % (n, i, (r, g, b)))
            grey.append(r)
        stages.append((mask, grey, sum(mask)))
    return stages


def carved(mask):
    """CARVED rule: crack 60, lit side (right of or below a crack) 170, shadow side (left of or above) 90 (wins), else 128."""
    def crack(x, y):
        return 0 <= x < SIZE and 0 <= y < SIZE and mask[y * SIZE + x]
    out = bytearray()
    for y in range(SIZE):
        for x in range(SIZE):
            if crack(x, y):
                out.append(CRACK)
            elif crack(x - 1, y) or crack(x, y - 1):
                shadow = crack(x + 1, y) or crack(x, y + 1)
                out.append(SHADOW if shadow else LIT)
            elif crack(x + 1, y) or crack(x, y + 1):
                out.append(SHADOW)
            else:
                out.append(NEUTRAL)
    return bytes(out)


def plain(mask, grey):
    """PLAIN rule: a crack texel keeps its own grey, everything else 128."""
    return bytes(grey[i] if mask[i] else NEUTRAL for i in range(SIZE * SIZE))


def strip_frames(stages=None):
    """20 frames of 256 greys: carved stages 0-9, then plain stages 0-9."""
    stages = stages or load_stages()
    return [carved(m) for m, _, _ in stages] + [plain(m, g) for m, g, _ in stages]


def frame_rgba(frame):
    return b"".join(bytes((g, g, g, 255)) for g in frame)


def write_strip_png(path, frames=None):
    """Writes the 16 x 320 strip; returns the expected FoundationBlockImport::HashLayer value of each layer.

    HashLayer is the lowercase hex SHA-256 of the layer's mip-0 RGBA bytes (R, G, B, A per texel, row-major)."""
    frames = frames or strip_frames()
    rgba = [frame_rgba(f) for f in frames]
    Path(path).parent.mkdir(parents=True, exist_ok=True)
    Path(path).write_bytes(encode_png_rgba(SIZE, SIZE * len(frames), b"".join(rgba)))
    return [hashlib.sha256(r).hexdigest() for r in rgba]


# ---------------------------------------------------------------------------------------------------- block tables


def _state_tinted(state, texture, models):
    """True when a tinted face (tint index >= 0) of the state's model uses the texture."""
    if state["shape"] == "model":
        return any(q["t"] == texture and q["tint"] >= 0
                   for part in state["model"] for alternative in part for q in models[alternative]["quads"])
    for face in state.get("faces", []):
        if face["t"] == texture and face["tint"] >= 0:
            return True
        overlay = face.get("overlay")
        if overlay and overlay["t"] == texture and overlay["tint"] >= 0:
            return True
    return False


def _sound_events():
    """sounds.json events as {name: [files]}, parsed lazily by event."""
    return _json(ASSETS / "sounds.json")


def build_tables():
    manifest = _json(REF / "block_manifest.json")
    states = _json(REF / "block_states.json")
    models = _json(REF / "block_models.json")["models"]
    particles = _json(REF / "block_particles.json")
    textures = {t["name"] for t in _json(REF / "textures.json")["textures"]}
    sounds = _sound_events()
    rows = {r["id"]: r for r in csv.DictReader(_read(REF / "facts/block_sounds.csv").decode("utf-8").splitlines())}

    entries = manifest["blocks"] + manifest["fluids"]  # DA_Blocks' order: the blocks, then water and lava
    ids = [b["id"] for b in entries]
    if ids != [b["id"] for b in states["blocks"]] or ids != list(particles["blocks"]) or set(ids) != set(rows):
        raise ValueError("manifest, block_states, block_particles and block_sounds disagree on the block list")
    outside = set(particles["textures_outside_textures_json"])
    if outside != {"shelf_mushroom_particle"}:
        raise ValueError("unexpected textures outside textures.json: %s" % sorted(outside))
    if OUTSIDE_FALLBACK not in textures:
        raise ValueError(OUTSIDE_FALLBACK + " is not in textures.json")

    def resolve(name):
        name = OUTSIDE_FALLBACK if name in outside else name
        if name not in textures:
            raise ValueError("particle texture %s is not in textures.json" % name)
        return name

    events, event_index, forms = [], {}, set()

    def event(name):
        if name == "intentionally_empty":
            return -1
        if name not in event_index:
            if name not in sounds:
                raise ValueError("sound event %s is not in sounds.json" % name)
            files = []
            for entry in sounds[name]["sounds"]:
                if isinstance(entry, str):
                    forms.add("str")
                    files.append({"name": entry, "volume": 1.0, "pitch": 1.0})
                elif isinstance(entry, dict) and set(entry) <= {"name", "volume", "pitch"} and "name" in entry:
                    forms.add("object:" + "+".join(sorted(entry)))
                    files.append({"name": entry["name"], "volume": float(entry.get("volume", 1.0)),
                                  "pitch": float(entry.get("pitch", 1.0))})
                else:
                    raise ValueError("sound event %s has an unsupported entry form: %r" % (name, entry))
            event_index[name] = len(events)
            events.append({"name": name, "files": files})
        return event_index[name]

    blocks, tinted_blocks, silent = [], 0, 0
    by_id = {b["id"]: b for b in states["blocks"]}
    for entry in entries:
        block_id = entry["id"]
        row = rows[block_id]
        record = {"id": block_id, "volume": float(row["volume"]), "pitch": float(row["pitch"]),
                  "break_event": event(row["break"]), "hit_event": event(row["hit"])}
        silent += record["break_event"] < 0 and record["hit_event"] < 0
        particle = particles["blocks"][block_id]
        by_state = {s["id"]: s for s in by_id[block_id]["states"]}
        fluid = block_id in ("water", "lava")

        def tinted(state_id, texture):
            if fluid:  # fluid states have no faces; the manifest says whether the fluid is tinted
                return bool(entry["tinted"])
            return _state_tinted(by_state[state_id], texture, models)

        record["particle"] = resolve(particle["default"])
        record["particle_tinted"] = tinted(entry["default_state_id"], particle["default"])
        record["override_states"], record["override_particles"], record["override_tinted"] = [], [], []
        for state_id, texture in sorted(particle.get("states", {}).items(), key=lambda kv: int(kv[0])):
            if int(state_id) not in by_state:
                raise ValueError("%s has no state %s" % (block_id, state_id))
            record["override_states"].append(int(state_id))
            record["override_particles"].append(resolve(texture))
            record["override_tinted"].append(tinted(int(state_id), texture))
        tinted_blocks += record["particle_tinted"]
        blocks.append(record)

    disk = sorted(p.relative_to(ASSETS / "sounds").with_suffix("").as_posix()
                  for p in (ASSETS / "sounds").rglob("*.ogg"))
    # Place sounds: a place plays the block's place event, at the break's volume and pitch (minecraft.wiki's Sounds
    # tables, e.g. Stone: block.stone.place 1.0 / 0.8 as block.stone.break). The reference carries only the files of
    # the break and hit events, so a place event with a file it lacks plays the block's break event instead (66 blocks
    # in 26.3, such as deepslate and amethyst; Docs/KnownDifferences.md). Indexed after the break and hit events so
    # their indices stay as they were.
    on_disk = set(disk)
    place_fallbacks = []
    for record in blocks:
        name = rows[record["id"]]["place"]
        names = [] if name == "intentionally_empty" else [
            entry if isinstance(entry, str) else entry.get("name") for entry in sounds[name]["sounds"]]
        if names and all(n in on_disk for n in names):
            record["place_event"] = event(name)
        else:
            record["place_event"] = record["break_event"]
            if names:
                place_fallbacks.append(record["id"])
    needed = sorted({f["name"] for e in events for f in e["files"]})
    if needed != disk:
        raise ValueError("needed sounds and .ogg files on disk differ: %d only needed %s, %d only on disk %s" % (
            len(set(needed) - set(disk)), sorted(set(needed) - set(disk))[:5],
            len(set(disk) - set(needed)), sorted(set(disk) - set(needed))[:5]))
    total = 0
    for name in needed:
        total += len(_read(ASSETS / "sounds" / (name + ".ogg")))
    return {"events": events, "blocks": blocks, "sound_files": needed, "sound_bytes": total, "forms": sorted(forms),
            "silent_blocks": silent, "tinted_blocks": tinted_blocks, "place_fallbacks": place_fallbacks}


def source_digest():
    """SHA-256 over (relative path, file sha256) of every input read, sorted by path, plus this script's own hash."""
    own = Path(__file__).resolve()
    digest = hashlib.sha256()
    entries = dict(_reads)
    entries[own.relative_to(ROOT).as_posix()] = hashlib.sha256(own.read_bytes()).hexdigest()
    for path in sorted(entries):
        digest.update(("%s\0%s\n" % (path, entries[path])).encode("utf-8"))
    return digest.hexdigest()


def load():
    """Everything the import needs: tables, strip frames, expected layer hashes and the source digest."""
    stages = load_stages()
    tables = build_tables()
    frames = strip_frames(stages)
    tables["frames"] = frames
    tables["layer_hashes"] = [hashlib.sha256(frame_rgba(f)).hexdigest() for f in frames]
    tables["crack_texels"] = [n for _, _, n in stages]
    tables["plain_greys"] = sorted({g for f in frames[STAGES:] for g in f})
    tables["digest"] = source_digest()
    return tables


# ---------------------------------------------------------------------------------------------------- standalone


def decode_wav(tables, outdir):
    import soundfile  # only this fallback route needs it
    for name in tables["sound_files"]:
        target = Path(outdir) / (name + ".wav")
        target.parent.mkdir(parents=True, exist_ok=True)
        data, rate = soundfile.read(str(ASSETS / "sounds" / (name + ".ogg")), dtype="int16")
        soundfile.write(str(target), data, rate, subtype="PCM_16")
    print("decoded %d files into %s" % (len(tables["sound_files"]), outdir))


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--write-strip")
    parser.add_argument("--decode-wav")
    args = parser.parse_args()
    t = load()
    problems = []
    if t["crack_texels"] != EXPECTED_CRACK_TEXELS:
        problems.append("crack texel counts %s" % t["crack_texels"])
    if t["plain_greys"] != [61, 128, 155]:
        problems.append("plain greys %s" % t["plain_greys"])
    by_id = {b["id"]: b for b in t["blocks"]}
    for block_id, expected in EXPECTED_TINT.items():
        if by_id[block_id]["particle_tinted"] != expected:
            problems.append("%s tint %s, expected %s" % (block_id, by_id[block_id]["particle_tinted"], expected))
    overrides = sum(len(b["override_states"]) for b in t["blocks"])
    print("blocks %d, events %d, files %d, bytes %d, silent blocks %d, override states %d, tinted %d" % (
        len(t["blocks"]), len(t["events"]), len(t["sound_files"]), t["sound_bytes"], t["silent_blocks"], overrides,
        t["tinted_blocks"]))
    print("entry forms used: " + ", ".join(t["forms"]))
    print("place events: %d blocks play their own, %d play their break event (files not in the reference): %s" % (
        sum(b["place_event"] >= 0 for b in t["blocks"]) - len(t["place_fallbacks"]), len(t["place_fallbacks"]),
        ", ".join(t["place_fallbacks"])))
    print("crack texels per stage: %s; plain greys: %s" % (t["crack_texels"], t["plain_greys"]))
    print("tint flags: " + ", ".join("%s=%s" % (k, by_id[k]["particle_tinted"]) for k in EXPECTED_TINT))
    print("digest " + t["digest"])
    if args.write_strip:
        hashes = write_strip_png(args.write_strip, t["frames"])
        print("wrote %s (%d layers; layer 0 %s)" % (args.write_strip, len(hashes), hashes[0]))
    if args.decode_wav:
        decode_wav(t, args.decode_wav)
    if problems:
        print("SELF-CHECK FAIL: " + "; ".join(problems))
        sys.exit(1)
    print("SELF-CHECK PASS")


if __name__ == "__main__":
    main()
