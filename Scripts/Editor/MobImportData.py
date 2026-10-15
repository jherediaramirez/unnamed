"""DA_Mobs' values from Reference/minecraft-entities-26.3 (survival T6 20a), without Unreal: ImportMobs.py writes them,
and `uv run --no-project --python 3.13 python -I Scripts/Editor/MobImportData.py` prints a summary (a dry run).

- Models: each mob's main layer from facts/entity_models.json, its parts (path, pivot, rotation) with their cubes'
  faces (normal, four corners in model pixels, four UVs), and its default texture (the temperate variant where there
  are variants).
- Loot: data/minecraft/loot_table/entities/<mob>.json flattened the way DA_Items' block tables are (FoundationLoot.h):
  each pool's entries become ordered candidates (an alternatives' children in order; a loot_table entry inlined when
  the table it names is one pool of one item), conditions become the table's nodes, functions its modifiers. A table
  using what the evaluator cannot do yet (weighted choices between entries, a tag entry) is left out, with the reason.
"""
import hashlib
import json
from pathlib import Path

REPO = Path(__file__).resolve().parents[2]
REF = REPO / "Reference/minecraft-entities-26.3"
LOOT = REF / "data/minecraft/loot_table/entities"
RECIPES = REF / "data/minecraft/recipe"
TEXTURES = "assets/minecraft/textures/"
# The eight mobs (FoundationMobs' EFoundationMobType order) and the texture each draws with.
MOBS = {
    "cow": "entity/cow/cow_temperate.png",
    "pig": "entity/pig/pig_temperate.png",
    "sheep": "entity/sheep/sheep.png",
    "chicken": "entity/chicken/chicken_temperate.png",
    "zombie": "entity/zombie/zombie.png",
    "skeleton": "entity/skeleton/skeleton.png",
    "creeper": "entity/creeper/creeper.png",
    "spider": "entity/spider/spider.png",
}


class Unsupported(Exception):
    pass


def bare(name):
    return name.removeprefix("minecraft:")


def verify_hashes():
    """Every file the reference lists matches its SHA256SUMS.txt line; returns that file's digest."""
    sums = (REF / "SHA256SUMS.txt").read_bytes()
    for line in sums.decode("utf-8").splitlines():
        digest, _, name = line.partition("  ")
        if hashlib.sha256((REF / name).read_bytes()).hexdigest() != digest:
            raise SystemExit(f"MOB_IMPORT FAIL: {name} differs from the reference's SHA256SUMS.txt")
    return hashlib.sha256(sums).hexdigest()


def models():
    facts = json.loads((REF / "facts/entity_models.json").read_text(encoding="utf-8"))
    sizes = {t["name"]: (t["width"], t["height"])
             for t in json.loads((REF / "textures.json").read_text(encoding="utf-8"))["textures"]}
    out = []
    for mob, texture in MOBS.items():
        layer = facts[f"{mob}#main"]
        parts = []
        for part in layer["parts"]:
            faces = []
            for cube in layer["cubes"]:
                if cube["path"].strip("/") != part["path"]:
                    continue
                for face in cube["faces"]:
                    faces.append({"normal": face["n"], "positions": [v[:3] for v in face["v"]],
                                  "uvs": [v[3:5] for v in face["v"]]})
            parts.append({"path": part["path"], "pivot": part["pivot"], "rotation": part["rot"], "faces": faces})
        out.append({"id": mob, "layer": f"{mob}#main", "texture": texture, "texture_size": sizes[texture],
                    "parts": parts})
    return out


def furnace_results():
    """Ingredient -> result of the reference's smelting recipes."""
    results = {}
    for path in sorted(RECIPES.glob("*.json")):
        recipe = json.loads(path.read_text(encoding="utf-8"))
        if recipe["type"] == "minecraft:smelting" and isinstance(recipe["ingredient"], str):
            results[bare(recipe["ingredient"])] = bare(recipe["result"]["id"])
    return results


def count_range(count):
    if isinstance(count, (int, float)):
        return int(count), int(count)
    if bare(count["type"]) == "uniform":
        return int(count["min"]), int(count["max"])
    if bare(count["type"]) == "constant":
        return int(count["value"]), int(count["value"])
    raise Unsupported(f"count {count}")


def functions_of(value):
    """An entry's, pool's or table's item modifiers, whichever key the jar used (26.3: "modifier")."""
    found = value.get("modifier", value.get("functions", []))
    return found if isinstance(found, list) else [found]


def conditions_of(value):
    """A condition list, whichever key the jar used."""
    if "conditions" in value:
        return value["conditions"]
    return [value["condition"]] if "condition" in value else []


def entity_facts(predicate):
    """An entity predicate's facts as (key, value) pairs FoundationLoot's EntityProperties checks."""
    facts = []
    for key, value in predicate.items():
        key = bare(key)
        if key == "flags":
            facts += [(flag, "true" if on else "false") for flag, on in value.items()]
        elif key == "components":
            facts += [(bare(component), str(v)) for component, v in value.items()]
        elif key == "type_specific/sheep":
            facts += [(field, "true" if v is True else "false" if v is False else str(v)) for field, v in value.items()]
        elif key == "equipment":
            # The direct attacker's main-hand enchantments (#minecraft:smelts_loot): nothing is enchanted yet, so a
            # fact nothing has, which fails.
            facts.append(("equipment_enchantments", json.dumps(value, sort_keys=True)))
        elif key == "type":
            facts.append(("type", str(value)))
        else:
            raise Unsupported(f"entity predicate {key}")
    return facts


class Table:
    def __init__(self, name, furnace):
        self.name, self.furnace, self.nodes = name, furnace, []

    def node(self, condition):
        index = len(self.nodes)
        kind = bare(condition["condition"] if "condition" in condition and isinstance(condition["condition"], str)
                    else condition["type"])
        n = {"type": kind}
        self.nodes.append(n)
        if kind in ("any_of", "all_of"):
            n["terms"] = [self.node(t) for t in condition["terms"]]
        elif kind == "inverted":
            n["terms"] = [self.node(condition["term"])]
        elif kind == "entity_properties":
            facts = entity_facts(condition.get("predicate", {}))
            n.update(name=condition["entity"], state_keys=[k for k, _ in facts], state_values=[v for _, v in facts])
        elif kind == "killed_by_player":
            pass
        elif kind == "random_chance":
            n["chances"] = [float(condition["chance"])]
        elif kind == "random_chance_with_enchanted_bonus":
            # Nothing is enchanted: the unenchanted chance.
            n.update(type="random_chance", chances=[float(condition["unenchanted_chance"])])
        else:
            raise Unsupported(f"condition {kind}")
        return index

    def modifiers(self, functions, item):
        out = []
        for function in functions:
            kind = bare(function["function"] if "function" in function else function["type"])
            m = {"type": kind, "tests": [self.node(c) for c in conditions_of(function)]}
            if kind == "set_count":
                m["min"], m["max"] = count_range(function["count"])
                m["add"] = bool(function.get("add", False))
            elif kind == "furnace_smelt":
                if item not in self.furnace:
                    raise Unsupported(f"furnace_smelt of {item}: no smelting recipe in the reference")
                m["result_item"] = self.furnace[item]
            elif kind == "enchanted_count_increase":
                pass
            else:
                raise Unsupported(f"function {kind}")
            out.append(m)
        return out

    def candidates(self, entry):
        kind = bare(entry["type"])
        if entry.get("weight", 1) != 1 or entry.get("quality", 0):
            raise Unsupported("weighted entry")
        if kind == "alternatives":
            return [c for child in entry["children"] for c in self.candidates(child)]
        if kind == "item":
            item = bare(entry["name"])
            return [{"item": item, "tests": [self.node(c) for c in conditions_of(entry)],
                     "modifiers": self.modifiers(functions_of(entry), item)}]
        if kind == "loot_table":
            if functions_of(entry):
                raise Unsupported(f"loot_table entry {entry['value']} with its own modifiers")
            sub = json.loads((LOOT.parent / (bare(entry["value"]) + ".json")).read_text(encoding="utf-8"))
            pools = sub["pools"]
            if len(pools) != 1 or len(pools[0]["entries"]) != 1 or pools[0].get("rolls", 1) != 1 or \
                    conditions_of(pools[0]) or bare(pools[0]["entries"][0]["type"]) != "item":
                raise Unsupported(f"loot_table entry {entry['value']}")
            inner = self.candidates(pools[0]["entries"][0])[0]
            inner["tests"] = [self.node(c) for c in conditions_of(entry)] + inner["tests"]
            return [inner]
        raise Unsupported(f"entry {kind}")

    def build(self, data):
        pools = []
        for pool in data["pools"]:
            rolls = pool.get("rolls", 1)
            if not isinstance(rolls, int) or pool.get("bonus_rolls"):
                raise Unsupported("rolls")
            entries = pool["entries"]
            # Several entries without conditions are a weighted choice, not "the first that passes".
            if len(entries) > 1 and sum(1 for e in entries if not conditions_of(e)) > 1:
                raise Unsupported("a weighted choice between entries")
            candidates = [c for e in entries for c in self.candidates(e)]
            pools.append({"rolls": rolls, "tests": [self.node(c) for c in conditions_of(pool)],
                          "modifiers": self.modifiers(functions_of(pool), None),
                          "candidates": candidates})
        return {"block": self.name, "pools": pools, "conditions": self.nodes,
                "modifiers": self.modifiers(functions_of(data), None)}


def loot_tables():
    """The flattened tables (by entity id) and the left-out ones with their reasons."""
    furnace = furnace_results()
    tables, left_out = [], {}
    for mob in MOBS:
        data = json.loads((LOOT / f"{mob}.json").read_text(encoding="utf-8"))
        try:
            tables.append(Table(mob, furnace).build(data))
        except Unsupported as why:
            left_out[mob] = str(why)
    return tables, left_out


def candidate_values(candidate):
    """FFoundationLootCandidateAsset's values (the modifiers stay dicts; ImportMobs.py makes them structs)."""
    return {"item": candidate["item"], "tests": candidate["tests"], "modifiers": candidate["modifiers"]}


if __name__ == "__main__":
    print("reference digest", verify_hashes())
    for model in models():
        print(model["id"], model["texture"], model["texture_size"], len(model["parts"]), "parts",
              sum(len(p["faces"]) for p in model["parts"]), "faces")
    tables, left_out = loot_tables()
    for table in tables:
        print(table["block"], [[(c["item"], [m["type"] for m in c["modifiers"]]) for c in p["candidates"]]
                               for p in table["pools"]], len(table["conditions"]), "nodes")
    print("left out:", left_out)
