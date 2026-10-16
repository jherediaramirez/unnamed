"""Import the Unnamed Player (the full-body player, export v002c) from Art/Characters/UnnamedPlayer into
UUnnamedProjectSettings::UnnamedPlayerRoot (/Game/Characters/UnnamedPlayer), repeating M0's proven Interchange
procedure (Resources/PlayerBody/unnamed-player-export-v002c-20261016/evidence/final_20261016_015709/REPORT.md):

- SK_UnnamedPlayer (22 bones, 180 cm, faces +Y; 2 sections M_UP_Body/M_UP_Head; sockets hand_r_grip, hand_l_grip,
  head_eye from the FBX's SOCKET_* leaf nodes) on a new SK_UnnamedPlayer_Skeleton, no physics asset;
- T_UP_Skin from T_UP_Skin_64.png (Nearest, no mips, sRGB) wired into both material instances (the FBX's embedded
  texture does not import through Interchange; its one logged error is expected);
- the 66 clips named by the export manifest's unreal_name ("Sneak" becomes "Crouch"). The 20 single-frame pose clips
  import over frames 0..2 at 60 Hz (Set Range; 3 identical keys); the 18 look poses are mesh-space additive on
  UP_Look_C or UP_CrouchLook_C frame 0;
- SM_UP_Pickaxe (its origin is the grip: attach to hand_r_grip with an identity transform).

Each asset carries the SHA-256 of the file it came from (tag UP.SourceSha256). Like the other import scripts it never
replaces anything: a folder that already holds the player is verified, one from other files is refused (after a
re-export, move the folder away deliberately and rerun; a re-import keeps an existing skeleton's reference pose).
Every run measures PLAN.md's gates 1-8 plus the additive looks and the pickaxe (UnnamedPlayerGates.py), writes the
numbers to Saved/ImportUnnamedPlayer/report-<time>.json and fails unless all pass.

    & 'C:\\Program Files\\Epic Games\\UE_5.8\\Engine\\Binaries\\Win64\\UnrealEditor-Cmd.exe' Unnamed.uproject
        -run=pythonscript "-script=Scripts\\Editor\\ImportUnnamedPlayer.py" -unattended -nop4 -nosplash
"""
import hashlib
import json
import sys
import time
from pathlib import Path
import unreal

sys.path.insert(0, str(Path(__file__).resolve().parent))
sys.dont_write_bytecode = True
import ProjectPaths  # noqa: E402
import UnnamedPlayerGates as G  # noqa: E402

ROOT = Path(unreal.Paths.project_dir()).resolve()
ART = ROOT / "Art/Characters/UnnamedPlayer"
TAG = "UP.SourceSha256"
assets = unreal.EditorAssetLibrary


def fail(message):
    raise RuntimeError("UNNAMED_PLAYER_IMPORT FAIL: " + message)


def log(message):
    unreal.log("[UnnamedPlayer] " + message)


manifest = json.loads((ART / "manifest.json").read_text(encoding="utf-8"))
for relative, sha in manifest["files"].items():
    if hashlib.sha256((ART / relative).read_bytes()).hexdigest() != sha:
        fail(relative + " does not match manifest.json")
oracle = G.Oracle(ART, manifest["export_manifest"])
SHA = {k: v.lower() for k, v in oracle.man["files"].items()}
mesh_package = ProjectPaths.unnamed_player_asset("SK_UnnamedPlayer")
folder, _ = ProjectPaths.split(mesh_package)


def package(name):
    return folder + "/" + name


# ---------------------------------------------------------------- import
def make_pipeline(mesh, skeleton=None, asset_name=""):
    """M0's explicit Interchange settings: scale 1, no offsets, sockets on, sections combined, T0 not the bind pose,
    no morph targets or physics asset, bone tracks sampled at 60 Hz over the file's timeline."""
    p = unreal.InterchangeGenericAssetsPipeline()
    p.set_editor_property("use_source_name_for_asset", True)
    p.set_editor_property("asset_name", asset_name)
    p.set_editor_property("import_offset_translation", unreal.Vector(0, 0, 0))
    p.set_editor_property("import_offset_rotation", unreal.Rotator(0, 0, 0))
    p.set_editor_property("import_offset_uniform_scale", 1.0)
    common = p.get_editor_property("common_meshes_properties")
    common.set_editor_property("import_sockets", True)
    common.set_editor_property("keep_sections_separate", False)
    skeletal = p.get_editor_property("common_skeletal_meshes_and_animations_properties")
    skeletal.set_editor_property("import_only_animations", not mesh)
    skeletal.set_editor_property("skeleton", skeleton)
    skeletal.set_editor_property("use_t0_as_ref_pose", False)
    meshes = p.get_editor_property("mesh_pipeline")
    meshes.set_editor_property("import_static_meshes", False)
    meshes.set_editor_property("import_skeletal_meshes", mesh)
    meshes.set_editor_property("combine_skeletal_meshes_behavior",
                               unreal.InterchangeCombineSkeletalMeshesBehavior.BY_SKELETON)
    meshes.set_editor_property("import_morph_targets", False)
    meshes.set_editor_property("create_physics_asset", False)
    meshes.set_editor_property("update_skeleton_reference_pose", False)
    anim = p.get_editor_property("animation_pipeline")
    anim.set_editor_property("import_animations", not mesh)
    anim.set_editor_property("import_bone_tracks", True)
    anim.set_editor_property("animation_range", unreal.InterchangeAnimationRange.TIMELINE)
    for name in ("use30hz_to_bake_bone_animation", "use30_hz_to_bake_bone_animation"):
        try:
            anim.set_editor_property(name, False)
            break
        except Exception:  # noqa: BLE001 (the binding's snake_case of "30Hz" varies)
            continue
    anim.set_editor_property("custom_bone_animation_sample_rate", 60)
    materials = p.get_editor_property("material_pipeline")
    materials.set_editor_property("import_materials", mesh)
    materials.get_editor_property("texture_pipeline").set_editor_property("import_textures", mesh)
    return p


def interchange_import(path, pipeline):
    params = unreal.ImportAssetParameters()
    params.set_editor_property("is_automated", True)
    params.set_editor_property("replace_existing", False)
    params.set_editor_property("override_pipelines", [unreal.SoftObjectPath(pipeline.get_path_name())])
    before = set(assets.list_assets(folder, recursive=True, include_folder=False)) \
        if assets.does_directory_exist(folder) else set()
    unreal.InterchangeManager.get_interchange_manager_scripted().import_asset(
        folder, unreal.InterchangeManager.create_source_data(str(path)), params)
    return sorted(set(assets.list_assets(folder, recursive=True, include_folder=False)) - before)


def only(new, cls, what):
    found = [a for a in new if G.class_of(a) == cls]
    if len(found) != 1:
        fail("%s made %s, not one %s" % (what, found, cls))
    return assets.load_asset(found[0])


def import_skin():
    task = unreal.AssetImportTask()
    task.filename, task.destination_path, task.destination_name = str(ART / "T_UP_Skin_64.png"), folder, "T_UP_Skin"
    task.automated, task.replace_existing, task.save = True, False, True
    unreal.AssetToolsHelpers.get_asset_tools().import_asset_tasks([task])
    texture = assets.load_asset(package("T_UP_Skin"))
    texture.set_editor_property("filter", unreal.TextureFilter.TF_NEAREST)
    texture.set_editor_property("mip_gen_settings", unreal.TextureMipGenSettings.TMGS_NO_MIPMAPS)
    texture.set_editor_property("srgb", True)
    assets.set_metadata_tag(texture, TAG, SHA["T_UP_Skin_64.png"])
    for name in ("M_UP_Body", "M_UP_Head"):
        instance = assets.load_asset(package(name))
        parameter = next((t for t in unreal.MaterialEditingLibrary.get_texture_parameter_names(instance)
                          if "diffuse" in str(t).lower() or "basecolor" in str(t).lower()), None)
        if parameter is None:
            fail(name + " has no base colour texture parameter")
        unreal.MaterialEditingLibrary.set_material_instance_texture_parameter_value(instance, parameter, texture)
        unreal.MaterialEditingLibrary.update_material_instance(instance)


def import_all():
    mesh = only(interchange_import(ART / "SK_UnnamedPlayer.fbx", make_pipeline(True)), "SkeletalMesh", "the mesh FBX")
    assets.set_metadata_tag(mesh, TAG, SHA["SK_UnnamedPlayer.fbx"])
    skeleton = mesh.get_editor_property("skeleton")
    import_skin()
    clips = {}
    for name, clip in sorted(oracle.clips.items()):
        pipeline = make_pipeline(False, skeleton, clip["unreal_name"])
        if clip.get("pose_clip"):
            anim = pipeline.get_editor_property("animation_pipeline")
            anim.set_editor_property("animation_range", unreal.InterchangeAnimationRange.SET_RANGE)
            frames = unreal.Int32Interval()
            frames.set_editor_property("min", 0)
            frames.set_editor_property("max", G.POSE_IMPORTED_FRAMES)
            anim.set_editor_property("frame_import_range", frames)
        sequence = only(interchange_import(ART / clip["file"], pipeline), "AnimSequence", clip["file"])
        if sequence.get_name() != clip["unreal_name"]:
            fail("%s imported as %s, not %s" % (clip["file"], sequence.get_name(), clip["unreal_name"]))
        assets.set_metadata_tag(sequence, TAG, SHA[clip["file"]])
        clips[clip["unreal_name"]] = sequence
    for unreal_name, sequence in clips.items():
        base = G.look_base(unreal_name)
        if base:
            sequence.set_editor_property("additive_anim_type", unreal.AdditiveAnimationType.AAT_ROTATION_OFFSET_MESH_SPACE)
            sequence.set_editor_property("ref_pose_type", unreal.AdditiveBasePoseType.ABPT_ANIM_FRAME)
            sequence.set_editor_property("ref_pose_seq", clips[base])
            sequence.set_editor_property("ref_frame_index", 0)
    pipeline = make_pipeline(True, None, "SM_UP_Pickaxe")
    pipeline.get_editor_property("mesh_pipeline").set_editor_property("import_static_meshes", True)
    pipeline.get_editor_property("mesh_pipeline").set_editor_property("import_skeletal_meshes", False)
    pipeline.get_editor_property("material_pipeline").set_editor_property("import_materials", False)
    pipeline.get_editor_property("material_pipeline").get_editor_property("texture_pipeline").set_editor_property(
        "import_textures", False)
    pickaxe = only(interchange_import(ART / "SM_UP_Pickaxe.fbx", pipeline), "StaticMesh", "the pickaxe FBX")
    assets.set_metadata_tag(pickaxe, TAG, SHA["SM_UP_Pickaxe.fbx"])
    assets.save_directory(folder, only_if_is_dirty=False, recursive=True)


# ---------------------------------------------------------------- verify (every run)
def tagged(path, sha, what):
    if not assets.does_asset_exist(path):
        fail("missing " + what + " " + path)
    asset = assets.load_asset(path)
    tag = assets.get_metadata_tag(asset, TAG)
    if not tag:
        fail(folder + " is an incomplete import (no " + TAG + " on " + path + "); move it away and rerun")
    if tag != sha:
        fail(path + " was imported from another file; move " + folder + " away deliberately and rerun")
    return asset


def verify():
    report = {"folder": folder, "engine": unreal.SystemLibrary.get_engine_version(), "export": manifest["fbx_sha256"]}
    mesh = tagged(mesh_package, SHA["SK_UnnamedPlayer.fbx"], "mesh")
    skeleton = mesh.get_editor_property("skeleton")
    texture = tagged(package("T_UP_Skin"), SHA["T_UP_Skin_64.png"], "skin")
    pickaxe = tagged(package("SM_UP_Pickaxe"), SHA["SM_UP_Pickaxe.fbx"], "pickaxe")
    sequences = {}
    for name, clip in oracle.clips.items():
        sequence = tagged(package(clip["unreal_name"]), SHA[clip["file"]], "clip")
        if sequence.get_editor_property("skeleton") != skeleton:
            fail(clip["unreal_name"] + " is on another skeleton")
        sequences[name] = sequence
    skin = {"filter": str(texture.get_editor_property("filter")), "srgb": texture.get_editor_property("srgb"),
            "mips": str(texture.get_editor_property("mip_gen_settings")),
            "size": [texture.blueprint_get_size_x(), texture.blueprint_get_size_y()], "materials": {}}
    for name in ("M_UP_Body", "M_UP_Head"):
        instance = assets.load_asset(package(name))
        values = [unreal.MaterialEditingLibrary.get_material_instance_texture_parameter_value(instance, t)
                  for t in unreal.MaterialEditingLibrary.get_texture_parameter_names(instance)]
        skin["materials"][name] = any(v and v.get_path_name() == texture.get_path_name() for v in values)
    skin["pass"] = (texture.get_editor_property("filter") == unreal.TextureFilter.TF_NEAREST
                    and texture.get_editor_property("mip_gen_settings") == unreal.TextureMipGenSettings.TMGS_NO_MIPMAPS
                    and skin["size"] == [64, 64] and all(skin["materials"].values()))
    report["skin"] = skin
    gates, comp, sx = G.mesh_gates(oracle, mesh, skeleton, sequences["UP_Idle"])
    report["mesh_gates"] = gates
    report["source_reference_calibration_cm"] = G.source_reference_calibration(oracle, comp)
    report["clips"] = {}
    for name, sequence in sorted(sequences.items()):
        report["clips"][name] = G.clip_gates(oracle, sequence, name, mesh, comp, sx)
    report["additive"] = {}
    for name, sequence in sorted(sequences.items()):
        base = G.look_base(oracle.clips[name]["unreal_name"])
        if base:
            report["additive"][name] = G.additive_gate(oracle, name, sequence, base, mesh, comp)
        elif str(sequence.get_editor_property("additive_anim_type")) != str(unreal.AdditiveAnimationType.AAT_NONE):
            report["additive"][name] = {"pass": False, "error": "additive, but not a look pose"}
    report["pickaxe"] = G.pickaxe_gate(oracle, pickaxe)
    every = assets.list_assets(folder, recursive=True, include_folder=False)
    counts = {}
    for path in every:
        counts[G.class_of(path)] = counts.get(G.class_of(path), 0) + 1
    want = {"Skeleton": 1, "SkeletalMesh": 1, "StaticMesh": 1, "AnimSequence": 66, "MaterialInstanceConstant": 2,
            "Texture2D": 1}
    report["8_assets"] = {"counts": counts, "expected": want, "pass": counts == want}
    clip_pass = {n: all(g["pass"] for g in c.values()) for n, c in report["clips"].items()}
    summary = {"mesh_gates": {k: v["pass"] for k, v in gates.items()},
               "clips_passing": sum(clip_pass.values()), "clips": len(clip_pass),
               "failing_clips": sorted(n for n, ok in clip_pass.items() if not ok),
               "looks_additive_passing": sum(a["pass"] for a in report["additive"].values()),
               "looks": len(report["additive"]), "skin": skin["pass"], "pickaxe": report["pickaxe"]["pass"],
               "gate8": report["8_assets"]["pass"], "assets": len(every)}
    summary["all_pass"] = (all(summary["mesh_gates"].values()) and not summary["failing_clips"]
                           and summary["clips"] == 66 and summary["looks"] == 18
                           and summary["looks_additive_passing"] == 18 and skin["pass"] and summary["pickaxe"]
                           and summary["gate8"])
    report["summary"] = summary
    return report


imported = False
if not assets.does_asset_exist(mesh_package):
    if assets.does_directory_exist(folder) and assets.list_assets(folder, recursive=True):
        fail(folder + " holds assets but no SK_UnnamedPlayer; move it away deliberately and rerun")
    log("importing into " + folder)
    import_all()
    imported = True
report = verify()
report["imported"] = imported
out = ROOT / "Saved/ImportUnnamedPlayer" / ("report-" + time.strftime("%Y%m%d-%H%M%S") + ".json")
out.parent.mkdir(parents=True, exist_ok=True)
out.write_text(json.dumps(report, indent=1, default=str), encoding="utf-8")
summary = dict(report["summary"], imported=imported, report=str(out))
if not summary["all_pass"]:
    fail("gates failed: " + json.dumps(summary))
unreal.log("UNNAMED_PLAYER_IMPORT OK " + json.dumps(summary))
