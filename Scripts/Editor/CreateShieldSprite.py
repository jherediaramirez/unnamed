"""The shield's GUI stand-in sprite (survival step 19b): 26.3 draws the shield's 3D special model in the GUI, which the HUD's
flat sprites cannot; this 16 x 16 sprite is the plate's front face of the jar's entity/shield/shield_base_nopattern.png
(box UV of the 12 x 22 x 1 plate: front at (1, 1), 12 x 22), scaled to 9 x 16 by nearest pixel and centred.
    uv run --no-project --python 3.13 --with pillow python -I Scripts/Editor/CreateShieldSprite.py
Writes Art/StandIns/item/shield.png (a Docs/StandIns.md row); ImportItems.py imports it as ItemRoot/Gui/item/shield."""
from pathlib import Path

from PIL import Image

ROOT = Path(__file__).resolve().parents[2]
SOURCE = ROOT / "Reference/minecraft-items-26.3/assets/minecraft/textures/entity/shield/shield_base_nopattern.png"
OUT = ROOT / "Art/StandIns/item/shield.png"
FRONT = (1, 1, 12, 22)  # x, y, width, height
WIDTH, HEIGHT = 9, 16


def main():
    source = Image.open(SOURCE).convert("RGBA")
    x0, y0, w, h = FRONT
    sprite = Image.new("RGBA", (16, 16), (0, 0, 0, 0))
    left = (16 - WIDTH) // 2
    for y in range(HEIGHT):
        for x in range(WIDTH):
            sprite.putpixel((left + x, y), source.getpixel((x0 + (2 * x + 1) * w // (2 * WIDTH),
                                                            y0 + (2 * y + 1) * h // (2 * HEIGHT))))
    OUT.parent.mkdir(parents=True, exist_ok=True)
    sprite.save(OUT)
    print(f"wrote {OUT.relative_to(ROOT).as_posix()}")


if __name__ == "__main__":
    main()
