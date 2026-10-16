"""Look/aim grid: rotations spread through spine, shoulders, neck and head; one neutral reference pose.

Manifest convention: yaw +90 = look RIGHT, pitch +90 = look UP (game convention). Internally (armature axes)
looking right is -Z and looking up is -X.
"""

import math

from mathutils import Matrix, Quaternion, Vector

import up_anim as A
import up_config as C
import up_loco as L

GRID = {"DL": (-90, -90), "D": (0, -90), "DR": (90, -90), "L": (-90, 0), "C": (0, 0), "R": (90, 0),
        "UL": (-90, 90), "U": (0, 90), "UR": (90, 90)}

# share of the look carried by each bone (sums to 1 for yaw; pitch handled separately)
YAW_SHARE = {"c_spine_01": .12, "c_spine_02": .18, "c_neck_01": .28, "c_head": .42}
PITCH_UP = {"c_spine_01": .1, "c_spine_02": .18, "c_neck_01": .32, "c_head": .4}
PITCH_DOWN = {"c_spine_01": .07, "c_spine_02": .11, "c_neck_01": .34, "c_head": .48}
SNEAK_PITCH_UP = {"c_spine_01": .16, "c_spine_02": .22, "c_neck_01": .26, "c_head": .36}


def look_pose(rig, yaw, pitch, crouch=0.0, stance_grid=False):
    """Reference body (idle phase 0, breathing frozen) + look rotations composed on top (local rest-frame axes)."""
    p = L.base_idle(A.Pose(rig), rig, 0.0, crouch, breathe=0.0)
    up = max(0.0, pitch) / 90.0
    down = max(0.0, -pitch) / 90.0
    pitch_table = SNEAK_PITCH_UP if (stance_grid and crouch > .5 and pitch > 0) else (PITCH_UP if pitch > 0 else PITCH_DOWN)
    # At the vertical poles yaw has no gaze meaning. Fade the body twist before the pole.
    visual_yaw = yaw * (1 - L.smoother((abs(pitch) - 60) / 30))
    def posed_matrix(b, pose, cache):
        if b in cache:
            return cache[b]
        bone = rig.data.bones[b]
        parent = posed_matrix(bone.parent.name, pose, cache) @ bone.parent.matrix_local.inverted() if bone.parent else Matrix.Identity(4)
        basis = Matrix.Translation(pose.loc.get(b, Vector())) @ pose.rot.get(b, Quaternion()).to_matrix().to_4x4()
        cache[b] = parent @ bone.matrix_local @ basis
        return cache[b]
    reference = {}
    for b in ("c_spine_01", "c_spine_02", "c_neck_01", "c_head"):
        posed_matrix(b, p, reference)
    cy = cp = 0.0
    for b in ("c_spine_01", "c_spine_02", "c_neck_01", "c_head"):
        cy += -visual_yaw * YAW_SHARE[b]     # armature Z: + = left
        cp += -pitch * pitch_table[b]        # armature X: + = down
        q = A.arm_quat([("X", cp), ("Z", cy)])
        bone = rig.data.bones[b]
        base_delta = reference[b].to_quaternion() @ bone.matrix_local.to_quaternion().inverted()
        target = q @ (base_delta if b != "c_head" else Quaternion()) @ bone.matrix_local.to_quaternion()
        parent = posed_matrix(bone.parent.name, p, {}) @ bone.parent.matrix_local.inverted() @ bone.matrix_local
        p.rot[b] = parent.to_quaternion().inverted() @ target
    # shoulders follow a little: protract when looking down, counter the chest twist slightly
    for s in "lr":
        sgn = 1 if s == "l" else -1
        p.add("c_clavicle_" + s, ("Z", sgn * -6.0 * down + sgn * 3.0 * up), ("Y", sgn * 2.0 * up))
    # the head slides forward over the chest when looking down (and back when looking up), so its block does not
    # sink into the torso; hidden in first person anyway
    # Slide in character space, not the turned parent's frame.
    slide = Vector((0, -(0.065 if crouch else .071)*down + (.055 if crouch else .045)*up,
                    (.048 if crouch else .043)*down + .070*up))
    b = rig.data.bones["c_head"]
    parent = posed_matrix(b.parent.name, p, {}) @ b.parent.matrix_local.inverted() @ b.matrix_local
    p.loc["c_head"] = parent.to_3x3().inverted() @ slide
    return p


def mesh_space_additive(skel_ref, skel_sample, skel_base):
    """Emulate UE's mesh-space rotation additive: delta_b = sample_ms * ref_ms^-1, applied to the base in mesh
    space (pre-multiplied, root to leaves), then back to local. Inputs: dicts bone -> armature matrix."""
    out = {}
    for name, mb in skel_base.items():
        d = skel_sample[name].to_quaternion() @ skel_ref[name].to_quaternion().inverted()
        out[name] = d
    return out
