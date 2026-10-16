"""Preview rendering helpers (Workbench by default: fast, deterministic, textured, with shadows)."""

import math
import os

import bpy
from mathutils import Vector

import up_config as C


def setup_workbench(scene, res=(900, 1100), shadows=True, cavity=False, world=(.62, .66, .72)):
    scene.render.engine = "BLENDER_WORKBENCH"
    sh = scene.display.shading
    sh.light = "STUDIO"
    sh.color_type = "TEXTURE"
    sh.show_shadows = shadows
    sh.shadow_intensity = .45
    sh.show_cavity = cavity
    sh.show_specular_highlight = False
    scene.display.light_direction = (.45, -.55, .75)
    scene.render.resolution_x, scene.render.resolution_y = res
    scene.render.resolution_percentage = 100
    scene.render.film_transparent = False
    scene.render.image_settings.file_format = "PNG"
    scene.render.image_settings.color_mode = "RGB"
    if not scene.world:
        scene.world = bpy.data.worlds.new("UP_World")
    scene.world.color = world
    scene.display.render_aa = "8"
    scene.view_settings.view_transform = "Standard"


def camera(name, coll=None, ortho=None, lens=50.0, sensor_fit="AUTO", clip=(.01, 200.0)):
    cam = bpy.data.objects.get(name)
    if cam is None:
        data = bpy.data.cameras.new(name)
        cam = bpy.data.objects.new(name, data)
        (coll or bpy.context.scene.collection).objects.link(cam)
    d = cam.data
    if ortho:
        d.type = "ORTHO"
        d.ortho_scale = ortho
    else:
        d.type = "PERSP"
        d.lens = lens
    d.sensor_fit = sensor_fit
    d.clip_start, d.clip_end = clip
    return cam


def look_at(cam, loc, target, roll=0.0):
    cam.location = Vector(loc)
    direction = Vector(target) - Vector(loc)
    q = direction.to_track_quat("-Z", "Y")
    cam.rotation_mode = "QUATERNION"
    cam.rotation_quaternion = q
    if roll:
        cam.rotation_mode = "XYZ"
        cam.rotation_euler.rotate_axis("Z", roll)


VIEWS = {  # name: (camera position offset from target, up target height)
    "front": Vector((0, -1, 0)), "back": Vector((0, 1, 0)), "right": Vector((-1, 0, 0)), "left": Vector((1, 0, 0)),
    "front34": Vector((-.62, -1, .22)).normalized(), "back34": Vector((.62, 1, .22)).normalized(),
}


def render_view(scene, cam, view, target, dist, path, ortho=None):
    off = VIEWS[view] * dist
    look_at(cam, Vector(target) + off, target)
    if ortho:
        cam.data.type = "ORTHO"
        cam.data.ortho_scale = ortho
    scene.camera = cam
    scene.render.filepath = path
    bpy.ops.render.render(write_still=True)
    return path


def set_visible(coll_names, visible=True):
    for n in coll_names:
        c = bpy.data.collections.get(n)
        if c:
            c.hide_render = not visible
            c.hide_viewport = not visible


def fov_vertical(cam, degrees):
    """Gameplay FOV is VERTICAL (FoundationPawn: Options 'fov' is vertical; default 110 here, 70 vanilla)."""
    cam.data.sensor_fit = "VERTICAL"
    cam.data.angle_y = math.radians(degrees)
