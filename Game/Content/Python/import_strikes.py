"""Import the strike clips (decision 0025) onto the base character's skeleton.

Source: Art/Characters/Base/Strikes/*.fbx, keyframed by build_strikes.py on the committed
character's own rig, so they need no retarget: they are imported straight onto
SK_BaseCharacter_Skeleton as AnimSequences in /Game/Characters/Base/Strikes. The strike component
plays them in the DefaultSlot as dynamic montages; strikes.json next to the FBX files gives each
clip's active frames, which CSStrikeMove.cpp carries as its defaults.

Run headless:
    UnrealEditor-Cmd.exe Game/CombatSimulator.uproject -run=pythonscript -script=Game/Content/Python/import_strikes.py
Safe to rerun: clips are reimported over the old ones.
"""

import json
import os

import unreal

DEST = "/Game/Characters/Base/Strikes"
SKELETON = "/Game/Characters/Base/SK_BaseCharacter_Skeleton"


def log(msg):
    unreal.log("import_strikes: " + msg)


def source_dir():
    game_dir = os.path.abspath(unreal.Paths.convert_relative_path_to_full(unreal.Paths.project_dir()))
    return os.path.join(os.path.dirname(game_dir), "Art", "Characters", "Base", "Strikes")


def import_clip(path, skeleton):
    task = unreal.AssetImportTask()
    task.filename = path
    task.destination_path = DEST
    task.automated = True
    task.replace_existing = True
    task.save = False
    pipeline = unreal.InterchangeGenericAssetsPipeline()
    common = pipeline.get_editor_property("common_skeletal_meshes_and_animations_properties")
    common.set_editor_property("skeleton", skeleton)
    common.set_editor_property("import_only_animations", True)
    pipeline.get_editor_property("mesh_pipeline").set_editor_property("import_skeletal_meshes", False)
    pipeline.get_editor_property("mesh_pipeline").set_editor_property("import_static_meshes", False)
    pipeline.get_editor_property("animation_pipeline").set_editor_property("import_animations", True)
    pipeline.get_editor_property("material_pipeline").set_editor_property("import_materials", False)
    override = unreal.InterchangePipelineStackOverride()
    override.add_pipeline(pipeline)
    task.options = override
    unreal.AssetToolsHelpers.get_asset_tools().import_asset_tasks([task])
    return [str(p) for p in task.imported_object_paths]


def main():
    skeleton = unreal.load_asset(SKELETON)
    if not skeleton:
        raise RuntimeError("missing " + SKELETON)
    folder = source_dir()
    meta = json.load(open(os.path.join(folder, "strikes.json"), encoding="utf-8"))
    for name, info in meta.items():
        path = os.path.join(folder, name + ".fbx")
        with open(path, "rb") as f:
            if f.read(64).startswith(b"version https://git-lfs"):
                raise RuntimeError(path + " is a Git LFS pointer; run git lfs pull")
        imported = import_clip(path, skeleton)
        asset_path = DEST + "/" + name
        anim = unreal.load_asset(asset_path)
        if not isinstance(anim, unreal.AnimSequence):
            # Interchange names an animation-only import after the file's take; find it.
            seqs = [unreal.load_asset(p.split(".")[0]) for p in imported]
            seqs = [s for s in seqs if isinstance(s, unreal.AnimSequence)]
            if not seqs:
                raise RuntimeError("no AnimSequence from " + path + ": " + repr(imported))
            anim = seqs[0]
            if anim.get_path_name().split(".")[0] != asset_path:
                unreal.EditorAssetLibrary.delete_asset(asset_path) if unreal.EditorAssetLibrary.does_asset_exist(asset_path) else None
                unreal.EditorAssetLibrary.rename_asset(anim.get_path_name().split(".")[0], asset_path)
                anim = unreal.load_asset(asset_path)
        anim.set_editor_property("enable_root_motion", False)
        length = anim.get_play_length()
        log("%s: %.3f s (built %.3f s), skeleton %s" % (name, length, info["seconds"], anim.get_editor_property("skeleton").get_name()))
        if abs(length - info["seconds"]) > 0.05:
            raise RuntimeError("%s imported %.3f s long, built %.3f s" % (name, length, info["seconds"]))
        unreal.EditorAssetLibrary.save_loaded_asset(anim)
    # Drop anything else the import left in the folder (a skeleton or mesh from the file).
    for p in unreal.EditorAssetLibrary.list_assets(DEST, recursive=False):
        a = unreal.load_asset(p.split(".")[0])
        if not isinstance(a, unreal.AnimSequence):
            log("removing stray " + p)
            unreal.EditorAssetLibrary.delete_asset(p.split(".")[0])
    log("done")


main()
