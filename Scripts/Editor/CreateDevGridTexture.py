"""Generate the dev grid markup mask Art/DevGrid/T_DevGridMarkup.png.

Standalone generator: it runs OUTSIDE Unreal (plain Python + Pillow), not through
-run=pythonscript. From the project root:

    uv run --with pillow python Scripts/Editor/CreateDevGridTexture.py

The PNG is the committed art source; CreateDevGridMaterial.py imports it as
/Game/DevGrid/T_DevGridMarkup. It is ONE 1 m tile (1024 px, 1 px ~ 0.977 mm), RGB mask, origin top-left:

    R  tile seam lines on the tile border, SEAM_PX each side (two adjacent tiles form a 2*SEAM_PX seam)
    G  "1m" label in the bottom-right corner, upright in texture space
    B  small "+" crosses centred on the 4 tile corners (quarter crosses, so they join when tiled)
       plus a faint 0.5 m midline cross at MIDLINE_VALUE intensity

Everything is drawn 4x supersampled and box-filtered down, and is symmetric about the
tile edges, so the texture wraps seamlessly.
"""
import sys
from pathlib import Path

from PIL import Image, ImageChops, ImageDraw, ImageFont

SIZE = 1024            # px per 1 m tile
SS = 4                 # supersampling factor
SEAM_PX = 6            # seam half-width (px) on each side of a tile border
CROSS_ARM = 56         # + cross half-length (px)
CROSS_HALF_THICK = 10  # + cross half-thickness (px)
MIDLINE_HALF_THICK = 2 # faint midline half-thickness (px)
MIDLINE_VALUE = 64     # ~25 % intensity
LABEL_TEXT = "1m"
LABEL_HEIGHT = 90      # px, height of the digit glyph
LABEL_MARGIN_RIGHT = 52
LABEL_MARGIN_BOTTOM = 52
FONT_CANDIDATES = ("segoeuib.ttf", "segoeui.ttf", "arialbd.ttf")

OUT = Path(__file__).resolve().parents[2] / "Art" / "DevGrid" / "T_DevGridMarkup.png"


def find_font():
    fonts = Path("C:/Windows/Fonts")
    for name in FONT_CANDIDATES:
        if (fonts / name).exists():
            return fonts / name
    raise SystemExit("No usable font found in C:/Windows/Fonts: " + ", ".join(FONT_CANDIDATES))


def new_canvas():
    return Image.new("L", (SIZE * SS, SIZE * SS), 0)


def downsample(image):
    return image.resize((SIZE, SIZE), Image.BOX)


def seam_channel():
    image = new_canvas()
    draw = ImageDraw.Draw(image)
    band = SEAM_PX * SS
    full = SIZE * SS
    draw.rectangle((0, 0, full - 1, band - 1), fill=255)
    draw.rectangle((0, full - band, full - 1, full - 1), fill=255)
    draw.rectangle((0, 0, band - 1, full - 1), fill=255)
    draw.rectangle((full - band, 0, full - 1, full - 1), fill=255)
    return downsample(image)


def label_channel(font_path):
    # Size the font so the "1" glyph is LABEL_HEIGHT px tall (measured at a large reference size).
    probe = ImageFont.truetype(str(font_path), 1000)
    ref = probe.getbbox(LABEL_TEXT, anchor="ls")
    ratio = (ref[3] - ref[1]) / 1000.0
    font = ImageFont.truetype(str(font_path), round(LABEL_HEIGHT * SS / ratio))
    image = new_canvas()
    draw = ImageDraw.Draw(image)
    right = (SIZE - LABEL_MARGIN_RIGHT) * SS
    baseline = (SIZE - LABEL_MARGIN_BOTTOM) * SS
    draw.text((right, baseline), LABEL_TEXT, font=font, fill=255, anchor="rs")
    return downsample(image)


def cross_channel():
    image = new_canvas()
    draw = ImageDraw.Draw(image)
    arm, half, full = CROSS_ARM * SS, CROSS_HALF_THICK * SS, SIZE * SS
    for cx in (0, full):
        for cy in (0, full):  # full crosses centred on the corners; the canvas edge clips them to quarters
            draw.rectangle((cx - arm, cy - half, cx + arm - 1, cy + half - 1), fill=255)
            draw.rectangle((cx - half, cy - arm, cx + half - 1, cy + arm - 1), fill=255)
    mid = new_canvas()
    mdraw = ImageDraw.Draw(mid)
    centre, thick = full // 2, MIDLINE_HALF_THICK * SS
    mdraw.rectangle((0, centre - thick, full - 1, centre + thick - 1), fill=MIDLINE_VALUE)
    mdraw.rectangle((centre - thick, 0, centre + thick - 1, full - 1), fill=MIDLINE_VALUE)
    return ImageChops.lighter(downsample(image), downsample(mid))


def main():
    font_path = find_font()
    r, g, b = seam_channel(), label_channel(font_path), cross_channel()
    tile = Image.merge("RGB", (r, g, b))
    OUT.parent.mkdir(parents=True, exist_ok=True)
    tile.save(OUT, optimize=True)
    # Self-check: wrap seamlessness (opposite border pixels must mirror each other).
    px = tile.load()
    for i in range(SIZE):
        for edge_a, edge_b in (((0, i), (SIZE - 1, i)), ((i, 0), (i, SIZE - 1))):
            if px[edge_a] != px[edge_b]:
                raise SystemExit("Tile is not symmetric across its wrap edge at " + str(edge_a))
    print("Wrote {} ({}x{}, font {})".format(OUT, SIZE, SIZE, font_path.name))
    print("Label ink bbox (G):", g.point(lambda v: 255 if v > 8 else 0).getbbox())


if __name__ == "__main__":
    sys.exit(main())
