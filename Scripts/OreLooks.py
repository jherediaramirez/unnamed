"""Generate the ore look textures: scout E's look calls L1 (ores wear their host rock) and L2 level 1 (ore-aware mips
and glint); see Docs/VoxelWorld.md, "Ore looks".

Reads only the 26.3 block reference (stone, deepslate, granite, diorite, andesite, tuff and the 16 stone and deepslate
ores) and writes Art/OreLooks/T_OreLooks.png and Art/OreLooks/manifest.json, which Scripts/Editor/ImportOreLooks.py
turns into /Game/Blocks/T_OreLooks. A rerun writes byte-identical files; --check only compares. The manifest records
this file's hash and the import refuses a stale one, so any edit here, even to a comment, needs a rerun.

    uv run --no-project --python 3.13 --with pillow python -I Scripts/OreLooks.py [--check] [--sheet <png>]

- Ore pixels: an ore texture's pixels whose colour is not in its base rock's palette (stone's 4 greys, deepslate's 5).
  The vanilla art redraws the rock around each speck, so "ore minus stone" is not a clean mask; this is.
- Slots (VoxelOreLooks::GetSlot): (mode * 5 + host) * 16 + ore * 2 + base. Ores in ORES order; base 0 stone, 1
  deepslate; host 0 is the ore's own base (the vanilla texture in every mode), then granite, diorite, andesite, tuff.
  Modes: 0 composite (the ore pixels over the host rock's own texture), 1 palette swap (the base rock's tones mapped by
  brightness onto the host's, the vanilla drawing kept), each with 26.3's mips made at import from mip 0; 2 and 3 the
  same images with ore-aware mips (below) and the gloss class in alpha.
- Ore-aware mips: at each level the ore pixels keep their share of the texture (at least one pixel down to 2 x 2) and
  their own colour. The cells whose footprint holds the most ore pixels become ore, coloured with the mean of those ore
  pixels; the rest take the mean of their footprint's rock pixels. Means are in linear light; 1 x 1 is the plain mean.
- Alpha of the ore-aware slots: 255 matte (rock and coal pixels), 170 metal (iron, copper, gold), 85 gem (redstone,
  emerald, lapis, diamond). The chunk material turns it into roughness and specular. The other slots are opaque.
- The strip: 16 pixels wide, 5 frames of 16 x 16 per slot (frame slot * 5 + level); level L is (16 >> L) pixels square
  at its frame's top-left, the rest transparent. Slots with 26.3's mips fill only their level 0.
"""
import argparse
import hashlib
import io
import json
import sys
from pathlib import Path

from PIL import Image

ROOT = Path(__file__).resolve().parent.parent
BLOCKS = ROOT / "Reference/minecraft-blocks-26.3/assets/minecraft/textures/block"
OUT = ROOT / "Art/OreLooks"
ORES = ["coal", "iron", "copper", "gold", "redstone", "emerald", "lapis", "diamond"]
BASES = ["stone", "deepslate"]
HOSTS = ["granite", "diorite", "andesite", "tuff"]
MODES = ["composite", "swap", "composite_ore_mips", "swap_ore_mips"]
GLOSS = {"coal": 255, "iron": 170, "copper": 170, "gold": 170,
         "redstone": 85, "emerald": 85, "lapis": 85, "diamond": 85}
SIZE, LEVELS = 16, 5


def fail(message):
    raise SystemExit("ORE_LOOKS FAIL: " + message)


def load(name):
    path = BLOCKS / (name + ".png")
    image = Image.open(path).convert("RGBA")
    if image.size != (SIZE, SIZE):
        fail("%s is %s, not 16 x 16" % (path.name, image.size))
    pixels = list(getattr(image, "get_flattened_data", image.getdata)())  # getdata before Pillow 12
    if any(p[3] != 255 for p in pixels):
        fail(path.name + " has transparent pixels")
    return [p[:3] for p in pixels], hashlib.sha256(path.read_bytes()).hexdigest()


def ore_name(ore, base):
    return ("deepslate_" if base else "") + ore + "_ore"


def slot_of(mode, host, ore, base):
    return (mode * (len(HOSTS) + 1) + host) * 16 + ore * 2 + base


def luminance(c):
    return 0.2126 * c[0] + 0.7152 * c[1] + 0.0722 * c[2]


def to_linear(v):
    v /= 255.0
    return v / 12.92 if v <= 0.04045 else ((v + 0.055) / 1.055) ** 2.4


def to_srgb(v):
    v = 12.92 * v if v <= 0.0031308 else 1.055 * v ** (1 / 2.4) - 0.055
    return max(0, min(255, int(v * 255 + 0.5)))


def mean(colours):
    if len(colours) == 1:
        return colours[0]
    return tuple(to_srgb(sum(to_linear(c[i]) for c in colours) / len(colours)) for i in range(3))


def swap_tones(base_palette, host_pixels):
    """Base colour -> host tone: the base palette by brightness onto the host's pixels at the same brightness
    quantiles (pixel-weighted, so the rock's common tones win)."""
    ranked = sorted(base_palette, key=lambda c: (luminance(c), c))
    host = sorted(host_pixels, key=lambda c: (luminance(c), c))
    return {c: host[int((i + 0.5) * len(host) / len(ranked))] for i, c in enumerate(ranked)}


def ore_mips(image, mask, gloss):
    """Levels 0..4 of one ore-aware slot as RGBA pixel lists (level L is (16 >> L)^2 pixels)."""
    total = sum(mask)
    levels = []
    for level in range(LEVELS):
        size, step = SIZE >> level, 1 << level
        cells = [(y, x) for y in range(size) for x in range(size)]

        def footprint(cell):
            return [(cell[0] * step + dy) * SIZE + cell[1] * step + dx for dy in range(step) for dx in range(step)]

        cover = {c: sum(mask[i] for i in footprint(c)) for c in cells}
        count = round(total * size * size / (SIZE * SIZE)) if size > 1 else 0
        if size > 1 and total and count == 0:
            count = 1
        chosen = set(sorted((c for c in cells if cover[c]), key=lambda c: (-cover[c], c))[:count])
        out = []
        for c in cells:
            indices = footprint(c)
            if c in chosen:
                out.append(mean([image[i] for i in indices if mask[i]]) + (gloss,))
            else:
                rock = [image[i] for i in indices if not mask[i]] if size > 1 else []
                out.append(mean(rock or [image[i] for i in indices]) + (255,))
        levels.append(out)
    return levels


def build():
    inputs = {}
    textures = {}
    for name in BASES + HOSTS + [ore_name(o, b) for b in range(2) for o in ORES]:
        textures[name], inputs[name + ".png"] = load(name)
    slots, frames, shares = [], {}, {}
    for base, base_name in enumerate(BASES):
        palette = set(textures[base_name])
        for ore, ore_id in enumerate(ORES):
            name = ore_name(ore_id, base)
            vanilla = textures[name]
            mask = [0 if p in palette else 1 for p in vanilla]
            shares[name] = sum(mask)
            images = {0: vanilla}
            for host, host_name in enumerate(HOSTS, start=1):
                rock = textures[host_name]
                tones = swap_tones(palette, rock)
                images[(0, host)] = [p if m else r for p, m, r in zip(vanilla, mask, rock)]
                images[(1, host)] = [p if m else tones[p] for p, m in zip(vanilla, mask)]
            for mode in range(len(MODES)):
                for host in range(len(HOSTS) + 1):
                    image = vanilla if host == 0 else images[(mode % 2, host)]
                    slot = slot_of(mode, host, ore, base)
                    if mode < 2:
                        levels = [[p + (255,) for p in image]]
                    else:
                        levels = ore_mips(image, mask, GLOSS[ore_id])
                        if [p[:3] for p in levels[0]] != image:
                            fail("ore-aware level 0 differs from its image: slot %d" % slot)
                    frames[slot] = levels
                    rgba = bytes(v for p in levels[0] for v in p)
                    slots.append({"slot": slot, "mode": MODES[mode], "ore": ore_id, "base": base_name,
                                  "host": HOSTS[host - 1] if host else base_name, "texture": name,
                                  "mips": "explicit" if mode >= 2 else "vanilla",
                                  "rgba_sha256": hashlib.sha256(rgba).hexdigest()})
            if images[(0, 1)] == vanilla:
                fail(name + ": the granite composite equals the vanilla texture")
    slots.sort(key=lambda s: s["slot"])
    if [s["slot"] for s in slots] != list(range(len(slots))):
        fail("slots are not contiguous")
    strip = Image.new("RGBA", (SIZE, SIZE * LEVELS * len(slots)), (0, 0, 0, 0))
    for slot, levels in frames.items():
        for level, pixels in enumerate(levels):
            size = SIZE >> level
            tile = Image.new("RGBA", (size, size))
            tile.putdata(pixels)
            strip.paste(tile, (0, (slot * LEVELS + level) * SIZE))
    buffer = io.BytesIO()
    strip.save(buffer, format="PNG", optimize=False, compress_level=9)
    png = buffer.getvalue()
    manifest = {
        "generator": "Scripts/OreLooks.py",
        "generator_sha256": hashlib.sha256(Path(__file__).read_bytes()).hexdigest(),
        "inputs_sha256": inputs,
        "frame_size": SIZE, "mip_count": LEVELS, "frames_per_slot": LEVELS,
        "ores": ORES, "bases": BASES, "hosts": HOSTS, "modes": MODES, "gloss_alpha": GLOSS,
        "ore_pixels_of_256": shares,
        "strip": "T_OreLooks.png", "strip_sha256": hashlib.sha256(png).hexdigest(),
        "slots": slots,
    }
    text = json.dumps(manifest, indent=1) + "\n"
    return png, text.encode("utf-8"), frames, shares


def sheet(path, frames):
    """Contact sheet, 8x: per ore and base a row of vanilla, the four composites, the four swaps, then the vanilla
    texture's ore-aware levels 1 to 4 (each scaled to a full tile)."""
    scale, tile = 8, SIZE * 8
    rows = [(o, b) for b in range(2) for o in range(len(ORES))]
    image = Image.new("RGBA", (tile * 13, tile * len(rows)), (40, 40, 40, 255))
    for row, (ore, base) in enumerate(rows):
        columns = [frames[slot_of(0, 0, ore, base)][0]]
        columns += [frames[slot_of(0, h, ore, base)][0] for h in range(1, 5)]
        columns += [frames[slot_of(1, h, ore, base)][0] for h in range(1, 5)]
        columns += frames[slot_of(2, 0, ore, base)][1:]
        for column, pixels in enumerate(columns):
            size = int(len(pixels) ** 0.5)
            small = Image.new("RGBA", (size, size))
            small.putdata([p[:3] + (255,) for p in pixels])
            image.paste(small.resize((tile, tile), Image.NEAREST), (column * tile, row * tile))
    image.save(path)


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--check", action="store_true", help="compare with the files on disk; write nothing")
    parser.add_argument("--sheet", help="also write a contact sheet PNG here")
    args = parser.parse_args()
    png, manifest, frames, shares = build()
    targets = {OUT / "T_OreLooks.png": png, OUT / "manifest.json": manifest}
    if args.check:
        stale = [p.name for p, data in targets.items() if not p.exists() or p.read_bytes() != data]
        if stale:
            fail("stale: " + ", ".join(stale) + " (rerun without --check)")
    else:
        OUT.mkdir(parents=True, exist_ok=True)
        for path, data in targets.items():
            path.write_bytes(data)
    if args.sheet:
        sheet(args.sheet, frames)
    for name, count in shares.items():
        print("  %-24s ore pixels %3d (%4.1f%%)" % (name, count, 100 * count / 256))
    print("ORE_LOOKS PASS: %d slots, strip %s%s" % (len(frames), hashlib.sha256(png).hexdigest()[:16],
                                                    " (checked)" if args.check else ""))


if __name__ == "__main__":
    sys.exit(main())
