"""Write Source/Unnamed/Tests/BlockR13Oracles.json, the R13 cross-check's oracle data (blocks B3b), from Jesus's lab
report SURVIVAL-RUN2-FINDINGS-26.3.md, R13 "Every requested block state", Appendix A (survival2.r13.shapes,
survival2.r13.connections).

The output keeps the box dictionary (pixels) and the shape rows of the families B3 and B4b touch (doors, trapdoors,
gates, buttons, levers, ladders), and every connection row. States are named without the "minecraft:" prefix and
stored once in "states"; rows index into it. Shape rows: [state, collision box id, outline box id]. Connection rows: [subject,
neighbour, side, result] with side 0 north, 1 east, 2 south, 3 west. It records the input's SHA-256.

usage: uv run --no-project --python 3.13 python -I Scripts/MakeR13Oracles.py [<SURVIVAL-RUN2-FINDINGS-26.3.md>]
"""
import hashlib
import json
import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
OUT = ROOT / "Source" / "Unnamed" / "Tests" / "BlockR13Oracles.json"
FINDINGS = Path(sys.argv[1]) if len(sys.argv) > 1 else Path(
    r"C:\dev\projects\value-holder\SURVIVAL-RUN2-FINDINGS-26.3.md")
# B4b adds the ladders; R13's vines list some boxes in another order than block_state_facts.json (B3b's offline note).
FAMILIES = ("doors", "trapdoors", "gates", "buttons", "levers", "ladders")
SIDES = ("north", "east", "south", "west")


def section(lines, title):
    start = next((i for i, line in enumerate(lines) if line.startswith("### " + title)), None)
    if start is None:
        sys.exit(f"{FINDINGS}: no section \"{title}\"")
    end = next((i for i in range(start + 1, len(lines)) if lines[i].startswith("#")), len(lines))
    return lines[start + 1:end]


def cells(line):
    return [cell.strip() for cell in line.strip().strip("|").split("|")]


def short(name):
    return name.removeprefix("minecraft:")


def main():
    data = FINDINGS.read_bytes()
    lines = data.decode("utf-8").splitlines()
    states, index = [], {}

    def state(name):
        name = short(name)
        if name not in index:
            index[name] = len(states)
            states.append(name)
        return index[name]

    boxes = {}
    for line in section(lines, "survival2.r13.shapes — box dictionary"):
        match = re.match(r"\| S(\d+) \| (.*) \|$", line)
        if match:
            boxes[int(match.group(1))] = [[round(v, 6) for v in box] for box in json.loads(match.group(2))]
    shapes, used = [], set()
    for line in section(lines, "survival2.r13.shapes — every state"):
        row = cells(line)
        if len(row) == 5 and row[0] in FAMILIES:
            collision, outline = int(row[2][1:]), int(row[3][1:])
            shapes.append([state(row[1]), collision, outline])
            used.update((collision, outline))
    connections = []
    for line in section(lines, "survival2.r13.connections"):
        row = cells(line)
        if len(row) == 4 and row[0].startswith("minecraft:"):
            connections.append([state(row[0]), state(row[1]), SIDES.index(row[2]), state(row[3])])
    out = {
        "source": FINDINGS.name,
        "source_sha256": hashlib.sha256(data).hexdigest(),
        "evidence": ["survival2.r13.shapes", "survival2.r13.connections"],
        "families": list(FAMILIES),
        "boxes": {str(key): boxes[key] for key in sorted(used)},
        "states": states,
        "shapes": shapes,
        "connections": connections,
    }
    OUT.write_text(json.dumps(out, separators=(",", ":")) + "\n", encoding="utf-8", newline="\n")
    print(f"wrote {OUT}: {len(shapes)} shape rows, {len(connections)} connection rows, {len(states)} states")


if __name__ == "__main__":
    main()
