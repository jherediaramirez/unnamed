"""Deformation and contact measurements on the evaluated (posed) meshes."""

import bpy
import numpy as np
from mathutils import Vector
from mathutils.bvhtree import BVHTree


def evaluated(ob):
    dg = bpy.context.evaluated_depsgraph_get()
    ev = ob.evaluated_get(dg)
    me = ev.to_mesh()
    mw = ev.matrix_world
    co = np.array([tuple(mw @ v.co) for v in me.vertices])
    polys = [tuple(p.vertices) for p in me.polygons]
    ev.to_mesh_clear()
    return co, polys


def rest_coords(ob):
    mw = ob.matrix_world
    return np.array([tuple(mw @ v.co) for v in ob.data.vertices])


def part_index(ob):
    names = list(ob["up_parts"])
    vals = np.zeros(len(ob.data.vertices), dtype=np.int64)
    ob.data.attributes["up_part"].data.foreach_get("value", vals)
    return names, vals


class PartSet:
    """Per-part vertex/face subsets of one evaluated mesh, with BVH trees on demand."""

    def __init__(self, ob, co=None, polys=None):
        self.ob = ob
        if co is None:
            co, polys = evaluated(ob)
        self.co, self.polys = co, polys
        self.names, self.vpart = part_index(ob)
        self.fpart = np.array([self.vpart[p[0]] for p in polys])
        self._trees = {}

    def verts(self, part):
        return np.where(self.vpart == self.names.index(part))[0]

    def faces(self, part):
        return [self.polys[i] for i in np.where(self.fpart == self.names.index(part))[0]]

    def tree(self, part):
        if part not in self._trees:
            fs = self.faces(part)
            self._trees[part] = BVHTree.FromPolygons([Vector(c) for c in self.co], fs, all_triangles=False,
                                                     epsilon=0.0)
        return self._trees[part]

    RAYS = [Vector((1, .1234, .0571)).normalized(), Vector((-.0731, 1, .1517)).normalized(),
            Vector((.1093, -.0423, 1)).normalized()]

    def inside(self, b, p):
        """Ray-parity test against part b (closed box-like surfaces); majority of three skewed rays."""
        t = self.tree(b)
        votes = 0
        for d in self.RAYS:
            n, o = 0, Vector(p)
            for _ in range(64):
                loc, nrm, idx, dist = t.ray_cast(o, d)
                if loc is None:
                    break
                n += 1
                o = loc + d * 1e-5
            votes += n % 2
        return votes >= 2

    def bbox(self, part):
        if not hasattr(self, "_bb"):
            self._bb = {}
        if part not in self._bb:
            c = self.co[self.verts(part)]
            self._bb[part] = (c.min(axis=0), c.max(axis=0))
        return self._bb[part]

    def penetration(self, a, b, other=None):
        """Max depth (m) of part a's vertices that are inside part b (ray parity), measured to b's surface."""
        src = self if other is None else other
        lo, hi = self.bbox(b)
        t = self.tree(b)
        worst = 0.0
        for vi in src.verts(a):
            p = src.co[vi]
            if (p < lo - 1e-4).any() or (p > hi + 1e-4).any():
                continue
            loc, nrm, idx, dist = t.find_nearest(Vector(p))
            if loc is None or dist < 1e-4:
                continue
            if self.inside(b, p):
                worst = max(worst, dist)
        return worst

    def attached(self, a, b, tol=.003):
        """True when part a touches or overlaps part b (min gap <= tol or any vertex of a inside b)."""
        if self.min_gap(a, b) <= tol:
            return True
        lo, hi = self.bbox(b)
        for vi in self.verts(a):
            p = self.co[vi]
            if (p >= lo).all() and (p <= hi).all() and self.inside(b, p):
                return True
        return False

    def penetration_old(self, a, b, other=None):
        """Max depth (m) of part a's vertices inside part b (inside = behind b's nearest face normal)."""
        src = self if other is None else other
        t = self.tree(b)
        worst = 0.0
        for vi in src.verts(a):
            p = Vector(src.co[vi])
            loc, nrm, idx, dist = t.find_nearest(p)
            if loc is None:
                continue
            # inside only when the offset points clearly against the face normal (coplanar contact is not)
            if dist > 1e-4 and (p - loc).dot(nrm) < -.3 * dist:
                worst = max(worst, dist)
        return worst

    def min_gap(self, a, b):
        t = self.tree(b)
        best = 1e9
        for vi in self.verts(a):
            loc, nrm, idx, dist = t.find_nearest(Vector(self.co[vi]))
            if loc is not None:
                best = min(best, dist)
        return best


def edge_face_stats(rest_co, co, polys, fpart, names):
    """Per part: min/max edge length ratio and min/max face area ratio (posed/rest)."""
    out = {}
    for pi, name in enumerate(names):
        er, ar = [], []
        for fi in np.where(fpart == pi)[0]:
            p = polys[fi]
            n = len(p)
            for k in range(n):
                a, b = p[k], p[(k + 1) % n]
                l0 = np.linalg.norm(rest_co[a] - rest_co[b])
                if l0 > 1e-6:
                    er.append(np.linalg.norm(co[a] - co[b]) / l0)
            a0 = poly_area(rest_co[list(p)])
            if a0 > 1e-9:
                ar.append(poly_area(co[list(p)]) / a0)
        if er:
            out[name] = {"edge_min": float(min(er)), "edge_max": float(max(er)),
                         "area_min": float(min(ar)) if ar else None, "area_max": float(max(ar)) if ar else None}
    return out


def poly_area(pts):
    s = np.zeros(3)
    for k in range(len(pts)):
        s += np.cross(pts[k], pts[(k + 1) % len(pts)])
    return np.linalg.norm(s) / 2
