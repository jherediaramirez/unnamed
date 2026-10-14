"""Import the player body, Better Steve, from Art/Characters/BetterSteve (the FBX the better-steve project's
scripts/export_ue.py writes) into UUnnamedProjectSettings::PlayerBodyRoot (/Game/Characters/BetterSteve):

- SK_BetterSteve (skeletal mesh, 56 bones, 187.5 cm), SK_BetterSteve_Skeleton, SK_BetterSteve_PhysicsAsset,
  M_BetterSteve and T_BetterSteve (Nearest filter, no mips: the pixels stay crisp);
- the 43 actions as AnimSequences named after them (BS_Idle, BS_Walk_F, ...). The look poses BS_Look_* are made
  mesh-space additive on BS_Look_C frame 0: they are the head look's samples (FoundationBodyAnimInstance).

Additive and verifying, like ImportOreLooks.py: a folder that already holds the body is checked (the FBX hash tag on
the mesh, every clip, the look poses' additive settings, the texture's filter) and never replaced; one made from another
FBX is refused. After a re-export, move the folder away deliberately and rerun: re-importing into an existing skeleton
keeps its old reference pose.

    & 'C:\\Program Files\\Epic Games\\UE_5.8\\Engine\\Binaries\\Win64\\UnrealEditor-Cmd.exe' Unnamed.uproject
        -run=pythonscript "-script=Scripts\\Editor\\ImportBetterSteve.py" -unattended -nop4 -nosplash
"""
import hashlib
import json
import sys
from pathlib import Path
import unreal

sys.path.insert(0, str(Path(__file__).resolve().parent))
sys.dont_write_bytecode = True
import ProjectPaths  # noqa: E402

ROOT = Path(unreal.Paths.project_dir()).resolve()
ART = ROOT / "Art/Characters/BetterSteve"
TAG = "BetterSteveFbxSha256"
IMPORT_PREFIX = "SK_BetterSteveArmature_"  # the importer's name for an action: <mesh><armature>_<action>
DIRECTIONS = ["F", "FR", "R", "BR", "B", "BL", "L", "FL"]
LOOKS = ["C", "U", "D", "L", "R", "UL", "UR", "DL", "DR"]
# The clips the game plays (FoundationBodyClip), then the rest of the 43 actions.
CLIPS = (["BS_Idle"] + ["BS_Walk_" + d for d in DIRECTIONS] + ["BS_Sprint_" + d for d in DIRECTIONS]
         + ["BS_CrouchIdle"] + ["BS_CrouchWalk_" + d for d in DIRECTIONS]
         + ["BS_StandToCrouch", "BS_CrouchToStand", "BS_Jump_Start", "BS_Jump_Loop", "BS_Jump_Land"]
         + ["BS_Look_" + n for n in LOOKS])
OTHER_ACTIONS = ["BS_Walk", "BS_FaceTest", "BS_FlexTest"]
assets = unreal.EditorAssetLibrary


def fail(message):
    raise RuntimeError("BETTER_STEVE_IMPORT FAIL: " + message)


def member(enum, test):
    return next(getattr(enum, m) for m in dir(enum) if not m.startswith("_") and test(m.upper()))


MESH_SPACE = member(unreal.AdditiveAnimationType, lambda m: m.endswith("MESH_SPACE"))
ANIM_FRAME = member(unreal.AdditiveBasePoseType, lambda m: m.endswith("ANIM_FRAME") and "LOCAL" not in m)

manifest = json.loads((ART / "manifest.json").read_text(encoding="utf-8"))
fbx = ART / manifest["fbx"]
if hashlib.sha256(fbx.read_bytes()).hexdigest() != manifest["fbx_sha256"]:
    fail(fbx.name + " does not match manifest.json")
mesh_package = ProjectPaths.player_body_asset("SK_BetterSteve")
folder, _ = ProjectPaths.split(mesh_package)


def package(name):
    return folder + "/" + name


def verify():
    mesh = assets.load_asset(mesh_package)
    if not isinstance(mesh, unreal.SkeletalMesh):
        fail(mesh_package + " is not a skeletal mesh")
    tag = assets.get_metadata_tag(mesh, TAG)
    if not tag:
        fail(folder + " is an incomplete import (no " + TAG + " tag on the mesh); move it away and rerun")
    if tag != manifest["fbx_sha256"]:
        fail(folder + " was imported from another FBX; move it away deliberately and rerun")
    height = mesh.get_bounds().box_extent.z * 2
    if abs(height - 187.5) > .5:
        fail("SK_BetterSteve is %.2f cm tall, not 187.5" % height)
    skeleton = mesh.get_editor_property("skeleton")
    bones = len(unreal.AnimPoseExtensions.get_bone_names(unreal.AnimPoseExtensions.get_reference_pose(skeleton)))
    if bones != 56:
        fail("SK_BetterSteve has %d bones, not 56" % bones)
    base = assets.load_asset(package("BS_Look_C")) if assets.does_asset_exist(package("BS_Look_C")) else None
    for name in CLIPS + OTHER_ACTIONS:
        seq = assets.load_asset(package(name)) if assets.does_asset_exist(package(name)) else None
        if not isinstance(seq, unreal.AnimSequence):
            fail("missing AnimSequence " + name)
        if seq.get_editor_property("skeleton") != skeleton:
            fail(name + " is on another skeleton")
        look = name.startswith("BS_Look_")
        if (str(seq.get_editor_property("additive_anim_type")) == str(MESH_SPACE)) != look:
            fail(name + (" is not mesh-space additive" if look else " is additive"))
        # The look poses' base: BS_Look_C frame 0 (BS_Look_C on itself, an identity offset: the grid's centre).
        if look and (str(seq.get_editor_property("ref_pose_type")) != str(ANIM_FRAME)
                     or seq.get_editor_property("ref_pose_seq") != base
                     or seq.get_editor_property("ref_frame_index") != 0):
            fail(name + "'s additive base is not BS_Look_C frame 0")
    texture = assets.load_asset(package("T_BetterSteve"))
    if texture.get_editor_property("filter") != unreal.TextureFilter.TF_NEAREST or \
            texture.get_editor_property("mip_gen_settings") != unreal.TextureMipGenSettings.TMGS_NO_MIPMAPS:
        fail("T_BetterSteve is not Nearest without mips")
    redirectors = [p for p in assets.list_assets(folder, recursive=True)
                   if assets.find_asset_data(p).asset_class_path.asset_name == "ObjectRedirector"]
    if redirectors:
        fail("redirectors left in " + folder + ": " + ", ".join(redirectors))
    return {"folder": folder, "height_cm": round(height, 2), "bones": bones,
            "anim_sequences": len(CLIPS + OTHER_ACTIONS), "assets": len(assets.list_assets(folder, recursive=True))}


imported = False
if not assets.does_asset_exist(mesh_package):
    if assets.does_directory_exist(folder) and assets.list_assets(folder, recursive=True):
        fail(folder + " holds assets but no SK_BetterSteve; move it away deliberately and rerun")
    task = unreal.AssetImportTask()
    task.filename, task.destination_path = str(fbx), folder
    task.automated, task.replace_existing, task.save = True, False, True
    unreal.AssetToolsHelpers.get_asset_tools().import_asset_tasks([task])
    if not assets.does_asset_exist(mesh_package):
        fail("the FBX import made no " + mesh_package)
    # The actions take their own names: BS_Walk_F, not SK_BetterSteveArmature_BS_Walk_F.
    for name in CLIPS + OTHER_ACTIONS:
        if not assets.rename_asset(package(IMPORT_PREFIX + name), package(name)):
            fail("could not rename " + IMPORT_PREFIX + name)
    texture = assets.load_asset(package("T_BetterSteve"))
    texture.set_editor_property("filter", unreal.TextureFilter.TF_NEAREST)
    texture.set_editor_property("mip_gen_settings", unreal.TextureMipGenSettings.TMGS_NO_MIPMAPS)
    assets.save_loaded_asset(texture)
    base = assets.load_asset(package("BS_Look_C"))
    for name in LOOKS:
        seq = assets.load_asset(package("BS_Look_" + name))
        seq.set_editor_property("additive_anim_type", MESH_SPACE)
        seq.set_editor_property("ref_pose_type", ANIM_FRAME)
        seq.set_editor_property("ref_pose_seq", base)
        seq.set_editor_property("ref_frame_index", 0)
        assets.save_loaded_asset(seq)
    mesh = assets.load_asset(mesh_package)
    assets.set_metadata_tag(mesh, TAG, manifest["fbx_sha256"])
    assets.save_loaded_asset(mesh)
    imported = True

summary = verify()
summary["imported"] = imported
unreal.log("BETTER_STEVE_IMPORT OK " + json.dumps(summary))
