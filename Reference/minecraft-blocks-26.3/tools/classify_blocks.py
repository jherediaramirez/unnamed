"""Classify vanilla Minecraft block models in a client jar as full cubes or slabs.

Usage (from anywhere):
    uv run --no-project --python 3.13 python -I classify_blocks.py [JAR] [OUT_DIR]

Reads assets/minecraft/blockstates, models and textures straight out of the jar
with zipfile (nothing is extracted). Writes only:
    OUT_DIR\\blocks_assets.csv
    OUT_DIR\\edge_cases.txt
"""

import csv
import io
import json
import os
import sys
import zipfile

from PIL import Image

DEFAULT_JAR = r"C:\Users\jherediaramirez\AppData\Roaming\ModrinthApp\meta\versions\26.3\26.3.jar"
DEFAULT_OUT = r"C:\dev\sandbox\mc-26.3-blocks\out"
DEFAULT_TEX_ROOT = r"C:\dev\sandbox\mc-26.3-blocks\textures"
# extra textures requested for the importer (copied if present)
EXTRA_TEXTURES = [
    "assets/minecraft/textures/block/water_still.png",
    "assets/minecraft/textures/block/water_flow.png",
    "assets/minecraft/textures/block/water_overlay.png",
    "assets/minecraft/textures/colormap/grass.png",
    "assets/minecraft/textures/colormap/foliage.png",
]

ASSETS = "assets/minecraft/"
DIRS = ("down", "up", "north", "south", "east", "west")
TEMPLATES = (
    "cube_all", "cube_column", "cube_column_horizontal", "cube_bottom_top", "cube_top",
    "cube", "cube_mirrored_all", "cube_north_west_mirrored_all", "cube_all_inner_faces",
    "orientable", "orientable_with_bottom", "orientable_vertical",
    "leaves", "slab", "slab_top",
)
TEMPLATE_SET = set(TEMPLATES)
SLAB_PARENTS = {"block/slab", "block/slab_top"}
COLUMNS = ["id", "shape", "template", "faces", "textures", "state_props",
           "random_rotation", "tinted", "animated", "model_count",
           "render_layer", "tex_size"]
LAYER_RANK = {"opaque": 0, "cutout": 1, "translucent": 2}

ZF = None
NAMES = set()
MODEL_CACHE = {}
WARNINGS = []
PNG_CACHE = {}


def warn(msg):
    WARNINGS.append(msg)


def norm(ref):
    """'minecraft:block/stone' -> 'block/stone'. Other namespaces are flagged."""
    if ":" in ref:
        ns, path = ref.split(":", 1)
        if ns != "minecraft":
            warn(f"non-minecraft namespace in ref: {ref}")
        return path
    return ref


def read_json(path):
    return json.loads(ZF.read(path).decode("utf-8"))


def model_chain(mid):
    """[(model_id, json_or_None), ...] from mid up the parent chain.

    Stops at builtin/* (kept as a terminal entry with json None) or at a model with no parent.
    """
    if mid in MODEL_CACHE:
        return MODEL_CACHE[mid]
    chain = []
    seen = set()
    cur = mid
    while cur is not None:
        if cur.startswith("builtin/"):
            chain.append((cur, None))
            break
        if cur in seen:
            warn(f"parent cycle at {cur} (from {mid})")
            break
        seen.add(cur)
        path = f"{ASSETS}models/{cur}.json"
        if path not in NAMES:
            warn(f"missing model file {cur} (from {mid})")
            chain.append((cur, None))
            break
        data = read_json(path)
        chain.append((cur, data))
        cur = norm(data["parent"]) if "parent" in data else None
    MODEL_CACHE[mid] = chain
    return chain


def model_info(mid):
    chain = model_chain(mid)
    elements = None
    for _, d in chain:
        if d is not None and "elements" in d:
            elements = d["elements"]  # nearest ancestor (or self) that has elements
            break
    merged = {}  # child-over-parent texture map
    for _, d in chain:
        if d is None:
            continue
        for k, v in d.get("textures", {}).items():
            merged.setdefault(k, v)
    return {
        "id": mid,
        "chain": [c for c, _ in chain],
        "root": chain[-1][0],
        "elements": elements or [],
        "textures": merged,
    }


def is_full_element(el):
    return el.get("from") == [0, 0, 0] and el.get("to") == [16, 16, 16]


def is_full_model(info):
    return bool(info["elements"]) and all(is_full_element(e) for e in info["elements"])


def is_slab_model(info):
    return any(c in SLAB_PARENTS for c in info["chain"])


def resolve_tex(value, merged, stack=()):
    """Resolve a texture value to a 'block/x' style path, following #vars. None if unresolved.

    26.x also allows object form: {"sprite": "minecraft:block/x", "force_translucent": true}.
    """
    if isinstance(value, dict):
        value = value.get("sprite")
    if not value or not isinstance(value, str):
        return None
    if value.startswith("#"):
        key = value[1:]
        if key in stack or key not in merged:
            return None
        return resolve_tex(merged[key], merged, stack + (key,))
    return norm(value)


def tex_jar(p):
    return f"{ASSETS}textures/{p}.png"


def is_rotated(entry):
    return entry.get("x", 0) % 360 != 0 or entry.get("y", 0) % 360 != 0


def collect_when_keys(cond, out):
    if isinstance(cond, dict):
        for k, v in cond.items():
            if k in ("OR", "AND"):
                for sub in (v if isinstance(v, list) else [v]):
                    collect_when_keys(sub, out)
            elif k not in out:
                out.append(k)
    elif isinstance(cond, list):
        for sub in cond:
            collect_when_keys(sub, out)


def analyze_blockstate(bid, data):
    """Return (refs, state_props, random_rotation, random_multipart, multipart).

    refs is a list of (normalized model id, rotated?) in document order.
    """
    props = []
    refs = []
    random_rot = False
    random_mp = False
    multipart = False
    if "variants" in data:
        for key, val in data["variants"].items():
            for prop in (key.split(",") if key else []):
                name = prop.split("=", 1)[0]
                if name not in props:
                    props.append(name)
            entries = val if isinstance(val, list) else [val]
            if isinstance(val, list) and len(val) > 1:
                random_rot = True
            for e in entries:
                refs.append((norm(e["model"]), is_rotated(e), key))
    elif "multipart" in data:
        multipart = True
        for part in data["multipart"]:
            if "when" in part:
                keys = []
                collect_when_keys(part["when"], keys)
                for k in keys:
                    if k not in props:
                        props.append(k)
            apply = part["apply"]
            entries = apply if isinstance(apply, list) else [apply]
            if isinstance(apply, list) and len(apply) > 1:
                random_mp = True
            for e in entries:
                refs.append((norm(e["model"]), is_rotated(e), ""))
    else:
        warn(f"blockstate {bid} has neither variants nor multipart")
    return refs, props, random_rot, random_mp, multipart


DIR6 = {"up", "down", "north", "south", "east", "west"}


def is_per_face_multipart(data, models, infos):
    """Multipart block whose six face-keyed parts are all single-face quads."""
    if "multipart" not in data:
        return False
    keys = set()
    for part in data["multipart"]:
        when = part.get("when")
        if isinstance(when, dict):
            keys.update(k for k in when if k not in ("OR", "AND"))
    return DIR6 <= keys and all(
        any(c == "block/template_single_face" for c in infos[m]["chain"]) for m in models)


def png_info(tex_path):
    """(render_layer, 'WxH') for a texture PNG read from the jar, from its alpha channel.

    opaque: alpha is all 255 (or no alpha); cutout: alpha only 0/255; translucent: any partial alpha.
    """
    if tex_path not in PNG_CACHE:
        with Image.open(io.BytesIO(ZF.read(tex_path))) as im:
            w, h = im.size
            alpha = im.convert("RGBA").getchannel("A")
            present = {i for i, c in enumerate(alpha.histogram()) if c}
        if present <= {255}:
            layer = "opaque"
        elif present <= {0, 255}:
            layer = "cutout"
        else:
            layer = "translucent"
        PNG_CACHE[tex_path] = (layer, f"{w}x{h}")
    return PNG_CACHE[tex_path]


def copy_jar_file(jar_path, tex_root):
    """Copy one jar file to tex_root, mirroring its path under assets/minecraft/textures/."""
    rel = jar_path[len(ASSETS + "textures/"):]  # e.g. block/stone.png
    dest = os.path.join(tex_root, *rel.split("/"))
    os.makedirs(os.path.dirname(dest), exist_ok=True)
    with open(dest, "wb") as f:
        f.write(ZF.read(jar_path))


def has_real_animation(tex_path, names, zf):
    """True only if the texture's .mcmeta has an 'animation' section (not just 'texture' metadata)."""
    mc = tex_path + ".mcmeta"
    if mc not in names:
        return False
    return "animation" in json.loads(zf.read(mc).decode("utf-8"))


def face_paths(info):
    """Per-direction jar paths taken from the first element that has that face."""
    out = {d: "" for d in DIRS}
    found = {d: False for d in DIRS}
    for el in info["elements"]:
        for d, face in (el.get("faces") or {}).items():
            if d not in out or found[d]:
                continue
            p = resolve_tex(face.get("texture"), info["textures"])
            if p is None:
                warn(f"unresolved face texture {face.get('texture')} in {info['id']} ({d})")
                continue
            out[d] = tex_jar(p)
            found[d] = True
    return out


def bbox_info(info):
    els = info["elements"]
    if not els:
        return None
    lo = [min(e["from"][i] for e in els) for i in range(3)]
    hi = [max(e["to"][i] for e in els) for i in range(3)]
    span = [hi[i] - lo[i] for i in range(3)]
    vol_sum = 0.0
    for e in els:
        vol_sum += (e["to"][0] - e["from"][0]) * (e["to"][1] - e["from"][1]) * (e["to"][2] - e["from"][2])
    bbox_vol = span[0] * span[1] * span[2]
    return {"lo": lo, "hi": hi, "span": span, "fill": (vol_sum / bbox_vol) if bbox_vol else 0.0}


def main():
    global ZF, NAMES
    jar = sys.argv[1] if len(sys.argv) > 1 else DEFAULT_JAR
    out_dir = sys.argv[2] if len(sys.argv) > 2 else DEFAULT_OUT
    os.makedirs(out_dir, exist_ok=True)

    ZF = zipfile.ZipFile(jar, "r")
    NAMES = set(ZF.namelist())
    mcmeta = {n[: -len(".mcmeta")] for n in NAMES if n.endswith(".png.mcmeta")}
    anim_mismatch = []  # animated=true per spec rule, but no 'animation' section in any mcmeta

    bs_prefix = ASSETS + "blockstates/"
    block_ids = sorted(n[len(bs_prefix):-len(".json")] for n in NAMES
                       if n.startswith(bs_prefix) and n.endswith(".json"))

    rows = []
    rejected = []  # (id, category, reason)
    template_counts = {}
    all_random_mp = []
    all_multipart_kept = []
    unresolved_ids = []
    block_textures = {}
    layer_counts = {}
    force_translucent_ids = []

    for bid in block_ids:
        data = read_json(f"{bs_prefix}{bid}.json")
        refs, props, random_rot, random_mp, multipart = analyze_blockstate(bid, data)
        models = sorted({m for m, _, _ in refs})
        infos = {m: model_info(m) for m in models}
        n = len(models)
        n_full = sum(1 for m in models if is_full_model(infos[m]))
        any_slab = any(is_slab_model(infos[m]) for m in models)

        if n and n_full == n:
            shape = "full_cube"
        elif bid.endswith("_slab") and any_slab:
            shape = "slab"
        else:
            shape = None

        if shape is None:
            # classify the reason for the edge-case report
            if bid.endswith("_slab"):
                rejected.append((bid, "slab_name_no_slab_geometry",
                                 "ends in _slab but no model chain has block/slab or block/slab_top: "
                                 + ", ".join(models)))
            elif not any(infos[m]["elements"] for m in models):
                roots = sorted({infos[m]["root"] for m in models})
                rejected.append((bid, "no_elements",
                                 f"no element geometry in any model (root: {', '.join(roots)})"))
            elif 0 < n_full < n:
                non_full = [m for m in models if not is_full_model(infos[m])]
                rejected.append((bid, "mixed_states",
                                 f"{n_full}/{n} models are full cubes; non-cube: {', '.join(non_full)}"))
            elif is_per_face_multipart(data, models, infos):
                rejected.append((bid, "per_face_multipart",
                                 "multipart of six single-face parts (template_single_face); "
                                 "reads as a cube only when all six faces are present: "
                                 + ", ".join(models)))
            else:
                full_plus = [m for m in models
                             if any(is_full_element(e) for e in infos[m]["elements"])
                             and len(infos[m]["elements"]) > 1]
                if full_plus:
                    extra = max(len(infos[m]["elements"]) - 1 for m in full_plus)
                    rejected.append((bid, "full_element_plus_inner",
                                     f"full-size element plus up to {extra} other element(s) in: "
                                     + ", ".join(full_plus)))
                else:
                    cube_like = []
                    for m in models:
                        bb = bbox_info(infos[m])
                        if bb and min(bb["span"]) >= 15 and bb["fill"] >= 0.9:
                            cube_like.append(f"{m} lo={bb['lo']} hi={bb['hi']}")
                    if cube_like and len(cube_like) == n:
                        rejected.append((bid, "near_full_cube",
                                         "bbox spans >=15/16 on every axis, not exact 0..16: "
                                         + "; ".join(cube_like)))
            continue

        # ---- kept block ----
        if multipart:
            all_multipart_kept.append(bid)
        if random_mp:
            all_random_mp.append(bid)

        # default model: prefer unrotated entries, then a vertical axis=y state (so bamboo/cherry
        # logs get their end-grain on up/down), then document order. Falls back to the first entry.
        def default_rank(item):
            idx, (_, rot, key) = item
            return (rot, 0 if "axis=y" in key.split(",") else 1, idx)
        default_mid = min(enumerate(refs), key=default_rank)[1][0]
        dinfo = infos[default_mid]

        template = "custom"
        for c in dinfo["chain"]:
            if c.startswith("block/") and c[len("block/"):] in TEMPLATE_SET:
                template = c[len("block/"):]
                break
        template_counts[template] = template_counts.get(template, 0) + 1

        fp = face_paths(dinfo)
        faces = ";".join(f"{d}={fp[d]}" for d in DIRS)

        tex_set = set()
        tinted = False
        for m in models:
            info = infos[m]
            for k, v in info["textures"].items():
                p = resolve_tex(v, info["textures"])
                if p is None:
                    unresolved_ids.append((bid, m, k, v))
                    continue
                tex_set.add(tex_jar(p))
            for el in info["elements"]:
                for face in (el.get("faces") or {}).values():
                    if "tintindex" in face:
                        tinted = True
        textures = sorted(tex_set)
        for t in textures:
            if t not in NAMES:
                warn(f"texture png missing from jar: {t} (block {bid})")
        animated = any(t in mcmeta for t in textures)
        if animated and not any(has_real_animation(t, NAMES, ZF) for t in textures):
            anim_mismatch.append(bid)

        # render layer: worst alpha class across all textures; tex_size: default face texture
        layer = "opaque"
        for t in textures:
            tl, _ = png_info(t)
            if LAYER_RANK[tl] > LAYER_RANK[layer]:
                layer = tl
        default_face = next(fp[d] for d in DIRS if fp[d])
        tex_size = png_info(default_face)[1]
        block_textures[bid] = textures
        if any(isinstance(v, dict) and v.get("force_translucent")
               for m in models for v in infos[m]["textures"].values()):
            force_translucent_ids.append(bid)
        layer_counts[layer] = layer_counts.get(layer, 0) + 1

        rows.append({
            "id": bid,
            "shape": shape,
            "template": template,
            "faces": faces,
            "textures": ";".join(textures),
            "state_props": ",".join(props),
            "random_rotation": str(random_rot).lower(),
            "tinted": str(tinted).lower(),
            "animated": str(animated).lower(),
            "model_count": str(n),
            "render_layer": layer,
            "tex_size": tex_size,
        })

    rows.sort(key=lambda r: r["id"])

    csv_path = os.path.join(out_dir, "blocks_assets.csv")
    with open(csv_path, "w", newline="", encoding="utf-8") as f:
        w = csv.writer(f, lineterminator="\n")
        w.writerow(COLUMNS)
        for r in rows:
            w.writerow([r[c] for c in COLUMNS])

    # ---- stage textures under DEFAULT_TEX_ROOT (paths mirror assets/minecraft/textures/) ----
    wanted = set()
    for r in rows:
        wanted.update(block_textures[r["id"]])
    extra_present = [p for p in EXTRA_TEXTURES if p in NAMES]
    extra_missing = [p for p in EXTRA_TEXTURES if p not in NAMES]
    wanted.update(extra_present)
    png_copied = 0
    mcmeta_copied = 0
    copied_by_dir = {}
    for p in sorted(wanted):
        if p not in NAMES:
            warn(f"requested texture not in jar: {p}")
            continue
        copy_jar_file(p, DEFAULT_TEX_ROOT)
        png_copied += 1
        sub = p.split("/")[3]  # 'block' or 'colormap'
        copied_by_dir[sub] = copied_by_dir.get(sub, 0) + 1
        if p + ".mcmeta" in NAMES:
            copy_jar_file(p + ".mcmeta", DEFAULT_TEX_ROOT)
            mcmeta_copied += 1

    # ---- edge cases ----
    kept_ids = {r["id"] for r in rows}
    order = ["mixed_states", "full_element_plus_inner", "per_face_multipart", "near_full_cube",
             "slab_name_no_slab_geometry", "no_elements"]
    by_cat = {c: [] for c in order}
    for bid, cat, reason in rejected:
        assert bid not in kept_ids
        by_cat.setdefault(cat, []).append((bid, reason))

    lines = []
    for cat in order:
        items = sorted(by_cat.get(cat, []))
        if not items:
            continue
        if cat == "no_elements":
            # one line per distinct root model family, ids grouped
            groups = {}
            for bid, reason in items:
                groups.setdefault(reason, []).append(bid)
            for reason, ids in sorted(groups.items()):
                lines.append(f"no_elements: {', '.join(ids)} -- {reason}")
        else:
            for bid, reason in items:
                lines.append(f"{bid} -- {cat}: {reason}")

    edge_path = os.path.join(out_dir, "edge_cases.txt")
    with open(edge_path, "w", encoding="utf-8", newline="\n") as f:
        f.write("\n".join(lines) + "\n")

    # ---- report to stdout (not written to disk) ----
    shape_counts = {}
    for r in rows:
        shape_counts[r["shape"]] = shape_counts.get(r["shape"], 0) + 1
    print(f"blockstates in jar: {len(block_ids)}")
    print(f"kept rows: {len(rows)} shapes={shape_counts}")
    print(f"templates (kept, default model): {dict(sorted(template_counts.items(), key=lambda kv: -kv[1]))}")
    print(f"rejected: {len(rejected)} by category: "
          + str({c: len(v) for c, v in by_cat.items() if v}))
    print(f"edge_cases lines: {len(lines)}")
    print(f"multipart blockstates among kept: {all_multipart_kept}")
    print(f"kept blocks with random multipart apply arrays: {all_random_mp}")
    print(f"animated=true with no 'animation' section (texture-only mcmeta): {len(anim_mismatch)}")
    print("   " + ", ".join(anim_mismatch))
    print(f"render_layer counts (kept): {dict(sorted(layer_counts.items()))}")
    print("   translucent: " + ", ".join(r["id"] for r in rows if r["render_layer"] == "translucent"))
    print("   cutout: " + ", ".join(r["id"] for r in rows if r["render_layer"] == "cutout"))
    print(f"kept blocks with force_translucent texture entries: {len(force_translucent_ids)}")
    print("   " + ", ".join(force_translucent_ids))
    print(f"textures staged under {DEFAULT_TEX_ROOT}: png={png_copied} mcmeta={mcmeta_copied} by_dir={copied_by_dir}")
    print(f"extra textures missing from jar: {extra_missing}")
    size_counts = {}
    for r in rows:
        size_counts[r["tex_size"]] = size_counts.get(r["tex_size"], 0) + 1
    print(f"tex_size counts (kept): {dict(sorted(size_counts.items(), key=lambda kv: -kv[1]))}")
    print(f"unresolved texture refs: {len(unresolved_ids)}")
    for u in unresolved_ids[:20]:
        print("   ", u)
    print(f"warnings: {len(WARNINGS)}")
    for w_ in sorted(set(WARNINGS))[:60]:
        print("   ", w_)
    print(f"csv: {csv_path}")
    print(f"edge: {edge_path}")


if __name__ == "__main__":
    main()
