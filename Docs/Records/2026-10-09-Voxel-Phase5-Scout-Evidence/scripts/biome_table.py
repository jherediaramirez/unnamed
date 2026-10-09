"""Tabulate climate and colour fields of every biome JSON (args: biome dir, out json)."""
import json, os, sys
src, out = sys.argv[1], sys.argv[2]
rows = []
for f in sorted(os.listdir(src)):
    d = json.load(open(os.path.join(src, f)))
    eff = d.get("effects", {})
    attrs = {k: v for k, v in d.get("attributes", {}).items() if k.startswith("minecraft:visual/")}
    rows.append({"biome": f[:-5], "temperature": d.get("temperature"), "downfall": d.get("downfall"),
                 "has_precipitation": d.get("has_precipitation"), "temperature_modifier": d.get("temperature_modifier", "none"),
                 "effects": eff, "visual_attributes": attrs, "other_keys": sorted(set(d) - {"attributes", "effects", "temperature", "downfall", "has_precipitation", "temperature_modifier", "features", "carvers", "spawners", "spawn_costs"})})
json.dump(rows, open(out, "w"), indent=1)
keys = sorted({k for r in rows for k in r["effects"]})
akeys = sorted({k for r in rows for k in r["visual_attributes"]})
print("effect keys:", keys); print("visual attr keys:", akeys); print("other keys:", sorted({k for r in rows for k in r["other_keys"]}))
print("modifiers:", sorted({r["temperature_modifier"] for r in rows}), "gcm:", sorted({r["effects"].get("grass_color_modifier", "-") for r in rows}))
for r in rows:
    e = r["effects"]
    print(r["biome"], r["temperature"], r["downfall"], int(bool(r["has_precipitation"])), r["temperature_modifier"][0],
          e.get("water_color"), e.get("grass_color", "-"), e.get("foliage_color", "-"), e.get("dry_foliage_color", "-"), e.get("grass_color_modifier", "-"))
