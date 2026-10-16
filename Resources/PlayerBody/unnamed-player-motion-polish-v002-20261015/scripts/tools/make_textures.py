"""Derive lossless skin textures from the original embedded JPEG (never overwritten).

    uv run --no-project --with pillow python scripts/tools/make_textures.py <work root>

The 4096 JPEG is a 64x upscale of a 64x64 skin with 8x8 DCT blocks aligned to skin pixels, so sampling texel centres
is exact (s00 audit: zero deviation inside every block). Also writes a 1024 grid image for preview floors.
"""
import json
import os
import sys

from PIL import Image, ImageDraw

root = os.path.abspath(sys.argv[1])
src = os.path.join(root, "source", "original_skin_texture.jpg")
im = Image.open(src).convert("RGB")
s = im.size[0] // 64
small = Image.new("RGB", (64, 64))
dev = 0
for y in range(64):
    for x in range(64):
        c = im.getpixel((x * s + s // 2, y * s + s // 2))
        small.putpixel((x, y), c)
        for (px, py) in ((x * s + 4, y * s + 4), (x * s + s - 5, y * s + s - 5)):
            dev = max(dev, max(abs(a - b) for a, b in zip(im.getpixel((px, py)), c)))
os.makedirs(os.path.join(root, "textures"), exist_ok=True)
small.save(os.path.join(root, "textures", "T_UP_Skin_64.png"))
small.resize((1024, 1024), Image.NEAREST).save(os.path.join(root, "textures", "T_UP_Skin_1024.png"))

# preview floor tile: 1 m tile, 256 px, major lines every 1 m, minor every 0.25 m
tile = Image.new("RGB", (256, 256), (196, 199, 204))
d = ImageDraw.Draw(tile)
for i in range(0, 256, 64):
    d.line([(i, 0), (i, 255)], fill=(170, 173, 178), width=1)
    d.line([(0, i), (255, i)], fill=(170, 173, 178), width=1)
d.rectangle([0, 0, 255, 255], outline=(70, 72, 78), width=3)
tile.save(os.path.join(root, "textures", "T_Preview_Grid_1m.png"))

# stone target: 16x16 pixel noise in Minecraft-like greys (generated; not a Mojang asset)
import random
random.seed(7)
stone = Image.new("RGB", (16, 16))
for y in range(16):
    for x in range(16):
        g = random.choice([112, 118, 125, 125, 131, 131, 138, 104])
        stone.putpixel((x, y), (g, g, g + 2))
stone.resize((256, 256), Image.NEAREST).save(os.path.join(root, "textures", "T_Preview_Stone.png"))
json.dump({"source": src, "texel": s, "max_deviation_at_block_corners": dev,
           "outputs": ["T_UP_Skin_64.png", "T_UP_Skin_1024.png", "T_Preview_Grid_1m.png", "T_Preview_Stone.png"]},
          open(os.path.join(root, "textures", "textures_record.json"), "w"), indent=1)
print("[tex] done; max deviation at block corners", dev)
