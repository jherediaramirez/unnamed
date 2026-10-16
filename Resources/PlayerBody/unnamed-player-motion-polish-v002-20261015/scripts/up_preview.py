"""Preview scaffolding for BAKED clips: constraints muted, rig hidden, a non-exported travel parent, video output."""

import math
import os

import bpy
from mathutils import Vector

import up_config as C
import up_render as R


class BakedPreview:
    def __init__(self, scene=None):
        self.scene = scene or bpy.context.scene
        self.skel = bpy.data.objects["UP_Skeleton"]
        self.rig = bpy.data.objects["UP_Rig"]
        self.muted = []
        for pb in self.skel.pose.bones:
            for c in pb.constraints:
                self.muted.append((c, c.mute))
                c.mute = True
        self.rig.hide_render = True
        self.rig.hide_viewport = True
        for n in ("REF_SOURCE_normalised", "GUIDES"):
            c = bpy.data.collections.get(n)
            if c:
                c.hide_render = True
        pick = bpy.data.objects.get("UP_Pickaxe")
        if pick:
            pick.hide_render = True
        self.travel = bpy.data.objects.get("PREVIEW_Travel")
        if self.travel is None:
            self.travel = bpy.data.objects.new("PREVIEW_Travel", None)
            bpy.data.collections["PREVIEW"].objects.link(self.travel)
        self.skel.parent = self.travel

    def play(self, act, velocity=(0.0, 0.0), yaw_deg=0.0, start=0, loops=1, frames=None, yaw_curve=None):
        """Assign a baked action looped `loops` times from scene frame `start`; travel parent moves at velocity."""
        n = frames or int(round(act.frame_range[1]))
        ad = self.skel.animation_data or self.skel.animation_data_create()
        ad.action = None
        for t in list(ad.nla_tracks):
            ad.nla_tracks.remove(t)
        tr = ad.nla_tracks.new()
        strip = tr.strips.new(act.name, start, act)
        strip.repeat = loops
        strip.extrapolation = "HOLD"
        try:
            strip.action_slot = act.slots[0]
        except Exception:
            pass
        self.travel.animation_data_clear()
        self.travel.location = (0, 0, 0)
        self.travel.rotation_euler = (0, 0, math.radians(yaw_deg))
        total = n * loops
        for f in (start, start + total):
            t = (f - start) / C.FPS
            self.travel.location = (velocity[0] * t, velocity[1] * t, 0.0)
            self.travel.keyframe_insert("location", frame=f)
        if yaw_curve:
            for f, deg in yaw_curve:
                self.travel.rotation_euler = (0, 0, math.radians(deg))
                self.travel.keyframe_insert("rotation_euler", frame=start + f)
        if self.travel.animation_data and self.travel.animation_data.action:
            for layer in self.travel.animation_data.action.layers:
                for st in layer.strips:
                    for bag in st.channelbags:
                        for fc in bag.fcurves:
                            for kp in fc.keyframe_points:
                                kp.interpolation = "LINEAR"
        return start + total

    def restore(self):
        for c, m in self.muted:
            c.mute = m
        self.skel.parent = None


def video_settings(scene, path, fps=C.FPS):
    scene.render.fps = fps
    scene.render.image_settings.media_type = "VIDEO"
    scene.render.image_settings.file_format = "FFMPEG"
    scene.render.ffmpeg.format = "MPEG4"
    scene.render.ffmpeg.codec = "H264"
    scene.render.ffmpeg.constant_rate_factor = "MEDIUM"
    scene.render.filepath = path


def still_settings(scene):
    scene.render.image_settings.media_type = "IMAGE"
    scene.render.image_settings.file_format = "PNG"


def follow_camera(cam, scene, target_fn, offset, frames):
    """Key a camera that keeps a constant offset from a moving target (same timestamp as the pose)."""
    cam.animation_data_clear()
    for f in frames:
        tgt = target_fn(f)
        R.look_at(cam, tgt + offset, tgt)
        cam.keyframe_insert("location", frame=f)
        cam.keyframe_insert("rotation_quaternion", frame=f)
