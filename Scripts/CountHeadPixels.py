"""Count the player head's pixels in the body suite's magenta-head shots (FoundationBodyFirstPersonProbe.cpp).

The probe draws both bodies' head material magenta, then shoots first person at pitch -89.9..89.9, standing and
crouched (runtime-<fps>-body-fp-head-*.png), and one front F5 view as the control (the head must show there). A pixel
counts as head when red and blue both clearly outweigh green (lit magenta stays well apart from the grid, sky and
skin). PLAN.md 7: 0 head pixels in every first-person shot.

    uv run --no-project --python 3.13 --with pillow python -I Scripts/CountHeadPixels.py <runtime evidence folder>
"""
import json
import sys
from pathlib import Path

from PIL import Image


def magenta(path):
    image = Image.open(path).convert("RGB")
    return sum(1 for r, g, b in image.getdata() if r > 60 and b > 60 and r > 1.6 * g and b > 1.6 * g)


folder = Path(sys.argv[1])
shots = sorted(folder.glob("*-body-fp-head-*.png"))
counts = {shot.name: magenta(shot) for shot in shots}
control = {k: v for k, v in counts.items() if "control" in k}
first_person = {k: v for k, v in counts.items() if "control" not in k}
result = {"shots": len(shots), "first_person": first_person, "control": control,
          "pass": bool(first_person) and all(v == 0 for v in first_person.values())
          and bool(control) and all(v > 0 for v in control.values())}
print(json.dumps(result, indent=1))
sys.exit(0 if result["pass"] else 1)
