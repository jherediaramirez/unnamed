"""Generates Source/Unnamed/Foundation/FoundationCookingData.inl (T1 step 8a, the furnace).

Cooking recipes come from the 26.3 client jar's data/minecraft/recipe/*.json (types smelting, blasting, smoking,
campfire_cooking; ingredient tags resolved through data/minecraft/tags/item). Fuels exist only in game code, so their
burn ticks come from the value report's table (SURVIVAL-FINDINGS-26.3.md, survival.r3.fuels). Every recipe's cooking
time and XP is checked against the report's recipe table (survival.r3.recipes); a mismatch or a count other than 116
recipes and 347 fuels fails. The hash-checked item reference is not read or changed.

    uv run --no-project --python 3.13 python -I Scripts/GenerateCookingData.py [--jar <26.3.jar>] [--findings <md>]
        [--check]

--check compares with the committed .inl instead of writing it.
"""

import argparse
import json
import os
import re
import sys
import zipfile
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
OUT = ROOT / "Source" / "Unnamed" / "Foundation" / "FoundationCookingData.inl"
DEFAULT_JAR = Path(os.environ.get("APPDATA", "")) / "ModrinthApp" / "meta" / "versions" / "26.3" / "26.3.jar"
DEFAULT_FINDINGS = Path(r"C:\dev\projects\value-holder\SURVIVAL-FINDINGS-26.3.md")
KINDS = {
    "minecraft:smelting": "Smelting",
    "minecraft:blasting": "Blasting",
    "minecraft:smoking": "Smoking",
    "minecraft:campfire_cooking": "Campfire",
}
REPORT_KINDS = {"smelting": "Smelting", "blasting": "Blasting", "smoking": "Smoking", "campfire_cooking": "Campfire"}
EXPECTED_RECIPES, EXPECTED_FUELS = 116, 347


def strip(name):
    return name.split(":", 1)[1] if ":" in name else name


def read_tags(jar):
    tags = {}
    prefix = "data/minecraft/tags/item/"
    for name in jar.namelist():
        if name.startswith(prefix) and name.endswith(".json"):
            tags[name[len(prefix) : -len(".json")]] = json.loads(jar.read(name))["values"]
    return tags


def resolve(entry, tags, seen=()):
    """Item names for an ingredient entry: an item, a #tag (nested tags resolved) or a list of either."""
    if isinstance(entry, list):
        out = []
        for part in entry:
            out += [item for item in resolve(part, tags, seen) if item not in out]
        return out
    if isinstance(entry, dict):
        entry = entry.get("item") or "#" + entry["tag"]
    if entry.startswith("#"):
        tag = strip(entry[1:])
        if tag in seen:
            raise SystemExit(f"tag cycle at {tag}")
        out = []
        for value in tags[tag]:
            value = value["id"] if isinstance(value, dict) else value
            out += [item for item in resolve(value, tags, seen + (tag,)) if item not in out]
        return out
    return [strip(entry)]


def read_recipes(jar, tags):
    recipes = []
    prefix = "data/minecraft/recipe/"
    for name in sorted(jar.namelist()):
        if not (name.startswith(prefix) and name.endswith(".json")):
            continue
        data = json.loads(jar.read(name))
        kind = KINDS.get(data.get("type"))
        if not kind:
            continue
        result = data["result"]
        recipes.append(
            {
                "id": name[len(prefix) : -len(".json")],
                "kind": kind,
                "ingredients": resolve(data["ingredient"], tags),
                "result": strip(result["id"]),
                "count": int(result.get("count", 1)),
                "ticks": int(data.get("cookingtime", 200)),
                "xp": float(data.get("experience", 0)),
            }
        )
    return recipes


def read_table(lines, summary):
    """The rows of the markdown table inside the <details> block whose summary starts with `summary`."""
    start = next(i for i, line in enumerate(lines) if line.startswith("<summary>") and summary in line)
    rows = []
    for line in lines[start + 1 :]:
        if line.startswith("</details>"):
            break
        if line.startswith("| ") and not line.startswith("| ---"):
            rows.append([cell.strip() for cell in line.strip().strip("|").split("|")])
    return rows[1:]


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--jar", type=Path, default=DEFAULT_JAR)
    parser.add_argument("--findings", type=Path, default=DEFAULT_FINDINGS)
    parser.add_argument("--check", action="store_true")
    args = parser.parse_args()

    with zipfile.ZipFile(args.jar) as jar:
        recipes = read_recipes(jar, read_tags(jar))
    lines = args.findings.read_text(encoding="utf-8").splitlines()
    fuels = [(row[0], int(row[1]), int(row[2]), int(row[3])) for row in read_table(lines, "Every fuel")]
    report = {row[0]: row for row in read_table(lines, "Every loaded cooking recipe")}

    errors = []
    if len(recipes) != EXPECTED_RECIPES:
        errors.append(f"{len(recipes)} cooking recipes in the jar, expected {EXPECTED_RECIPES}")
    if len(fuels) != EXPECTED_FUELS:
        errors.append(f"{len(fuels)} fuels in the findings table, expected {EXPECTED_FUELS}")
    for recipe in recipes:
        row = report.get(recipe["id"])
        if not row:
            errors.append(f"{recipe['id']}: not in survival.r3.recipes")
            continue
        kind = REPORT_KINDS.get(row[1])
        if kind != recipe["kind"] or int(row[2]) != recipe["ticks"] or abs(float(row[4]) - recipe["xp"]) > 1e-9:
            errors.append(f"{recipe['id']}: jar {recipe['kind']} {recipe['ticks']} {recipe['xp']} vs report {row}")
    if len(report) != len(recipes):
        errors.append(f"the report lists {len(report)} recipes, the jar {len(recipes)}")
    # One recipe per ingredient and kind: the furnace takes the first match, so a second would be ambiguous.
    seen = {}
    for recipe in recipes:
        for item in recipe["ingredients"]:
            key = (recipe["kind"], item)
            if key in seen:
                errors.append(f"{item}: both {seen[key]} and {recipe['id']} ({recipe['kind']})")
            seen[key] = recipe["id"]
    if errors:
        print("\n".join(errors), file=sys.stderr)
        return 1

    out = [
        "// Generated by Scripts/GenerateCookingData.py (T1 step 8a); do not edit.",
        "// Cooking recipes: the 26.3 client jar's data/minecraft/recipe/*.json (smelting, blasting, smoking,",
        "// campfire_cooking; item tags resolved), checked against SURVIVAL-FINDINGS-26.3.md survival.r3.recipes.",
        "// Fuels: SURVIVAL-FINDINGS-26.3.md survival.r3.fuels (furnace, smoker, blast furnace burn ticks).",
        f"// {len(recipes)} recipes, {len(fuels)} fuels.",
        "// FOUNDATION_FUEL(Item, FurnaceTicks, SmokerTicks, BlastTicks)",
        "// FOUNDATION_COOKING(Id, Kind, Result, Count, CookTicks, Experience, Ingredients separated by spaces)",
        "",
    ]
    for item, furnace, smoker, blast in sorted(fuels):
        out.append(f'FOUNDATION_FUEL(TEXT("{item}"), {furnace}, {smoker}, {blast})')
    out.append("")
    for recipe in recipes:
        out.append(
            f'FOUNDATION_COOKING(TEXT("{recipe["id"]}"), {recipe["kind"]}, TEXT("{recipe["result"]}"), '
            f'{recipe["count"]}, {recipe["ticks"]}, {recipe["xp"]!r}f, TEXT("{" ".join(recipe["ingredients"])}"))'
        )
    text = "\n".join(out) + "\n"
    if args.check:
        same = OUT.exists() and OUT.read_text(encoding="utf-8") == text
        print("FoundationCookingData.inl: " + ("up to date" if same else "DIFFERS"))
        return 0 if same else 1
    OUT.write_text(text, encoding="utf-8", newline="\n")
    print(f"wrote {OUT.relative_to(ROOT)}: {len(recipes)} recipes, {len(fuels)} fuels")
    return 0


if __name__ == "__main__":
    sys.exit(main())
