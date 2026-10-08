import json
import sys
import zipfile
from collections import defaultdict
from pathlib import Path

JAR = Path(r"C:\Users\jherediaramirez\AppData\Roaming\ModrinthApp\meta\versions\26.3\26.3.jar")
REPORTS = Path(r"C:\dev\sandbox\mc-26.3-blocks\reports\reports")
OUT_MD = Path(r"C:\dev\sandbox\mc-26.3-blocks\out\worldgen_heights.md")
TAGS_DIR_PREFIX = "data/minecraft/tags/block/"

z = zipfile.ZipFile(JAR)
names = z.namelist()


def load_prefix(prefix):
    out = {}
    for n in names:
        if n.startswith(prefix) and n.endswith(".json"):
            out[n[len(prefix):-5]] = json.loads(z.read(n).decode("utf-8"))
    return out


cf = load_prefix("data/minecraft/worldgen/feature/")
pf = load_prefix("data/minecraft/worldgen/placed_feature/")
biomes = load_prefix("data/minecraft/worldgen/biome/")
dimtypes = load_prefix("data/minecraft/dimension_type/")
block_tags = {k: v for k, v in load_prefix(TAGS_DIR_PREFIX).items()}


def short(s):
    return s.replace("minecraft:", "") if isinstance(s, str) else s


# ---------- dimension membership ----------
ow_params = json.loads((REPORTS / "biome_parameters/minecraft/overworld.json").read_text(encoding="utf-8"))
nt_params = json.loads((REPORTS / "biome_parameters/minecraft/nether.json").read_text(encoding="utf-8"))
OW = {b["biome"].replace("minecraft:", "") for b in ow_params["biomes"]}
NT = {b["biome"].replace("minecraft:", "") for b in nt_params["biomes"]}
END_NAMES = {"the_end", "end_highlands", "end_midlands", "end_barrens", "small_end_islands"}


def dim_of_biome(b):
    if b in OW:
        return "overworld"
    if b in NT:
        return "nether"
    if b in END_NAMES:
        return "end"
    return "other"


BIOME_DIM = {b: dim_of_biome(b) for b in biomes}
DIM_TOTAL = defaultdict(int)
for b, d in BIOME_DIM.items():
    DIM_TOTAL[d] += 1

# placed feature -> set of biomes (direct listing)
feat_biomes = defaultdict(set)
for b, v in biomes.items():
    for step in v.get("features", []):
        for f in step:
            if isinstance(f, str):
                feat_biomes[short(f)].add(b)

# ---------- helpers for block tags ----------
def resolve_tag(tag, seen=None):
    """Return set of block ids (short) for a block tag name (short or minecraft:)."""
    seen = seen or set()
    t = short(tag).lstrip("#")
    if t in seen:
        return set()
    seen.add(t)
    obj = block_tags.get(t)
    out = set()
    if obj is None:
        return {f"?{t}"}
    for v in obj.get("values", []):
        if isinstance(v, dict):
            v = v.get("id")
        if not isinstance(v, str):
            continue
        if v.startswith("#"):
            out |= resolve_tag(v[1:], seen)
        else:
            out.add(short(v))
    return out


def tag_label(tag):
    t = short(tag).lstrip("#")
    members = resolve_tag(t)
    if len(members) <= 6:
        return f"#{t} ({', '.join(sorted(members))})"
    return f"#{t} ({len(members)} blocks)"


def pred_tags(p, acc, flags):
    if isinstance(p, str):
        acc.append(short(p))
        return
    if not isinstance(p, dict):
        return
    pt = p.get("predicate_type", p.get("type"))
    if pt == "minecraft:tag_match":
        acc.append("#" + short(p["tag"]).lstrip("#"))
    elif pt in ("minecraft:block_match",):
        acc.append(short(p["block"]))
    elif pt in ("minecraft:random_block_match",):
        acc.append(short(p["block"]) + f"(p={p.get('probability')})")
    elif pt == "minecraft:height_match":
        flags.add("y-gated")
    for k in ("rules", "predicates"):
        for sub in p.get(k, []):
            pred_tags(sub, acc, flags)
    if "rule" in p:
        pred_tags(p["rule"], acc, flags)


# ---------- height ----------
DIM_Y = {"overworld": (-64, 320), "nether": (0, 256), "end": (0, 256)}


def anchor_y(a, dim):
    bottom, top = DIM_Y[dim]
    if "absolute" in a:
        return a["absolute"]
    if "above_bottom" in a:
        return bottom + a["above_bottom"]
    if "below_top" in a:
        return top - a["below_top"]
    return None


def height_str(p, dim):
    h = p.get("height")
    if h is None:
        return None
    t = short(h.get("type"))
    if t == "constant":
        return f"constant Y={anchor_y(h['value'], dim)}"
    lo = anchor_y(h["min_inclusive"], dim)
    hi = anchor_y(h["max_inclusive"], dim)
    if t == "uniform":
        return f"uniform {lo}..{hi}"
    if t == "trapezoid":
        mid = (lo + hi) / 2
        pl = h.get("plateau", 0)
        return f"trapezoid {lo}..{hi}, peak ~Y={mid:g} (plateau {pl})"
    if t in ("biased_to_bottom", "very_biased_to_bottom"):
        return f"{t.replace('_', ' ')} {lo}..{hi}"
    return f"{t} {lo}..{hi}"


def count_str(p):
    c = p.get("count")
    if c is None:
        return None
    if isinstance(c, int):
        return f"{c}/chunk"
    t = short(c.get("type"))
    if t == "uniform":
        return f"{c['min_inclusive']}..{c['max_inclusive']}/chunk (uniform)"
    if t == "weighted_list":
        return "weighted " + "/".join(str(d["data"]) for d in c["distribution"]) + "/chunk"
    if t in ("biased_to_bottom",):
        return f"{c['min_inclusive']}..{c['max_inclusive']}/chunk (biased low)"
    if t == "clamped":
        return f"clamped {c['min_inclusive']}..{c['max_inclusive']}/chunk"
    return f"{t}/chunk"


def placement_summary(pl_list, dim):
    count = None
    rarity = None
    height = None
    heightmap = None
    ceiling = None
    extras = []
    every_layer = False
    for p in pl_list:
        t = short(p.get("type"))
        if t == "count":
            count = count_str(p)
        elif t == "count_on_every_layer":
            count = f"{p['count']}/layer"
            every_layer = True
        elif t == "noise_based_count":
            count = "noise-based"
        elif t == "noise_threshold_count":
            count = "noise threshold"
        elif t == "rarity_filter":
            rarity = f"1/{p['chance']}"
        elif t == "height_range":
            height = height_str(p, dim)
        elif t == "heightmap":
            heightmap = short(p.get("heightmap"))
        elif t == "environment_scan":
            if short(p.get("direction_of_search")) == "up":
                ceiling = "ceiling-attached"
            elif short(p.get("direction_of_search")) == "down":
                extras.append("floor-scan")
        elif t == "fixed_placement":
            height = "fixed " + ", ".join(str(q) for q in p["positions"])
    if count is None and rarity is None:
        count = "1 try"
    cnt = " x ".join(x for x in [count, (f"chance {rarity}" if rarity else None)] if x)
    if height is None:
        if heightmap:
            height = f"surface (heightmap {heightmap})"
        elif every_layer:
            bottom, top = DIM_Y[dim]
            height = f"every layer {bottom}..{top} (no height modifier)"
        else:
            height = "no Y modifier (surface-relative)"
    if ceiling:
        height += " [ceiling-attached]"
    return cnt, height


# ---------- ore rows ----------
def ore_row(pname, pl_list, cfg, dim):
    targets = cfg.get("targets", [])
    states = []
    replaces = []
    gated = False
    for t in targets:
        st = t.get("state")
        st = short(st) if isinstance(st, str) else short(st.get("Name"))
        if st not in states:
            states.append(st)
        acc, flags = [], set()
        pred_tags(t.get("target", {}), acc, flags)
        gated = gated or ("y-gated" in flags)
        for a in dict.fromkeys(acc):
            lab = tag_label(a) if a.startswith("#") else a
            if lab not in replaces:
                replaces.append(lab)
    rep = "; ".join(replaces) if replaces else "—"
    if gated:
        rep += " (height_specific tags only inside their Y gate)"
    size = cfg.get("size")
    disc = cfg.get("discard_chance_on_air_exposure")
    cnt, height = placement_summary(pl_list, dim)
    return {
        "blocks": ", ".join(states),
        "replaces": rep,
        "vein": str(size),
        "discard": f"{disc:g}" if isinstance(disc, (int, float)) else "—",
        "count": cnt,
        "height": height,
    }


def disk_row(pname, pl_list, cfg, dim):
    sp = cfg.get("state_provider", {})
    st = []

    def collect(x):
        if isinstance(x, dict):
            if "id" in x:
                st.append(short(x["id"]))
            for v in x.values():
                collect(v)
        elif isinstance(x, list):
            for v in x:
                collect(v)
    collect(sp)
    tgt = cfg.get("target", {})
    acc, flags = [], set()
    pred_tags(tgt, acc, flags)
    r = cfg.get("radius")
    if isinstance(r, dict):
        rad = f"{r['min_inclusive']}..{r['max_inclusive']}"
    else:
        rad = str(r)
    cnt, height = placement_summary(pl_list, dim)
    return {
        "blocks": ", ".join(dict.fromkeys(st)),
        "replaces": ", ".join(dict.fromkeys(acc)) or "—",
        "vein": f"radius {rad}, half-height {cfg.get('half_height')}",
        "discard": "—",
        "count": cnt,
        "height": height,
    }


def blob_row(pname, pl_list, cfg, dim):
    r = cfg.get("radius", {})
    rad = f"{r['min_inclusive']}..{r['max_inclusive']}" if isinstance(r, dict) else str(r)
    cnt, height = placement_summary(pl_list, dim)
    return {
        "blocks": short(cfg.get("state")),
        "replaces": short(cfg.get("target")),
        "vein": f"radius {rad}",
        "discard": "—",
        "count": cnt,
        "height": height,
    }


# ---------- manual rows for code-defined / structure / complex features ----------
MANUAL = {
    "amethyst_geode": dict(blocks="amethyst_block, budding_amethyst, calcite, smooth_basalt (outer), amethyst buds/clusters, air core",
                           replaces="not #features_cannot_replace (geode shell)", vein="outer wall radius 4..6"),
    "basalt_pillar": dict(blocks="basalt (column)", replaces="air", vein="single-block pillars"),
    "blue_ice": dict(blocks="blue_ice", replaces="code-defined (no JSON targets)", vein="—"),
    "iceberg_blue": dict(blocks="blue_ice", replaces="code-defined (placed on water, no JSON targets)", vein="—"),
    "iceberg_packed": dict(blocks="packed_ice", replaces="code-defined (placed on water, no JSON targets)", vein="—"),
    "delta": dict(blocks="lava (core), magma_block (rim)", replaces="code-defined (nether base blocks)", vein="size 3..7, rim 0..2"),
    "dripstone_cluster": dict(blocks="dripstone_block (base), pointed_dripstone", replaces="#dripstone_replaceable_blocks", vein="radius 2..8, height 3..6"),
    "large_dripstone": dict(blocks="dripstone_block, pointed_dripstone", replaces="#dripstone_replaceable_blocks", vein="column radius 3..16"),
    "sulfur_spike_cluster": dict(blocks="sulfur (base), sulfur_spike", replaces="#sulfur_spike_replaceable_blocks", vein="radius 2..8, height 1..4"),
    "end_island_decorated": dict(blocks="end_stone (code-defined island)", replaces="air (code-defined)", vein="—"),
    "end_spike": dict(blocks="obsidian (pillars; spikes list empty in JSON, generated at runtime)", replaces="air (code-defined)", vein="—"),
    "end_platform": dict(blocks="obsidian (fixed platform)", replaces="air (code-defined)", vein="—"),
    "forest_rock": dict(blocks="mossy_cobblestone", replaces="surface on #forest_rock_can_place_on", vein="—"),
    "fossil_lower": dict(blocks="bone_block (template skeleton); coal/diamond overlay", replaces="stone-type (template processors)", vein="template"),
    "fossil_upper": dict(blocks="bone_block (template skeleton); coal overlay", replaces="stone-type (template processors)", vein="template"),
    "freeze_top_layer": dict(blocks="snow layer, ice over water", replaces="surface (code-defined)", vein="—"),
    "glowstone": dict(blocks="glowstone", replaces="air on ceilings (neighbour spread)", vein="attempts 1500, xz +-7, y -11..0"),
    "glowstone_extra": dict(blocks="glowstone", replaces="air on ceilings (neighbour spread)", vein="neighbour spread"),
    "ice_spike": dict(blocks="packed_ice", replaces="#ice_spike_replaceable (placed on snow_block)", vein="spike"),
    "lake_lava_surface": dict(blocks="lava (fluid), stone (barrier)", replaces="air/fluid; not #features_cannot_replace", vein="lake basin"),
    "lake_lava_underground": dict(blocks="lava (fluid), stone (barrier)", replaces="air/fluid; not #features_cannot_replace", vein="lake basin"),
    "sulfur_pool": dict(blocks="water (fluid), sulfur (barrier)", replaces="air/fluid; not #features_cannot_replace", vein="lake basin"),
    "large_basalt_columns": dict(blocks="basalt (stepped columns)", replaces="air or lava (above sea level)", vein="50 columns, height 5..10"),
    "small_basalt_columns": dict(blocks="basalt (stepped columns)", replaces="air or lava (above sea level)", vein="small clusters"),
    "lush_caves_ceiling_vegetation": dict(blocks="moss_block (ceiling), cave_vine", replaces="#moss_replaceable", vein="xz radius 4..7, depth 1..2"),
    "lush_caves_vegetation": dict(blocks="moss_block (floor), moss_carpet", replaces="#moss_replaceable", vein="xz radius 4..7, depth 1"),
    "pale_moss_patch": dict(blocks="pale_moss_block (floor), pale moss carpet", replaces="#moss_replaceable", vein="xz radius 2..4, depth 1"),
    "lush_caves_clay": dict(blocks="clay (floor); dripleaf plants on top (not terrain)", replaces="#lush_ground_replaceable", vein="xz radius 4..7, depth 3 (waterlogged variant: vertical range 5)"),
    "underwater_magma": dict(blocks="magma_block", replaces="seafloor (code-defined, search range 5, p=0.5)", vein="—"),
    "mushroom_island_vegetation": dict(blocks="huge_red_mushroom or huge_brown_mushroom (50/50)", replaces="#huge_*_mushroom_can_place_on", vein="foliage radius 3"),
    "crimson_fungi": dict(blocks="nether_wart_block (hat), shroomlight (decor), crimson stem", replaces="replaceable_blocks list (saplings/plants)", vein="huge fungus"),
    "warped_fungi": dict(blocks="warped_wart_block (hat), shroomlight (decor), warped stem", replaces="replaceable_blocks list (saplings/plants)", vein="huge fungus"),
    "pile_ice": dict(blocks="blue_ice (1) / packed_ice (5) block pile", replaces="air (surface)", vein="1 block high"),
    "pile_snow": dict(blocks="snow (layers=1)", replaces="air (surface)", vein="1 layer"),
    "pile_hay": dict(blocks="hay_block (axis y)", replaces="air (surface)", vein="1 block"),
    "sculk_patch_ancient_city": dict(blocks="sculk, sculk_catalyst (50%)", replaces="code-defined sculk spread", vein="charges 10, 32/charge, 64 spread attempts"),
    "sculk_patch_deep_dark": dict(blocks="sculk, sculk_catalyst (50%)", replaces="code-defined sculk spread", vein="charges 10, 32/charge, 64 spread attempts"),
}

# Features whose tag/placement data is not used by the table (fluid-only springs), listed separately.
FLUID_ONLY = {"spring_closed", "spring_closed_double", "spring_delta", "spring_lava", "spring_lava_frozen", "spring_open", "spring_water"}

INCLUDE_NON_ORE = [
    "amethyst_geode", "basalt_blobs", "blackstone_blobs", "basalt_pillar", "blue_ice", "iceberg_blue", "iceberg_packed",
    "delta", "dripstone_cluster", "large_dripstone", "sulfur_spike_cluster", "end_island_decorated", "end_spike",
    "end_platform", "forest_rock", "fossil_lower", "fossil_upper", "freeze_top_layer", "glowstone", "glowstone_extra",
    "ice_spike", "lake_lava_surface", "lake_lava_underground", "sulfur_pool", "large_basalt_columns",
    "small_basalt_columns", "lush_caves_ceiling_vegetation", "lush_caves_vegetation", "pale_moss_patch",
    "lush_caves_clay", "underwater_magma", "mushroom_island_vegetation", "crimson_fungi", "warped_fungi",
    "pile_ice", "pile_snow", "pile_hay", "sculk_patch_ancient_city", "sculk_patch_deep_dark",
    "disk_clay", "disk_gravel", "disk_grass", "disk_sand", "ice_patch",
]


def get_cfg(pl):
    ref = short(pl["feature"])
    c = cf.get(ref)
    return ref, c


def build_rows():
    rows = []
    ore_names = sorted(n for n in pf if n.startswith("ore_"))
    for pname in ore_names + INCLUDE_NON_ORE:
        if pname not in pf:
            print("MISSING placed feature:", pname, file=sys.stderr)
            continue
        pl = pf[pname]
        ref, c = get_cfg(pl)
        if c is None:
            print("MISSING configured:", pname, ref, file=sys.stderr)
            continue
        # dimension from biome membership (direct listing)
        bl = feat_biomes.get(pname, set())
        dims = sorted({BIOME_DIM[b] for b in bl})
        dim = dims[0] if len(dims) == 1 else ("overworld" if "overworld" in dims else (dims[0] if dims else "overworld"))
        ctype = short(c.get("type"))
        if pname.startswith("ore_") and ctype in ("ore", "scattered_ore"):
            cells = ore_row(pname, pl["placement"], c, dim)
        elif ctype == "disk":
            cells = disk_row(pname, pl["placement"], c, dim)
        elif ctype == "netherrack_replace_blobs":
            cells = blob_row(pname, pl["placement"], c, dim)
        else:
            m = MANUAL.get(pname)
            if m is None:
                print("NO MANUAL for", pname, ctype, file=sys.stderr)
                continue
            cnt, height = placement_summary(pl["placement"], dim)
            cells = {"blocks": m["blocks"], "replaces": m["replaces"], "vein": m["vein"], "discard": "—",
                     "count": cnt, "height": height}
        rows.append((pname, dim, bl, cells, ctype))
    return rows


def biome_str(dim, bl):
    if not bl:
        return "not in any biome (structure/feature-only)"
    same = sorted(b for b in bl if BIOME_DIM[b] == dim)
    n = len(same)
    total = DIM_TOTAL[dim]
    if n == total:
        return f"all {dim} biomes ({n})"
    if n >= 0.8 * total:
        return f"most {dim} biomes ({n}/{total})"
    if n <= 6:
        return f"{dim}: " + ", ".join(s for s in same)
    return f"{n}/{total} {dim} biomes (e.g. " + ", ".join(same[:3]) + ")"


def main():
    rows = build_rows()
    lines = []
    lines.append("# Worldgen terrain features - Minecraft Java 26.3 (vanilla data)")
    lines.append("")
    lines.append("Source: `data/minecraft/worldgen/feature/*.json` (configured features; this version's folder is `feature/`, not `configured_feature/`), `placed_feature/*.json`, `biome/*.json`, from the 26.3 client jar. Biome dimension membership from the generator's `biome_parameters` reports.")
    lines.append("")
    lines.append("Y conversion: overworld bottom -64 / top 320; nether and end 0 / 256. `above_bottom: N` = bottom+N, `below_top: N` = top-N. Heightmap placements have no fixed Y and are listed as surface.")
    lines.append("")
    lines.append("Note: `top` here is the exclusive end of the height range (min_y + height). The highest placeable block Y is 319 in the overworld and 255 in the nether and end, so a `below_top: 0` bound maps to 319/255 in-game.")
    lines.append("")
    lines.append("Columns: count = `count` placement modifier (tries per chunk); chance = `rarity_filter` 1-in-N per chunk. Vein size is the configured `size` (ore/scattered_ore). Discard = `discard_chance_on_air_exposure`.")
    lines.append("")
    lines.append("Items marked `code-defined` have no block/target data in the JSON; values are from the feature type and are not checked against source code.")
    lines.append("")
    for dim in ("overworld", "nether", "end", "other"):
        sub = [r for r in rows if r[1] == dim]
        if not sub:
            continue
        lines.append(f"## {dim.capitalize()}")
        lines.append("")
        lines.append("| Placed feature | Configured type | Block(s) placed | Replaces (rule test target) | Vein size | Air-exposure discard | Count / chance per chunk | Height (Y) | Biomes using it |")
        lines.append("|---|---|---|---|---|---|---|---|---|")
        for pname, d, bl, cells, ctype in sorted(sub, key=lambda r: (r[4], r[0])):
            lines.append("| `{}` | {} | {} | {} | {} | {} | {} | {} | {} |".format(
                pname, ctype, cells["blocks"], cells["replaces"], cells["vein"], cells["discard"],
                cells["count"], cells["height"], biome_str(d, bl)))
        lines.append("")
    lines.append("## Not in the table")
    lines.append("")
    lines.append("- Trees, fallen trees, bamboo, flowers, grass/fern/patches, mushrooms (small), melons/pumpkins, cactus, sugar cane, kelp, seagrass, vines, berry bushes, sea pickles, chorus, multiface growth, root systems and bonus/structure features (monster room, end gateway/podium, desert well).")
    lines.append("- Fluid-only springs (`spring_water`, `spring_lava`, `spring_closed`, `spring_closed_double`, `spring_open`, `spring_delta`, `spring_lava_frozen`): they place water/lava source blocks into rock; not terrain blocks.")
    lines.append("- Dark forest vegetation (`dark_forest_vegetation`) mixes trees with huge brown/red mushrooms; the mushrooms are listed only via `mushroom_island_vegetation`.")
    lines.append("")
    # dimension types
    lines.append("## Dimension types (`data/minecraft/dimension_type`)")
    lines.append("")
    lines.append("In 26.x the dimension-type JSON has no `ultrawarm` key; that behaviour is an environment attribute (`minecraft:gameplay/water_evaporates`, `minecraft:gameplay/fast_lava`).")
    lines.append("")
    lines.append("| Dimension type | min_y | height | logical_height | coordinate_scale | has_skylight | has_ceiling | water_evaporates attr |")
    lines.append("|---|---|---|---|---|---|---|---|")
    for k in sorted(dimtypes):
        v = dimtypes[k]
        we = v.get("attributes", {}).get("minecraft:gameplay/water_evaporates")
        lines.append(f"| {k} | {v.get('min_y')} | {v.get('height')} | {v.get('logical_height')} | {v.get('coordinate_scale')} | {v.get('has_skylight')} | {v.get('has_ceiling')} | {we} |")
    lines.append("")
    OUT_MD.parent.mkdir(parents=True, exist_ok=True)
    OUT_MD.write_text("\n".join(lines) + "\n", encoding="utf-8")
    print("rows:", len(rows), "written:", OUT_MD)
    print("fluid-only (excluded):", sorted(FLUID_ONLY))


if __name__ == "__main__":
    main()
