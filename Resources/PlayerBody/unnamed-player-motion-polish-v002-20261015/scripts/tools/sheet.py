"""Contact sheets for remote review (keeps image tokens low).

    uv run --with pillow python scripts/tools/sheet.py out.png --cols 3 --width 1200 [--labels a,b,c] img1 img2 ...
"""

import argparse
import os

from PIL import Image, ImageDraw, ImageFont

ap = argparse.ArgumentParser()
ap.add_argument("out")
ap.add_argument("images", nargs="+")
ap.add_argument("--cols", type=int, default=3)
ap.add_argument("--width", type=int, default=1200)
ap.add_argument("--labels", default="")
ap.add_argument("--title", default="")
a = ap.parse_args()

ims = [Image.open(p).convert("RGB") for p in a.images]
labels = a.labels.split("|") if a.labels else [os.path.splitext(os.path.basename(p))[0] for p in a.images]
cols = min(a.cols, len(ims))
rows = (len(ims) + cols - 1) // cols
cw = a.width // cols
ch = max(int(im.height * cw / im.width) for im in ims)
top = 28 if a.title else 0
sheet = Image.new("RGB", (cw * cols, ch * rows + top), (24, 24, 24))
draw = ImageDraw.Draw(sheet)
try:
    font = ImageFont.truetype("arial.ttf", 15)
    tfont = ImageFont.truetype("arialbd.ttf", 18)
except OSError:
    font = tfont = ImageFont.load_default()
if a.title:
    draw.text((8, 4), a.title, fill=(255, 255, 255), font=tfont)
for i, im in enumerate(ims):
    r, c = divmod(i, cols)
    t = im.resize((cw, int(im.height * cw / im.width)), Image.LANCZOS)
    sheet.paste(t, (c * cw, top + r * ch))
    lab = labels[i] if i < len(labels) else ""
    if lab:
        draw.rectangle([c * cw, top + r * ch, c * cw + 8 + 8 * len(lab), top + r * ch + 20], fill=(0, 0, 0))
        draw.text((c * cw + 4, top + r * ch + 2), lab, fill=(255, 230, 120), font=font)
sheet.save(a.out)
print(f"[sheet] {a.out} {sheet.size}")
