"""Bake 26.3 block models into quads for the blocks drawn from their own models (shape "model").

Used by build_reference.py; no command line of its own. The model format follows minecraft.wiki "Model" and
"Blockstates definition" (26.3: shade_direction_override replaced shade, light_emission per element, element
rotation with rescale). Conventions this baker fixes, each our own reading of the published format:

- Coordinates are Mojang block space (x east, y up, z south) in blocks (0..1); quads keep FaceInfo's vertex order
  (build_reference.FACE_INFO) and the face UV with its rotation (build_reference.vertex_uv). A quad's facing is its
  declared face turned by the blockstate rotation; faces with no area are left out.
- An element rotation turns the element about its origin, right-handed about the positive axis. rescale stretches
  the two other axes by 1 / cos(angle), so a 45 degree cross spans the whole cell.
- The blockstate rotation (x, then y) turns the whole model about the cell centre (build_reference.rotate), and with
  it each face's cullface and shade direction.
- uvlock keeps every face's texture aligned with the world. A face's UV in model space is one of the 8 transforms of
  the canonical UV (build_reference.canonical_uv, transform_uv); after the blockstate rotation it shows the same
  transform of the canonical UV of its final direction. Faces of rotated elements never use it.
- Random variant lists bake every entry (all equal weight in the 83 blocks); multipart states are the sum of their
  parts. A state is therefore a list of parts, each a list of alternatives, each a baked model index.
"""
import json
import math

FACES = ("down", "up", "north", "south", "west", "east")


class Baker:
    def __init__(self, ref, models):
        # ref: the build_reference module (FACE_INFO, NORMALS, default_uv, vertex_uv, canonical_uv, rotate,
        # direction_of); models: its Models reader over the client jar.
        self.ref, self.models = ref, models
        self.baked, self.index = [], {}

    def model_info(self, name):
        """(texture resolver, elements, ambient occlusion, gui display) with parent inheritance."""
        texture, elements, chain = self.models.resolve(name)
        ao, gui = None, None
        for link in chain:
            data = self.models.raw(link)
            if ao is None and "ambientocclusion" in data:
                ao = data["ambientocclusion"]
            if gui is None and "gui" in data.get("display", {}):
                gui = data["display"]["gui"]
        return texture, elements, True if ao is None else ao, gui

    def element_point(self, element, p):
        """An element vertex (0..16) after the element's own rotation, in blocks."""
        rotation = element.get("rotation")
        x, y, z = p
        if rotation and rotation.get("angle", 0):
            ox, oy, oz = rotation["origin"]
            angle = math.radians(rotation["angle"])
            c, s = math.cos(angle), math.sin(angle)
            scale = 1 / math.cos(angle) if rotation.get("rescale") else 1
            x, y, z = x - ox, y - oy, z - oz
            axis = rotation["axis"]
            if axis == "x":
                y, z = y * c - z * s, y * s + z * c
                y, z = y * scale, z * scale
            elif axis == "y":
                x, z = x * c + z * s, -x * s + z * c
                x, z = x * scale, z * scale
            else:
                x, y = x * c - y * s, x * s + y * c
                x, y = x * scale, y * scale
            x, y, z = x + ox, y + oy, z + oz
        return x / 16, y / 16, z / 16

    def bake_variant(self, variant, tints=frozenset()):
        """One blockstate model reference (model, x, y, uvlock) as a baked model index. Face tintindex values outside
        tints (the block's registered tint indices) are dropped to -1: such faces render untinted."""
        r = self.ref
        texture, elements, ao, _ = self.model_info(variant["model"])
        xr, yr, uvlock = variant.get("x", 0), variant.get("y", 0), variant.get("uvlock", False)
        assert xr % 90 == 0 and yr % 90 == 0 and not variant.get("z"), variant

        def turn(direction):
            return r.direction_of(tuple(a - .5 for a in r.rotate(tuple(.5 + c for c in r.NORMALS[direction]), xr, yr)))

        quads = []
        for element in elements:
            f, t = element["from"], element["to"]
            ext = {"x0": f[0], "x1": t[0], "y0": f[1], "y1": t[1], "z0": f[2], "z1": t[2]}
            for face_dir in FACES:
                face = element["faces"].get(face_dir)
                if face is None:
                    continue
                uvs = face.get("uv", r.default_uv(f, t, face_dir))
                shift = (face.get("rotation", 0) // 90) % 4
                verts = [r.rotate(self.element_point(element, tuple(ext[e] for e in r.FACE_INFO[face_dir][i])), xr, yr)
                         for i in range(4)]
                world = turn(face_dir)
                uv = [r.vertex_uv(uvs, shift, i) for i in range(4)]
                if uvlock and (xr or yr):
                    # The face's mapping in model space is one of the 8 transforms of the canonical UV (the vine's
                    # back face mirrors U); uvlock keeps that transform on the canonical UV of the final direction.
                    assert not element.get("rotation", {}).get("angle"), (variant, "uvlock on a rotated element")
                    local = [tuple(ext[e] / 16 for e in r.FACE_INFO[face_dir][i]) for i in range(4)]
                    canon = [r.canonical_uv(face_dir, p) for p in local]
                    code = next((c for c in range(8) if all(abs(a - b) < 1e-6 for p, q in zip(uv, canon)
                                                           for a, b in zip(p, r.transform_uv(c, *q)))), None)
                    assert code is not None, (variant, face_dir, "uvlock needs a whole-face mapping")
                    uv = [r.transform_uv(code, *r.canonical_uv(world, p)) for p in verts]
                # Faces of a flat element along its own thin axis have no area (the mangrove propagule's): skipped.
                a, b, c = verts[0], verts[1], verts[2]
                u, v = [b[i] - a[i] for i in range(3)], [c[i] - a[i] for i in range(3)]
                n = (u[1] * v[2] - u[2] * v[1], u[2] * v[0] - u[0] * v[2], u[0] * v[1] - u[1] * v[0])
                if sum(x * x for x in n) < 1e-12:
                    continue
                # The quad's facing is its declared face after the blockstate rotation: element rotations stay within
                # 45 degrees, so it is the axis nearest the normal (or one of two tied ones).
                facing = world
                override = element.get("shade_direction_override")
                quads.append({
                    "v": [[round(c, 6) for c in p] for p in verts],
                    "uv": [[round(c, 6) for c in p] for p in uv],
                    "t": texture(face["texture"]),
                    "tint": face.get("tintindex", -1) if face.get("tintindex", -1) in tints else -1,
                    "cull": turn(face["cullface"]) if "cullface" in face else None,
                    "facing": facing,
                    "shade": turn(override) if override else facing,
                    "light": element.get("light_emission", 0),
                })
        key = json.dumps([ao, quads], sort_keys=True)
        if key not in self.index:
            self.index[key] = len(self.baked)
            self.baked.append({"ao": ao, "quads": quads})
        return self.index[key]

    def bake_state(self, blockstate, props, tints=frozenset()):
        """A state's parts: each a list of alternative baked models (one unless the blockstate lists several)."""
        if "variants" in blockstate:
            for key, value in blockstate["variants"].items():
                cond = dict(part.split("=") for part in key.split(",")) if key else {}
                if all(props[k] == v for k, v in cond.items()):
                    applied = [value]
                    break
            else:
                raise AssertionError(f"no variant for {props}")
        else:
            applied = [p["apply"] for p in blockstate["multipart"] if "when" not in p or self.ref.matches(p["when"], props)]
        parts = []
        for value in applied:
            options = value if isinstance(value, list) else [value]
            assert len({o.get("weight", 1) for o in options}) == 1, ("unequal weights", options)
            parts.append([self.bake_variant(o, tints) for o in options])
        return parts

    def bake_item(self, model, tints=frozenset()):
        """An item's block model (no rotation) and its gui display transform, for 3D icons."""
        index = self.bake_variant({"model": model}, tints)
        gui = self.model_info(model)[3] or {}
        return index, {"rotation": gui.get("rotation", [0, 0, 0]), "translation": gui.get("translation", [0, 0, 0]),
                       "scale": gui.get("scale", [1, 1, 1])}
