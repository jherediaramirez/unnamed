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
import re
from pathlib import Path

REPO = Path(__file__).resolve().parents[2]
REF = REPO / "Reference/minecraft-entities-26.3"
LOOT = REF / "data/minecraft/loot_table/entities"
RECIPES = REF / "data/minecraft/recipe"
COOKING = REPO / "Source/Unnamed/Foundation/FoundationCookingData.inl"
# Read only (survival T6 20b2): the biomes' natural_mob_spawns and creature_world_gen_spawn_probability attributes.
WORLDGEN = REPO / "Reference/minecraft-worldgen-26.3"
BIOMES = WORLDGEN / "data/minecraft/worldgen/biome"
# The chunk-generation creature chance of a biome without the attribute (minecraft.wiki "Mob spawning"; the scout O11).
DEFAULT_GEN_CHANCE = 0.1
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


def biome_spawns():
    """Each biome's creature spawn entries and chunk-generation chance, every entity kept (the game skips the mobs it
    has not built), after checking the biome files against the worldgen reference's SHA256SUMS.txt. Returns
    (biomes, digest of the worldgen SHA256SUMS.txt)."""
    sums = (WORLDGEN / "SHA256SUMS.txt").read_bytes()
    listed = {}
    for line in sums.decode("utf-8").splitlines():
        digest, _, name = line.partition("  ")
        listed[name] = digest
    biomes = []
    for path in sorted(BIOMES.glob("*.json")):
        name = path.relative_to(WORLDGEN).as_posix()
        if listed.get(name) != hashlib.sha256(path.read_bytes()).hexdigest():
            raise SystemExit(f"MOB_IMPORT FAIL: {name} differs from the worldgen reference's SHA256SUMS.txt")
        attributes = json.loads(path.read_text(encoding="utf-8")).get("attributes", {})
        spawns = attributes.get("minecraft:gameplay/natural_mob_spawns", {}).get("argument", {})
        creatures = []
        for entry in spawns.get("spawns_by_category", {}).get("creature", []):
            count = entry["count"]
            if isinstance(count, int):
                low = high = count
            elif count.get("type") == "minecraft:uniform":
                low, high = count["min_inclusive"], count["max_inclusive"]
            else:
                raise SystemExit(f"MOB_IMPORT FAIL: {name}: count {count}")
            creatures.append({"mob": bare(entry["type"]), "weight": entry["weight"], "min_count": low,
                              "max_count": high})
        chance = attributes.get("minecraft:gameplay/creature_world_gen_spawn_probability", DEFAULT_GEN_CHANCE)
        biomes.append({"biome": "minecraft:" + path.stem, "chance": float(chance), "creatures": creatures})
    return biomes, hashlib.sha256(sums).hexdigest()


# Layers drawn over a mob's main layer, listed after the eight main models (20b: the sheep's wool, white; its undercoat
# and the other colours come with shearing, 20e): (mob, layer, texture, texture asset name).
LAYERS = [("sheep", "sheep#wool", "entity/sheep/sheep_wool.png", "T_Mob_SheepWool")]


def texture_name(mob):
    return "T_Mob_" + mob[0].upper() + mob[1:]


def models():
    facts = json.loads((REF / "facts/entity_models.json").read_text(encoding="utf-8"))
    sizes = {t["name"]: (t["width"], t["height"])
             for t in json.loads((REF / "textures.json").read_text(encoding="utf-8"))["textures"]}
    layers = [(mob, f"{mob}#main", texture, texture_name(mob)) for mob, texture in MOBS.items()] + LAYERS
    out = []
    for mob, name, texture, asset in layers:
        layer = facts[name]
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
        out.append({"id": mob, "layer": name, "texture": texture, "texture_asset": asset,
                    "texture_size": sizes[texture], "parts": parts})
    return out


def furnace_results():
    """Ingredient -> result of the reference's smelting recipes."""
    results = {}
    for path in sorted(RECIPES.glob("*.json")):
        recipe = json.loads(path.read_text(encoding="utf-8"))
        if recipe["type"] == "minecraft:smelting" and isinstance(recipe["ingredient"], str):
            results[bare(recipe["ingredient"])] = bare(recipe["result"]["id"])
    # Survival T6 20c: the ones the entity reference does not carry (the zombie's potato) from the furnace's cooking
    # table, generated from the same 26.3 jar's recipes (Scripts/GenerateCookingData.py).
    for result, ingredients in re.findall(
            r'FOUNDATION_COOKING\(TEXT\("[^"]+"\), Smelting, TEXT\("([^"]+)"\), [^)]*?, ((?:TEXT\("[^"]+"\)\s*)+)\)',
            COOKING.read_text(encoding="utf-8")):
        for ingredient in re.findall(r'TEXT\("([^"]+)"\)', ingredients):
            for one in ingredient.split():
                results.setdefault(one, result)
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
        elif key == "vehicle":
            # Nothing rides anything yet (survival T6 20c: treated as false), so a fact nothing has, which fails.
            facts.append(("vehicle", json.dumps(value, sort_keys=True)))
        elif key == "type":
            facts.append(("type", str(value)))
        else:
            raise Unsupported(f"entity predicate {key}")
    return facts


def needs_vehicle(condition):
    """The condition (or a term every pass needs) checks what the entity rides."""
    kind = bare(condition.get("type", condition.get("condition", "")))
    if kind == "all_of":
        return any(needs_vehicle(term) for term in condition["terms"])
    return kind == "entity_properties" and any(bare(key) == "vehicle" for key in condition.get("predicate", {}))


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
            # A pool only a rider passes never drops while nothing rides (20c: the zombie's chicken-jockey disc, an
            # item DA_Items does not carry); left out so the rest of the table loads.
            if any(needs_vehicle(c) for c in conditions_of(pool)):
                continue
            rolls = pool.get("rolls", 1)
            if not isinstance(rolls, int) or pool.get("bonus_rolls"):
                raise Unsupported("rolls")
            entries = pool["entries"]
            candidates = [c for e in entries for c in self.candidates(e)]
            # Several entries without conditions are a weighted choice, not "the first that passes" (20c: the
            # zombie's rare drop): each plain item entry gets its weight (1: candidates() rejects any other weight).
            if len(entries) > 1 and sum(1 for e in entries if not conditions_of(e)) > 1:
                if any(bare(e["type"]) != "item" for e in entries):
                    raise Unsupported("a weighted choice between composite entries")
                for candidate in candidates:
                    candidate["weight"] = 1
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
    return {"item": candidate["item"], "tests": candidate["tests"], "modifiers": candidate["modifiers"],
            "weight": candidate.get("weight", 0)}


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
    biomes, worldgen = biome_spawns()
    print("worldgen digest", worldgen[:12], len(biomes), "biomes")
    for biome in biomes:
        if biome["biome"] in ("minecraft:plains", "minecraft:snowy_plains", "minecraft:badlands"):
            print(biome)
