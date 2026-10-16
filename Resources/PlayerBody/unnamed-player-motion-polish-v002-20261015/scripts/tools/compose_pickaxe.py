"""Side-by-side A-A-A-A vs A-B-A-C (identical timing/camera) as GIFs + a key-frame comparison sheet; layered sheet.

    uv run --no-project --with pillow python scripts/tools/compose_pickaxe.py <work root>
"""
import os
import sys

from PIL import Image, ImageDraw, ImageFont

root = os.path.abspath(sys.argv[1])
tmp = os.path.join(root, "previews", "_tmp", "pick")
out = os.path.join(root, "previews")
try:
    font = ImageFont.truetype("arial.ttf", 18)
    small = ImageFont.truetype("arial.ttf", 14)
except OSError:
    font = small = ImageFont.load_default()


def label(im, text, sub=""):
    d = ImageDraw.Draw(im)
    d.rectangle([0, 0, im.width, 26], fill=(0, 0, 0))
    d.text((6, 3), text, fill=(255, 230, 120), font=font)
    if sub:
        d.text((im.width - 8 * len(sub) - 10, 5), sub, fill=(220, 220, 220), font=small)
    return im


for view in ("3p", "1p"):
    frames = []
    for f in range(72):
        a = Image.open(os.path.join(tmp, f"seq_AAAA_{view}", f"{f:03d}.png")).convert("RGB")
        b = Image.open(os.path.join(tmp, f"seq_ABAC_{view}", f"{f:03d}.png")).convert("RGB")
        k = f // 18
        sub = f"f{f:02d}  t={f / 60:.3f}s" + ("  CONTACT" if f % 18 == 9 else "")
        a = label(a, "repeated A-A-A-A  (" + "AAAA"[k] + ")", sub)
        b = label(b, "varied A-B-A-C  (" + "ABAC"[k] + ")", sub)
        c = Image.new("RGB", (a.width + b.width + 6, a.height), (30, 30, 30))
        c.paste(a, (0, 0))
        c.paste(b, (a.width + 6, 0))
        frames.append(c)
    gif = [fr.resize((fr.width // 2 * 2 // (2 if view == "1p" else 1), fr.height // (2 if view == "1p" else 1)))
           for fr in frames[::2]]
    gif[0].save(os.path.join(out, f"s06_compare_AAAA_vs_ABAC_{view}.gif"), save_all=True, append_images=gif[1:],
                duration=33, loop=0, optimize=True)
    # key frames sheet: contact and anticipation of each stroke
    keys = [3, 9, 21, 27, 39, 45, 57, 63]
    w, h = frames[0].width // 2, frames[0].height // 2
    sheet = Image.new("RGB", (w * 2, h * 4), (20, 20, 20))
    for i, f in enumerate(keys):
        t = frames[f].resize((w, h))
        sheet.paste(t, ((i % 2) * w, (i // 2) * h))
    sheet.save(os.path.join(out, f"s06_compare_keyframes_{view}.png"))
    print("[compose]", view, len(frames), "frames")

# layered: strikes over idle / walking / sneaking legs
rows = []
for base in ("Idle", "Walk_F", "Sneak_F"):
    ims = [label(Image.open(os.path.join(tmp, f"layer_{base}_{f:02d}.png")).convert("RGB"), f"{base} legs + A,B", f"f{f}")
           for f in (3, 9, 15, 21, 27, 33)]
    row = Image.new("RGB", (sum(i.width for i in ims), ims[0].height))
    x = 0
    for i in ims:
        row.paste(i, (x, 0))
        x += i.width
    rows.append(row)
sheet = Image.new("RGB", (rows[0].width, sum(r.height for r in rows)))
y = 0
for r in rows:
    sheet.paste(r, (0, y))
    y += r.height
sheet = sheet.resize((sheet.width // 2, sheet.height // 2))
sheet.save(os.path.join(out, "s06_strikes_over_locomotion.png"))
print("[compose] layered sheet")
