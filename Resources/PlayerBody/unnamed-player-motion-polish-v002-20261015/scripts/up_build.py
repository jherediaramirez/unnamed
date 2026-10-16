"""Character, skeleton, weights, control rig and reference scene for Unnamed Player v001 (Blender 5.2, bpy).

Imported by the stage scripts. Geometry is rebuilt as per-pixel box grids that reproduce the source GLB's exact
Minecraft UV layout (s00b probe), then extended with blocky hands and toe caps whose faces sample existing skin
pixels by projection onto the parent limb box.
"""

import math
import os

import bpy
from mathutils import Matrix, Vector

import up_config as C

# ------------------------------------------------------------------ small helpers


def smoothstep(e0, e1, x):
    if e0 == e1:
        return 0.0 if x < e0 else 1.0
    t = max(0.0, min(1.0, (x - e0) / (e1 - e0)))
    return t * t * (3 - 2 * t)


def ensure_collection(name, parent=None, hide_render=False, exclude=False):
    coll = bpy.data.collections.get(name) or bpy.data.collections.new(name)
    par = parent or bpy.context.scene.collection
    if coll.name not in [c.name for c in par.children]:
        par.children.link(coll)
    coll.hide_render = hide_render
    if exclude:
        lc = find_layer_collection(bpy.context.view_layer.layer_collection, coll.name)
        if lc:
            lc.exclude = True
    return coll


def find_layer_collection(lc, name):
    if lc.collection.name == name:
        return lc
    for c in lc.children:
        r = find_layer_collection(c, name)
        if r:
            return r
    return None


def link(obj, coll):
    for c in list(obj.users_collection):
        c.objects.unlink(obj)
    coll.objects.link(obj)
    return obj


def image(path, name=None):
    for im in bpy.data.images:
        if bpy.path.abspath(im.filepath) == path:
            return im
    im = bpy.data.images.load(path)
    if name:
        im.name = name
    return im


def textured_material(name, img_path, closest=True, rough=1.0, alpha=1.0, color=None):
    mat = bpy.data.materials.get(name) or bpy.data.materials.new(name)
    mat.use_nodes = True
    nt = mat.node_tree
    nt.nodes.clear()
    out = nt.nodes.new("ShaderNodeOutputMaterial")
    bsdf = nt.nodes.new("ShaderNodeBsdfPrincipled")
    bsdf.inputs["Roughness"].default_value = rough
    for key in ("Specular IOR Level", "Specular"):
        if key in bsdf.inputs:
            bsdf.inputs[key].default_value = .15
    nt.links.new(bsdf.outputs[0], out.inputs[0])
    if img_path:
        tex = nt.nodes.new("ShaderNodeTexImage")
        tex.image = image(img_path)
        tex.interpolation = "Closest" if closest else "Linear"
        nt.links.new(tex.outputs["Color"], bsdf.inputs["Base Color"])
        nt.nodes.active = tex
    elif color:
        bsdf.inputs["Base Color"].default_value = (*color[:3], 1)
        mat.diffuse_color = (*color[:3], alpha)
    if alpha < 1:
        bsdf.inputs["Alpha"].default_value = alpha
        mat.diffuse_color = (*(color or (1, 1, 1))[:3], alpha)
    return mat


# ------------------------------------------------------------------ Minecraft box UV rects (skin px)


def box_rects(U, Vv, w, h, d, arm_bottom=False):
    r = {
        "top": (U + d, Vv, U + d + w, Vv + d),
        "bottom": (U + d + w, Vv, U + d + 2 * w, Vv + d),
        "right": (U, Vv + d, U + d, Vv + d + h),
        "front": (U + d, Vv + d, U + d + w, Vv + d + h),
        "left": (U + d + w, Vv + d, U + 2 * d + w, Vv + d + h),
        "back": (U + 2 * d + w, Vv + d, U + 2 * d + 2 * w, Vv + d + h),
    }
    if arm_bottom:  # the source maps arm bottoms rotated 180 degrees (s00b probe); kept for fidelity
        u0, v0, u1, v1 = r["bottom"]
        r["bottom"] = (u1, v1, u0, v0)
    return r


class TexBox:
    """A parent box in metres plus its skin rects; blocks project their faces onto it for UVs."""

    def __init__(self, x, y, z, U, Vv, w, h, d, arm_bottom=False):
        self.x, self.y, self.z = x, y, z
        self.px = (w, d, h)
        self.rects = box_rects(U, Vv, w, h, d, arm_bottom)

    def grid(self, axis):
        lo, hi = (self.x, self.y, self.z)[axis]
        n = self.px[axis]
        return [lo + (hi - lo) * k / n for k in range(n + 1)]

    def uv(self, face, co, rect=None):
        (X0, X1), (Y0, Y1), (Z0, Z1) = self.x, self.y, self.z
        sx = (co[0] - X0) / (X1 - X0)
        sy = (co[1] - Y0) / (Y1 - Y0)
        sz = (co[2] - Z0) / (Z1 - Z0)
        if face == "front":
            sh, sv = sx, 1 - sz
        elif face == "back":
            sh, sv = 1 - sx, 1 - sz
        elif face == "right":
            sh, sv = 1 - sy, 1 - sz
        elif face == "left":
            sh, sv = sy, 1 - sz
        else:  # top / bottom: v grows toward the front
            sh, sv = sx, 1 - sy
        u0, v0, u1, v1 = rect or self.rects[face]
        sh = min(max(sh, 0.0), 1.0)
        sv = min(max(sv, 0.0), 1.0)
        u = u0 + (u1 - u0) * sh
        v = v0 + (v1 - v0) * sv
        return (u / 64.0, 1.0 - v / 64.0)


FACES = {  # name: (normal axis, sign)
    "front": (1, -1), "back": (1, 1), "right": (0, -1), "left": (0, 1), "bottom": (2, -1), "top": (2, 1)}


def merge_cuts(lo, hi, *lists, tol=1e-6):
    vals = [lo, hi]
    for li in lists:
        vals += [v for v in li if lo + tol < v < hi - tol]
    vals.sort()
    out = []
    for v in vals:
        if not out or v - out[-1] > tol:
            out.append(v)
    return out


class MeshBuilder:
    def __init__(self):
        self.verts, self.faces, self.uvs, self.weights, self.parts, self.face_names = [], [], [], [], [], []

    def block(self, part, x, y, z, tex, weight_fn, faces=None, face_rects=None, extra_cuts=((), (), ())):
        """Add a box (x, y, z ranges) gridded on the parent's pixel lines; per-face unwelded grids."""
        cuts = [merge_cuts(*rng, tex.grid(a), extra_cuts[a]) for a, rng in enumerate((x, y, z))]
        for face, (ax, sign) in FACES.items():
            if faces and face not in faces:
                continue
            plane = (x, y, z)[ax][1 if sign > 0 else 0]
            a1, a2 = [i for i in range(3) if i != ax]
            index = {}
            for i, c1 in enumerate(cuts[a1]):
                for j, c2 in enumerate(cuts[a2]):
                    co = [0.0, 0.0, 0.0]
                    co[ax], co[a1], co[a2] = plane, c1, c2
                    index[i, j] = len(self.verts)
                    self.verts.append(tuple(co))
                    self.weights.append(weight_fn(co))
                    self.parts.append(part)
            rect = (face_rects or {}).get(face)
            normal = [0.0, 0.0, 0.0]
            normal[ax] = sign
            for i in range(len(cuts[a1]) - 1):
                for j in range(len(cuts[a2]) - 1):
                    q = [index[i, j], index[i + 1, j], index[i + 1, j + 1], index[i, j + 1]]
                    p0, p1, p2 = (Vector(self.verts[k]) for k in q[:3])
                    if (p1 - p0).cross(p2 - p0).dot(Vector(normal)) < 0:
                        q.reverse()
                    self.faces.append(q)
                    self.face_names.append(face)
                    self.uvs.append([tex.uv(face, self.verts[k], rect) for k in q])

    def to_object(self, name, materials):
        me = bpy.data.meshes.new(name)
        me.from_pydata(self.verts, [], self.faces)
        me.update()
        uvl = me.uv_layers.new(name="UVMap")
        li = 0
        for poly, fuv in zip(me.polygons, self.uvs):
            for k, loop_index in enumerate(poly.loop_indices):
                uvl.data[loop_index].uv = fuv[k]
            li += 1
        for m in materials:
            me.materials.append(m)
        ob = bpy.data.objects.new(name, me)
        groups = {}
        for vi, w in enumerate(self.weights):
            for bone, val in w.items():
                if val <= 1e-4:
                    continue
                g = groups.get(bone) or ob.vertex_groups.new(name=bone)
                groups[bone] = g
                g.add([vi], val, "REPLACE")
        for poly in me.polygons:
            poly.use_smooth = False
        names = sorted(set(self.parts))
        attr = me.attributes.new("up_part", "INT", "POINT")
        attr.data.foreach_set("value", [names.index(p) for p in self.parts])
        ob["up_parts"] = names
        return ob


# ------------------------------------------------------------------ weights


def leg_weights(side):
    s = "_" + side
    hip, knee, ankle = C.J["hip_z"], C.J["knee_z"], C.J["ankle_z"]
    by = C.J["ball_y"]

    def fn(co):
        z, y = co[2], co[1]
        s1 = 1 - .5 * smoothstep(C.LEG_TOP - .045, C.LEG_TOP, z)       # pelvis share at the very top, max 50%
        s2 = smoothstep(knee + .045, knee - .045, z)
        s3 = smoothstep(ankle + .03, ankle - .02, z)
        b = smoothstep(by + .012, by - .012, y) * (1 - smoothstep(.045, .08, z))
        w = {"pelvis": 1 - s1, "thigh" + s: s1 * (1 - s2), "calf" + s: s1 * s2 * (1 - s3),
             "foot" + s: s1 * s2 * s3 * (1 - b), "ball" + s: s1 * s2 * s3 * b}
        return w
    return fn


def torso_weights(co):
    z = co[2]
    s1 = smoothstep(C.LEG_TOP + .06, C.LEG_TOP + .14, z)
    s2 = smoothstep(C.SPINE["spine_02"] - .04, C.SPINE["spine_02"] + .06, z)
    return {"pelvis": 1 - s1, "spine_01": s1 * (1 - s2), "spine_02": s1 * s2}


def arm_weights(side):
    s = "_" + side
    ez = C.J["elbow"][2]
    wz = C.J["wrist"][2]

    def fn(co):
        t = smoothstep(ez + .045, ez - .045, co[2])
        if C.HAND_STYLE == "blocky":
            return {"upperarm" + s: 1 - t, "lowerarm" + s: t}
        if C.HAND_STYLE == "pixel" and co[2] < C.LEG_TOP + 1.5 * C.V + 1e-6:
            return {"hand" + s: 1.0}
        h = smoothstep(wz + .025, wz - .025, co[2])     # the arm's end block bends at the wrist (box hands)
        return {"upperarm" + s: 1 - t, "lowerarm" + s: t * (1 - h), "hand" + s: t * h}
    return fn


def rigid(bone):
    return lambda co: {bone: 1.0}


# ------------------------------------------------------------------ character geometry


def mirror_range(r, side):
    return r if side == "l" else (-r[1], -r[0])


def build_character(coll, mat_body, mat_head):
    body = MeshBuilder()
    head = MeshBuilder()
    # head + torso (parents are the parts themselves)
    hb = TexBox((-C.HEAD / 2, C.HEAD / 2), (-C.HEAD / 2, C.HEAD / 2), (C.TORSO_TOP, C.HEIGHT), 0, 0, 8, 8, 8)
    head.block("head", hb.x, hb.y, hb.z, hb, rigid("head"))
    tb = TexBox(C.TORSO_X, C.TORSO_Y, (C.LEG_TOP, C.TORSO_TOP), 16, 16, 8, 12, 4)
    body.block("torso", tb.x, tb.y, tb.z, tb, torso_weights)
    for side in "lr":
        S = "_" + side
        # ---- legs: right leg texture (0,16), left (16,48)
        lx = mirror_range(C.LEG_X, side)
        lt = TexBox(lx, C.LEG_Y, (0.0, C.LEG_TOP), *((16, 48) if side == "l" else (0, 16)), 4, 12, 4)
        body.block("leg" + S, lt.x, lt.y, lt.z, lt, leg_weights(side),
                   extra_cuts=((), (C.J["ball_y"],), (C.J["ankle_z"],)))
        # toe cap: shoe-grey faces, sole underneath
        tc = C.TOE_CAP
        U, Vv = (16, 48) if side == "l" else (0, 16)
        shoe = (U + 4, Vv + 4 + 10, U + 8, Vv + 4 + 11)  # front face, shoe row 10 (1 px)
        body.block("toe" + S, mirror_range(tc["x"], side), tc["y"], tc["z"], lt, rigid("ball" + S),
                   faces=["front", "left", "right", "top", "bottom"], face_rects={"top": shoe})
        # ---- arms: right arm texture (40,16), left (32,48)
        ax = mirror_range(C.ARM_X, side)
        U, Vv = (32, 48) if side == "l" else (40, 16)
        at = TexBox(ax, C.ARM_Y, (C.LEG_TOP, C.TORSO_TOP), U, Vv, 4, 12, 4, arm_bottom=True)
        skin_bottom = (U + 4, Vv + 4 + 8, U + 8, Vv + 4 + 12)  # forearm end cap uses front-face skin rows
        if C.HAND_STYLE == "pixel":
            # vanilla arm down to the knuckle line, then the last 1.5 px rows: a 3-px block (back of the hand) plus
            # four 1-px finger bars on the inner edge, every face projected from the same arm pixels
            kz = C.LEG_TOP + 1.5 * C.V
            mz = (kz + C.LEG_TOP) / 2
            body.block("arm" + S, at.x, at.y, (kz, C.TORSO_TOP), at, arm_weights(side),
                       face_rects={"bottom": skin_bottom}, extra_cuts=((), (), (C.J["elbow"][2], C.J["wrist"][2])))
            inner = (C.ARM_X[0], C.ARM_X[0] + C.H)
            back = (C.ARM_X[0] + C.H, C.ARM_X[1])
            body.block("palm" + S, mirror_range(back, side), C.ARM_Y, (C.LEG_TOP, kz), at, rigid("hand" + S),
                       face_rects={"top": skin_bottom})
            for i in range(4):
                y0 = C.ARM_Y[0] + i * C.H
                gap = .002
                fy = (y0 + gap, y0 + C.H - gap)
                skin_top = {"top": skin_bottom}           # hidden at rest; never show the sleeve top when folded
                body.block("fingers1" + S, mirror_range(inner, side), fy, (mz, kz), at, rigid("fingers_01" + S),
                           face_rects=skin_top)
                body.block("fingers2" + S, mirror_range(inner, side), fy, (C.LEG_TOP, mz + .004), at,
                           rigid("fingers_02" + S), face_rects=skin_top)
            continue
        if C.HAND_STYLE != "blocky":
            # vanilla arm: one full-length box with its original 12 rows and bottom face; the end block is the hand
            body.block("arm" + S, at.x, at.y, at.z, at, arm_weights(side),
                       extra_cuts=((), (), (C.J["elbow"][2], C.J["wrist"][2])))
            continue
        body.block("arm" + S, at.x, at.y, (C.FOREARM_BOTTOM, C.TORSO_TOP), at, arm_weights(side),
                   face_rects={"bottom": skin_bottom}, extra_cuts=((), (), (C.J["elbow"][2],)))
        px = mirror_range(C.PALM["x"], side)
        skin = {"top": skin_bottom}
        body.block("palm" + S, px, C.PALM["y"], C.PALM["z"], at, rigid("hand" + S), face_rects=skin)
        # fingers: two rigid segments hinged on the palm's inner bottom edge, small overlap at each hinge
        kx = C.KNUCKLE[0]
        fx = mirror_range((kx, kx + C.FINGER["t"]), side)
        fy = C.FINGER["y"]
        z0 = C.KNUCKLE[2]
        body.block("fingers1" + S, fx, fy, (z0 - C.FINGER["l1"], z0 + .006), at, rigid("fingers_01" + S),
                   face_rects=skin)
        body.block("fingers2" + S, fx, fy, (z0 - C.FINGER["l1"] - C.FINGER["l2"], z0 - C.FINGER["l1"] + .006), at,
                   rigid("fingers_02" + S), face_rects=skin)
        # thumb: two rigid blocks along the thumb bones (built axis-aligned around each bone, then oriented)
        for seg, bname in ((1, "thumb_01" + S), (2, "thumb_02" + S)):
            p, hd, tl, roll = C.SKELETON[bname]
            add_bone_block(body, "thumb%d%s" % (seg, S), hd, tl, C.THUMB["w"], C.THUMB["w"], at, rigid(bname),
                           overlap=.006, face_rects={"top": skin_bottom, "bottom": skin_bottom})
    ob_body = body.to_object("UP_Body", [mat_body])
    ob_head = head.to_object("UP_Head", [mat_head])
    for ob in (ob_body, ob_head):
        link(ob, coll)
    return ob_body, ob_head


def add_bone_block(mb, part, head, tail, w, d, tex, weight_fn, overlap=0.0, face_rects=None):
    """A box around the segment head->tail (cross-section w x d), built axis-aligned then rotated into place."""
    h = Vector(head)
    t = Vector(tail)
    axis = (t - h)
    length = axis.length
    sub = MeshBuilder()
    # local box: x in [-w/2, w/2], y in [-d/2, d/2], z from -length (tip) to +overlap (root)
    lb = TexBox((-w / 2, w / 2), (-d / 2, d / 2), (-length, overlap), 0, 0, 1, 1, 1)
    sub.block(part, lb.x, lb.y, lb.z, lb, weight_fn)
    rot = Vector((0, 0, -1)).rotation_difference(axis.normalized()).to_matrix().to_4x4()
    m = Matrix.Translation(h) @ rot
    base = len(mb.verts)
    for k, co in enumerate(sub.verts):
        wco = m @ Vector(co)
        mb.verts.append(tuple(wco))
        mb.weights.append(sub.weights[k])
        mb.parts.append(part)
    for f, fname in zip(sub.faces, sub.face_names):
        mb.faces.append([base + i for i in f])
        mb.face_names.append(fname)
        # UVs: project the corners onto the parent limb box face that this face's world normal points along
        ax, sign = FACES[fname]
        n = Vector((0.0, 0.0, 0.0))
        n[ax] = sign
        wn = rot.to_3x3() @ n
        k = max(range(3), key=lambda i: abs(wn[i]))
        pface = {0: ("left", "right"), 1: ("back", "front"), 2: ("top", "bottom")}[k][0 if wn[k] > 0 else 1]
        mb.uvs.append([tex.uv(pface, mb.verts[base + i], (face_rects or {}).get(pface)) for i in f])


# ------------------------------------------------------------------ skeleton


def build_skeleton(coll, name="UP_Skeleton"):
    arm = bpy.data.armatures.new(name)
    ob = bpy.data.objects.new(name, arm)
    link(ob, coll)
    bpy.context.view_layer.objects.active = ob
    bpy.ops.object.mode_set(mode="EDIT")
    ebs = {}
    for n in C.BONE_ORDER:
        p, h, t, roll = C.SKELETON[n]
        eb = arm.edit_bones.new(n)
        eb.head, eb.tail = h, t
        eb.align_roll(Vector(roll))
        eb.use_deform = True
        ebs[n] = eb
    for n in C.BONE_ORDER:
        p = C.SKELETON[n][0]
        if p:
            ebs[n].parent = ebs[p]
            ebs[n].use_connect = False
    bpy.ops.object.mode_set(mode="OBJECT")
    arm.display_type = "STICK"
    ob.show_in_front = True
    return ob


def bind(mesh_obs, skel):
    for ob in mesh_obs:
        ob.parent = skel
        mod = ob.modifiers.new("Armature", "ARMATURE")
        mod.object = skel
        mod.use_deform_preserve_volume = False


# ------------------------------------------------------------------ control rig

RIG_COLL = {"Ctrl_Main": (0.9, 0.7, 0.1), "Ctrl_FK": (0.2, 0.6, 1.0), "Ctrl_IK": (1.0, 0.3, 0.3),
            "Ctrl_Fingers": (0.6, 1.0, 0.6), "Grips": (1.0, 0.2, 1.0), "MCH": (0.5, 0.5, 0.5)}


def widget(name, kind, coll, size=1.0):
    ob = bpy.data.objects.get(name)
    if ob:
        return ob
    me = bpy.data.meshes.new(name)
    vs, es = [], []
    if kind == "circle":
        n = 24
        vs = [(math.cos(2 * math.pi * i / n) * size, 0, math.sin(2 * math.pi * i / n) * size) for i in range(n)]
        es = [(i, (i + 1) % n) for i in range(n)]
    elif kind == "square":
        vs = [(-size, 0, -size), (size, 0, -size), (size, 0, size), (-size, 0, size)]
        es = [(0, 1), (1, 2), (2, 3), (3, 0)]
    elif kind == "cube":
        s = size
        vs = [(x, y, z) for x in (-s, s) for y in (-s, s) for z in (-s, s)]
        es = [(0, 1), (2, 3), (4, 5), (6, 7), (0, 2), (1, 3), (4, 6), (5, 7), (0, 4), (1, 5), (2, 6), (3, 7)]
    elif kind == "diamond":
        s = size
        vs = [(s, 0, 0), (-s, 0, 0), (0, s, 0), (0, -s, 0), (0, 0, s), (0, 0, -s)]
        es = [(0, 2), (2, 1), (1, 3), (3, 0), (0, 4), (1, 4), (2, 4), (3, 4), (0, 5), (1, 5), (2, 5), (3, 5)]
    elif kind == "foot":
        vs = [(-size * .5, -size * .4, 0), (size * .5, -size * .4, 0), (size * .5, size * 1.6, 0),
              (-size * .5, size * 1.6, 0)]
        es = [(0, 1), (1, 2), (2, 3), (3, 0)]
    me.from_pydata(vs, es, [])
    ob = bpy.data.objects.new(name, me)
    coll.objects.link(ob)
    return ob


def build_rig(coll, wgt_coll, skel):
    """Separate control armature. Every deform bone X has a rig bone c_X; UP_Skeleton copies them (world)."""
    arm = bpy.data.armatures.new("UP_Rig")
    rig = bpy.data.objects.new("UP_Rig", arm)
    link(rig, coll)
    bpy.context.view_layer.objects.active = rig
    bpy.ops.object.mode_set(mode="EDIT")
    E = arm.edit_bones
    sk = {b.name: b for b in skel.data.bones}

    def nb(name, head, tail, parent=None, roll_vec=None, deform=False):
        eb = E.new(name)
        eb.head, eb.tail = Vector(head), Vector(tail)
        if roll_vec is not None:
            eb.align_roll(Vector(roll_vec))
        eb.parent = E[parent] if parent else None
        eb.use_deform = deform
        return eb

    # FK mirror of the deform skeleton (same rest matrices)
    for n in C.BONE_ORDER:
        b = sk[n]
        eb = E.new("c_" + n)
        eb.head, eb.tail = b.head_local, b.tail_local
        eb.matrix = b.matrix_local
        eb.use_deform = False
    for n in C.BONE_ORDER:
        p = C.SKELETON[n][0]
        if p:
            E["c_" + n].parent = E["c_" + p]
    for side in "lr":
        S = "_" + side
        sgn = 1 if side == "l" else -1
        hx = sgn * (C.LEG_X[0] + C.LEG_X[1]) / 2
        # ---- leg IK: floor-level foot control + reverse foot (heel -> tip -> ball -> ankle target)
        nb("c_foot_ik" + S, (hx, 0, 0), (hx, -.16, 0), "c_root", (0, 0, 1))
        nb("c_heel_roll" + S, (hx, C.J["heel_y"], 0), (hx, C.J["heel_y"] - .06, 0), "c_foot_ik" + S, (0, 0, 1))
        nb("c_tip_roll" + S, (hx, C.J["toe_tip_y"], 0), (hx, C.J["toe_tip_y"] + .06, 0), "c_heel_roll" + S, (0, 0, 1))
        nb("c_toe_ik" + S, sk["ball" + S].head_local, sk["ball" + S].tail_local, "c_tip_roll" + S, (0, 0, 1))
        E["c_toe_ik" + S].matrix = sk["ball" + S].matrix_local
        # ball roll pivots on the toe hinge itself (the ball joint), so the flat toe segment never slides
        nb("c_ball_roll" + S, (hx, C.J["ball_y"], C.J["ball_z"]), (hx, C.J["ball_y"] + .06, C.J["ball_z"]),
           "c_tip_roll" + S, (0, 0, 1))
        at = nb("m_ankle_target" + S, sk["foot" + S].head_local, sk["foot" + S].tail_local, "c_ball_roll" + S)
        at.matrix = sk["foot" + S].matrix_local
        nb("c_knee_pole" + S, (hx, -.55, C.J["knee_z"]), (hx, -.6, C.J["knee_z"]), "c_root", (0, 0, 1))
        # ---- arm IK
        hand = sk["hand" + S]
        ik = nb("c_hand_ik" + S, hand.head_local, hand.tail_local, "c_root")
        ik.matrix = hand.matrix_local
        el = sk["lowerarm" + S].head_local
        nb("c_elbow_pole" + S, (el[0], .5, el[2]), (el[0], .55, el[2]), "c_root", (0, 0, 1))
        # ---- hand curl (blocky hands only) + grips
        if "fingers_01" + S in sk:
            k = sk["fingers_01" + S]
            nb("c_fingers_curl" + S, k.head_local, k.head_local + Vector((0, 0, -.06)), "c_hand" + S, (0, -1, 0))
            E["c_fingers_curl" + S].matrix = k.matrix_local
            E["c_fingers_curl" + S].length = .07
    # grips: handle axis centre inside the fist, +Y toward the pick head (world -Y at rest), +Z pick tip side
    for side in "lr":
        S = "_" + side
        sgn = 1 if side == "l" else -1
        g = Vector((sgn * C.GRIP_L[0], C.GRIP_L[1], C.GRIP_L[2]))
        gb = nb("c_grip" + S, g, g + Vector((0, -.08, 0)), "c_hand" + S, (0, 0, -1))
    # pickaxe control (world) and its two-handed support target
    gr = E["c_grip_r"]
    # c_pickaxe: free world control (tool-led actions). m_pickaxe: final tool placement (mesh parent) that copies
    # c_pickaxe, or the right grip while "pickaxe_in_hand" = 1 (hand-led actions). No dependency cycles.
    pk = nb("c_pickaxe", gr.head, gr.tail, "c_root")
    pk.matrix = gr.matrix
    mp = nb("m_pickaxe", gr.head, gr.tail, "c_root")
    mp.matrix = gr.matrix
    # two-handed support: left grip 16 cm further up the handle, same grip-frame orientation (palms on opposite
    # sides, both thumbs toward the head); the bone holds the matching LEFT WRIST frame
    S = mp.matrix @ Matrix.Translation((0, {"blocky": .16, "pixel": .24}.get(C.HAND_STYLE, .22), 0))
    W = S @ E["c_grip_l"].matrix.inverted() @ E["c_hand_l"].matrix
    sh = nb("c_support_hand_l", (0, 0, 0), (0, .05, 0), "m_pickaxe")
    sh.matrix = W
    sh.length = E["c_hand_l"].length
    # tool-led right hand: the right wrist frame that puts the right grip on c_pickaxe
    Wr = pk.matrix @ E["c_grip_r"].matrix.inverted() @ E["c_hand_r"].matrix
    shr = nb("c_support_hand_r", (0, 0, 0), (0, .05, 0), "c_pickaxe")
    shr.matrix = Wr
    shr.length = E["c_hand_r"].length
    bpy.ops.object.mode_set(mode="OBJECT")
    # bone collections & colours
    bcs = {}
    for name in RIG_COLL:
        bcs[name] = arm.collections.new(name)
    for b in arm.bones:
        n = b.name
        if n.startswith("m_"):
            key = "MCH"
        elif n.startswith(("c_grip", "c_pickaxe", "c_support")):
            key = "Grips"
        elif "ik" in n or "pole" in n or "roll" in n:
            key = "Ctrl_IK"
        elif "finger" in n or "thumb" in n:
            key = "Ctrl_Fingers"
        elif n in ("c_root", "c_pelvis"):
            key = "Ctrl_Main"
        else:
            key = "Ctrl_FK"
        bcs[key].assign(b)
    bcs["MCH"].is_visible = False
    # custom shapes
    shapes = {"circle": widget("WGT_circle", "circle", wgt_coll, 1.0), "cube": widget("WGT_cube", "cube", wgt_coll, .5),
              "diamond": widget("WGT_diamond", "diamond", wgt_coll, .5), "foot": widget("WGT_foot", "foot", wgt_coll, .5),
              "square": widget("WGT_square", "square", wgt_coll, .5)}
    for pb in rig.pose.bones:
        n = pb.name
        pb.rotation_mode = "QUATERNION"
        if n.startswith("c_foot_ik"):
            pb.custom_shape = shapes["foot"]
            pb.custom_shape_scale_xyz = (.4, .4, .4)
        elif "pole" in n or n.startswith("c_grip") or n == "c_pickaxe":
            pb.custom_shape = shapes["diamond"]
            pb.custom_shape_scale_xyz = (.12, .12, .12)
        elif "roll" in n or n.startswith("c_toe_ik"):
            pb.custom_shape = shapes["square"]
            pb.custom_shape_scale_xyz = (.15, .15, .15)
        elif n.startswith("c_hand_ik"):
            pb.custom_shape = shapes["cube"]
            pb.custom_shape_scale_xyz = (1.2, 1.2, 1.2)
        elif n == "c_root":
            pb.custom_shape = shapes["circle"]
            pb.custom_shape_scale_xyz = (2.2, 2.2, 2.2)
        elif n.startswith("c_") and not n.startswith("c_finger") and not n.startswith("c_thumb"):
            pb.custom_shape = shapes["circle"]
            pb.custom_shape_scale_xyz = (.6, .6, .6)
    # switches (custom props on c_root), keyed per action by the authoring scripts
    root = rig.pose.bones["c_root"]
    for side in "lr":
        for limb in ("arm", "leg"):
            key = f"ik_{limb}_{side}"
            root[key] = 1.0 if limb == "leg" else 0.0
            root.id_properties_ui(key).update(min=0.0, max=1.0, soft_min=0.0, soft_max=1.0)
    root["pickaxe_in_hand"] = 0.0
    root.id_properties_ui("pickaxe_in_hand").update(min=0.0, max=1.0)
    root["support_grip_l"] = 0.0
    root.id_properties_ui("support_grip_l").update(min=0.0, max=1.0)
    root["hand_r_on_pickaxe"] = 0.0
    root.id_properties_ui("hand_r_on_pickaxe").update(min=0.0, max=1.0)
    # constraints
    P = rig.pose.bones

    def drive(con, prop):
        fc = con.driver_add("influence")
        drv = fc.driver
        drv.type = "AVERAGE"
        var = drv.variables.new()
        var.name = "v"
        var.type = "SINGLE_PROP"
        var.targets[0].id_type = "OBJECT"
        var.targets[0].id = rig
        var.targets[0].data_path = f'pose.bones["c_root"]["{prop}"]'
        return fc

    for side in "lr":
        S = "_" + side
        c = P["c_calf" + S].constraints.new("IK")
        c.name = "IK_leg"
        c.target, c.subtarget = rig, "m_ankle_target" + S
        c.pole_target, c.pole_subtarget = rig, "c_knee_pole" + S
        c.chain_count = 2
        c.use_tail = True
        drive(c, "ik_leg" + S)
        c = P["c_foot" + S].constraints.new("COPY_ROTATION")
        c.name = "IK_foot_rot"
        c.target, c.subtarget = rig, "m_ankle_target" + S
        drive(c, "ik_leg" + S)
        c = P["c_ball" + S].constraints.new("COPY_ROTATION")
        c.name = "IK_toe_rot"
        c.target, c.subtarget = rig, "c_toe_ik" + S
        drive(c, "ik_leg" + S)
        c = P["c_lowerarm" + S].constraints.new("IK")
        c.name = "IK_arm"
        c.target, c.subtarget = rig, "c_hand_ik" + S
        c.pole_target, c.pole_subtarget = rig, "c_elbow_pole" + S
        c.chain_count = 2
        drive(c, "ik_arm" + S)
        c = P["c_hand" + S].constraints.new("COPY_ROTATION")
        c.name = "IK_hand_rot"
        c.target, c.subtarget = rig, "c_hand_ik" + S
        drive(c, "ik_arm" + S)
        for fb in [b for b in ("c_fingers_01" + S, "c_fingers_02" + S) if b in P]:
            c = P[fb].constraints.new("COPY_ROTATION")
            c.name = "Curl"
            c.target, c.subtarget = rig, "c_fingers_curl" + S
            c.owner_space = c.target_space = "LOCAL"
            c.mix_mode = "AFTER"
    c = P["c_hand_ik_l"].constraints.new("COPY_TRANSFORMS")
    c.name = "SupportGrip"
    c.target, c.subtarget = rig, "c_support_hand_l"
    drive(c, "support_grip_l")
    c = P["c_hand_ik_r"].constraints.new("COPY_TRANSFORMS")
    c.name = "OnPickaxe"
    c.target, c.subtarget = rig, "c_support_hand_r"
    drive(c, "hand_r_on_pickaxe")
    c = P["m_pickaxe"].constraints.new("COPY_TRANSFORMS")
    c.name = "FromControl"
    c.target, c.subtarget = rig, "c_pickaxe"
    c = P["m_pickaxe"].constraints.new("COPY_TRANSFORMS")
    c.name = "InRightHand"
    c.target, c.subtarget = rig, "c_grip_r"
    drive(c, "pickaxe_in_hand")
    set_pole_angles(rig)
    # deform skeleton copies the rig
    for pb in skel.pose.bones:
        pb.rotation_mode = "QUATERNION"
        c = pb.constraints.new("COPY_TRANSFORMS")
        c.name = "FromRig"
        c.target, c.subtarget = rig, "c_" + pb.name
    rig.show_in_front = True
    arm.display_type = "WIRE"
    return rig


def signed_angle(v1, v2, axis):
    # sign convention of the classic Blender pole-angle recipe: negative when (v1 x v2) points along the axis
    a = v1.angle(v2)
    return -a if v1.cross(v2).dot(axis) > 0 else a


def set_pole_angles(rig):
    """Pole angle that keeps the rest pose (classic formula), then verified numerically by the rig test."""
    bones = rig.data.bones
    for side in "lr":
        S = "_" + side
        for upper, lower, pole, cname in (("c_thigh", "c_calf", "c_knee_pole", "IK_leg"),
                                          ("c_upperarm", "c_lowerarm", "c_elbow_pole", "IK_arm")):
            ub, lb, pb = bones[upper + S], bones[lower + S], bones[pole + S]
            base_head, base_tail = ub.head_local, ub.tail_local
            ik_tail = lb.tail_local
            pole_loc = pb.head_local
            pole_normal = (ik_tail - base_head).cross(pole_loc - base_head)
            proj = pole_normal.cross(base_tail - base_head)
            ang = signed_angle(ub.x_axis, proj, base_tail - base_head)
            rig.pose.bones[lower + S].constraints[cname].pole_angle = ang


# ------------------------------------------------------------------ reference scene


def build_reference(ref_coll, guide_coll):
    """Source GLB normalised (x0.225, rebased to soles, turned to face -Y) beside the new body, plus guides."""
    before = set(bpy.data.objects)
    bpy.ops.import_scene.gltf(filepath=C.SOURCE_GLB)
    new = [o for o in bpy.data.objects if o not in before]
    for o in new:
        link(o, ref_coll)
    norm = bpy.data.objects.new("REF_Source_Normaliser", None)
    link(norm, ref_coll)
    k = 1.8 / 8.000002
    norm.scale = (k, k, k)
    norm.rotation_euler = (0, 0, -math.pi / 2)
    norm.location = (1.25, 0, 0.180509 * k)
    for o in new:
        if o.parent is None:
            o.parent = norm
    ico = bpy.data.objects.get("Icosphere")
    if ico:
        ico.hide_render = True
        ico.hide_viewport = True
    # guides
    gmat = {n: textured_material("M_Guide_" + n, None, color=c) for n, c in
            (("Stand", (.95, .25, .2)), ("Sneak", (1.0, .65, .1)), ("Low", (.2, .7, 1.0)), ("Cube", (.35, .8, .35)),
             ("Box", (.15, .15, .15)), ("Contact", (.9, .1, .9)), ("Step", (.55, .45, .35)))}
    objs = {}

    def bar(name, z, mat, length=1.6, x=0.25):
        me = bpy.data.meshes.new(name)
        w, t = .004, .004
        vs = [(x - length / 2, -w, z - t), (x + length / 2, -w, z - t), (x + length / 2, w, z - t), (x - length / 2, w, z - t),
              (x - length / 2, -w, z + t), (x + length / 2, -w, z + t), (x + length / 2, w, z + t), (x - length / 2, w, z + t)]
        fs = [(0, 1, 2, 3), (4, 7, 6, 5), (0, 4, 5, 1), (1, 5, 6, 2), (2, 6, 7, 3), (3, 7, 4, 0)]
        me.from_pydata(vs, [], fs)
        me.materials.append(mat)
        ob = bpy.data.objects.new(name, me)
        link(ob, guide_coll)
        objs[name] = ob
        return ob

    def label(name, text, loc, mat, size=.045):
        cu = bpy.data.curves.new(name, "FONT")
        cu.body = text
        cu.size = size
        ob = bpy.data.objects.new(name, cu)
        ob.location = loc
        ob.rotation_euler = (math.pi / 2, 0, 0)
        cu.materials.append(mat)
        link(ob, guide_coll)
        objs[name] = ob
        return ob

    for key, z, txt in (("Stand", C.GAME["stand_eyes_m"], "eyes 162"), ("Sneak", C.GAME["sneak_eyes_m"], "sneak eyes 127"),
                        ("Low", C.GAME["low_eyes_m"], "low eyes 40")):
        bar("GUIDE_Eye_" + key, z, gmat[key], length=3.4, x=.15)
        label("GUIDE_EyeLabel_" + key, txt, (-1.5, -.32, z + .012), gmat[key], size=.075)
    for key, h in (("Stand", 1.80), ("Sneak", 1.50), ("Low", .60)):
        bpy.ops.mesh.primitive_cube_add(size=1)
        bx = bpy.context.active_object
        bx.name = "GUIDE_Box_" + key
        bx.scale = (C.GAME["width_m"], C.GAME["width_m"], h)
        bx.location = (0, 0, h / 2)
        bpy.ops.object.transform_apply(location=False, rotation=False, scale=True)
        wf = bx.modifiers.new("wire", "WIREFRAME")
        wf.thickness = .006 if key == "Stand" else .004
        bx.data.materials.append(gmat[key] if key != "Stand" else gmat["Box"])
        link(bx, guide_coll)
        objs[bx.name] = bx
    bpy.ops.mesh.primitive_cube_add(size=1, location=(-1.35, 0, .5))
    cube = bpy.context.active_object
    cube.name = "GUIDE_Cube_1m"
    cube.data.materials.append(gmat["Cube"])
    link(cube, guide_coll)
    label("GUIDE_CubeLabel", "1 m block", (-1.62, -.52, 1.04), gmat["Cube"], size=.075)
    # contact guides: thin sole-plane markers under each foot (flat plant reference)
    for side in "lr":
        sgn = 1 if side == "l" else -1
        hx = sgn * (C.LEG_X[0] + C.LEG_X[1]) / 2
        me = bpy.data.meshes.new("GUIDE_Sole_" + side)
        y0, y1 = C.J["toe_tip_y"], C.J["heel_y"]
        vs = [(hx - .1, y0, .001), (hx + .1, y0, .001), (hx + .1, y1, .001), (hx - .1, y1, .001)]
        me.from_pydata(vs, [], [(0, 1, 2, 3)])
        me.materials.append(gmat["Contact"])
        ob = bpy.data.objects.new("GUIDE_Sole_" + side, me)
        link(ob, guide_coll)
        ob.display_type = "WIRE"
        ob.hide_render = True
    return objs


def floor(coll, size=40.0, name="PREVIEW_Floor"):
    me = bpy.data.meshes.new(name)
    s = size / 2
    me.from_pydata([(-s, -s, 0), (s, -s, 0), (s, s, 0), (-s, s, 0)], [], [(0, 1, 2, 3)])
    uv = me.uv_layers.new(name="UVMap")
    for li, (u, v) in zip(range(4), ((0, 0), (size, 0), (size, size), (0, size))):
        uv.data[li].uv = (u, v)
    mat = textured_material("M_Preview_Grid", os.path.join(C.ROOT, "textures", "T_Preview_Grid_1m.png"), closest=False)
    me.materials.append(mat)
    ob = bpy.data.objects.new(name, me)
    link(ob, coll)
    return ob


# ------------------------------------------------------------------ props: pickaxe + stone target


def build_props(coll, rig):
    """Blocky stone pickaxe in the c_pickaxe bone space (Y toward the head, Z toward the pick tip) + 1 m stone."""
    wood = textured_material("M_Prop_Wood", None, color=(.42, .29, .16))
    stone_m = textured_material("M_Prop_PickStone", None, color=(.50, .50, .52))
    mb = MeshBuilder()
    unit = TexBox((-1, 1), (-1, 1), (-1, 1), 0, 0, 1, 1, 1)
    w = .04
    # handle: butt 9 cm behind the grip centre, runs 0.50 m toward the head
    # handle butt flush with the fist's little-finger side (a longer butt clipped the forearm at cocked wrists)
    mb.block("handle", (-w / 2, w / 2), (-.065, .47), (-w / 2, w / 2), unit, rigid("pickaxe"),
             extra_cuts=((0.0,), [-.065 + .02 * k for k in range(27)], (0.0,)))
    # pick head: long point toward +Z (the tip: points DOWN at rest, FORWARD once the arm is raised), short adze -Z
    head = [(.175, .265, .40, .455), (.065, .175, .43, .50), (-.065, .065, .455, .53), (-.145, -.065, .43, .50),
            (-.205, -.145, .40, .455)]
    nhandle = len(mb.faces)
    for z0, z1, y0, y1 in head:
        mb.block("head", (-.024, .024), (y0, y1), (z0, z1), unit, rigid("pickaxe"))
    me_ob = mb.to_object("UP_Pickaxe", [wood, stone_m])
    for i, poly in enumerate(me_ob.data.polygons):
        poly.material_index = 0 if i < nhandle else 1
    me_ob.vertex_groups.clear()
    link(me_ob, coll)
    me_ob.parent = rig
    me_ob.parent_type = "BONE"
    me_ob.parent_bone = "m_pickaxe"
    # bone parenting puts the child at the bone TAIL; express the mesh in head-space
    pb = rig.data.bones["m_pickaxe"]
    me_ob.matrix_world = rig.matrix_world @ pb.matrix_local
    # stone target (1 m block), placed by the pickaxe stage
    bpy.ops.mesh.primitive_cube_add(size=1, location=(0, -1.3, .5))
    st = bpy.context.active_object
    st.name = "PROP_Stone"
    bpy.ops.object.mode_set(mode="EDIT")
    bpy.ops.uv.cube_project(cube_size=1.0)
    bpy.ops.object.mode_set(mode="OBJECT")
    st.data.materials.append(textured_material("M_Prop_Stone", os.path.join(C.ROOT, "textures", "T_Preview_Stone.png")))
    link(st, coll)
    st.hide_render = True
    st.hide_viewport = True
    return me_ob, st
