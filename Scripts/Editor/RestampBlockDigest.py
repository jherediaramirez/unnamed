"""Re-stamp DA_Blocks with the block reference's current digest, after an approved additive reference extension
(new files only: every file the block import reads is unchanged). It changes only DA_Blocks.SourceSha256, so
ImportBlocks.py verifies again instead of refusing. It exports DA_Blocks as T3D before and after, into the folder
named by the RESTAMP_T3D environment variable, so a diff can prove nothing else moved:
    $env:RESTAMP_T3D = '<fresh folder>'
    & 'C:\\Program Files\\Epic Games\\UE_5.8\\Engine\\Binaries\\Win64\\UnrealEditor-Cmd.exe' Unnamed.uproject
        -run=pythonscript "-script=Scripts\\Editor\\RestampBlockDigest.py" -unattended -nop4 -nosplash
Then run ImportBlocks.py: it must report BLOCK_IMPORT PASS with nothing rebuilt.
"""
import os
import sys
from pathlib import Path
import unreal

sys.path.insert(0, str(Path(__file__).resolve().parent))
sys.dont_write_bytecode = True
import ProjectPaths  # noqa: E402
from BlockImportData import source_sha256  # noqa: E402

ROOT = Path(unreal.Paths.project_dir()).resolve()
OUT = Path(os.environ.get("RESTAMP_T3D", ""))
assets = unreal.EditorAssetLibrary


def fail(message):
    raise RuntimeError("RESTAMP FAIL: " + message)


def export(asset, name):
    task = unreal.AssetExportTask()
    task.set_editor_property("object", asset)
    task.set_editor_property("filename", str(OUT / name))
    task.set_editor_property("automated", True)
    task.set_editor_property("prompt", False)
    task.set_editor_property("replace_identical", True)
    if not unreal.Exporter.run_asset_export_task(task):
        fail("T3D export failed: " + name)


if not os.environ.get("RESTAMP_T3D") or not OUT.is_dir() or any(OUT.iterdir()):
    fail("RESTAMP_T3D must name an existing empty folder")
path = ProjectPaths.block_asset("DA_Blocks")
if not assets.does_asset_exist(path):
    fail("DA_Blocks does not exist; run ImportBlocks.py instead")
registry = assets.load_asset(path)
old, new = registry.get_editor_property("source_sha256"), source_sha256(ROOT)
export(registry, "DA_Blocks.before.t3d")
if old == new:
    unreal.log("RESTAMP PASS: DA_Blocks already carries %s; nothing changed" % new)
    sys.exit(0)
registry.set_editor_property("source_sha256", new)
if not assets.save_loaded_asset(registry, only_if_is_dirty=False):
    fail("saving DA_Blocks failed")
export(registry, "DA_Blocks.after.t3d")
unreal.log("RESTAMP PASS: DA_Blocks SourceSha256 %s -> %s" % (old, new))
