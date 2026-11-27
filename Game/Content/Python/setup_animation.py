"""Stand-in locomotion for the base character: CC0 Quaternius clips retargeted onto SK_BaseCharacter,
two speed blend spaces and ABP_BaseCharacter.

Source: Quaternius Universal Animation Library, Standard (free) tier, CC0-1.0, ledger row
quaternius-universal-animation-library in assets/THIRD_PARTY.csv. The pack is not committed: download
it from https://quaternius.com/packs/universalanimationlibrary.html (Download, then "Just give me the
Download"; itch.io name-your-own-price, 0 is accepted) and unzip it under
_scratch/quaternius_ual/unpacked/ (gitignored), or point CS_UAL1_GLB at UAL1_Standard.glb.
These clips are stand-ins, replaced as the project's own animations arrive.

Makes or refreshes:
  /Game/ThirdParty/Quaternius/UAL1/Source/       the pack's mannequin (UAL1_Standard, its skeleton,
                                                 materials) and the CLIPS below, imported from the
                                                 glTF file; the pack's other clips are deleted
  /Game/ThirdParty/Quaternius/UAL1/IK_UAL1_Source          IK Rig of the pack's skeleton (it was
                                                 IK_UAL1_Mannequin; a rerun deletes that name, which
                                                 the repo's Epic-name guard rejects)
  /Game/ThirdParty/Quaternius/UAL1/RTG_UAL1_To_BaseCharacter  IK Retargeter, pack -> base character
  /Game/ThirdParty/Quaternius/UAL1/Retargeted/A_<clip>     the CLIPS on the base skeleton
  /Game/Characters/Base/IK_BaseCharacter         IK Rig of the base skeleton
  /Game/Characters/Base/BS_BaseCharacter_Ground  1D blend space on speed: idle, walk, jog, sprint
  /Game/Characters/Base/BS_BaseCharacter_Crouch  1D blend space on speed: crouch idle, crouch walk
  /Game/Characters/Base/ABP_BaseCharacter        animation Blueprint, parent UCSAnimInstance
                                                 (CSCharacter plugin), graph built by
                                                 UCSAnimGraphBuilder (CSCharacterEditor)

Retargeting (UE 5.8 IK Retargeter): both IK Rigs get the engine's auto-generated retarget chains
(both skeletons use UE5 humanoid bone names) and auto full-body IK; the retargeter gets the default
op stack (pelvis motion, FK chains, IK chains, IK solve, root motion), exact chain mapping, and the
target's retarget pose auto-aligned to the source's. The pack's mannequin is in a T-pose and the
base character in an A-pose (Art/Characters/Base/NOTES.md); the alignment turns the base
character's retarget pose into the T-pose so the arms are not off by 55 degrees. Both meshes face +Y.

Blend spaces: laid out by gait, not by speed. Ground has idle, walk, jog and sprint at X 0, 1, 2, 3;
Crouch has crouch idle and crouch walk at X 0 and 1. Every sample plays at rate 1. At run time
UCSAnimInstance works out where the ground speed falls between the gait speeds in use
(LocomotionAxis) and the play rate that makes the feet match the ground (LocomotionPlayRate), from
each clip's stride speed and length. This script writes those to GroundStride and CrouchStride on
the animation Blueprint's class defaults. A clip's stride speed is its root-motion speed in the
pack's root-motion file, scaled by the pelvis height ratio (how the retargeter scales the pelvis).
Changing gait speeds in the tuning asset, or in a running build, needs no rerun.

ABP_BaseCharacter's AnimGraph: the pose for UCSAnimInstance::PoseIndex (POSES below, in
ECSLocomotionPose order) -> Slot DefaultSlot -> Output Pose; the blend spaces read LocomotionAxis
and LocomotionPlayRate.

Graph only: with the environment variable CS_ANIM_GRAPH_ONLY=1 the script rebuilds just the
animation Blueprint's graph from the assets that are already there (after a change to
UCSAnimGraphBuilder, for example). It needs no pack and touches no clip, blend space or stride table.

Safe to rerun: clips are reimported and retargeted over the old ones, the IK Rigs and retargeter
are rebuilt, the blend spaces refilled and the AnimGraph rebuilt.

Run headless (editor closed):
    UnrealEditor-Cmd.exe <repo>/Game/CombatSimulator.uproject -run=pythonscript
        -script="<repo>/Game/Content/Python/setup_animation.py" -unattended -nosplash -nullrhi
or from a running editor through the Unreal MCP server (call_tool, Python execution).
"""

import json
import math
import os
import struct

import unreal

PACK = "/Game/ThirdParty/Quaternius/UAL1"
SOURCE = PACK + "/Source"
RETARGETED = PACK + "/Retargeted"
SOURCE_IK_RIG = PACK + "/IK_UAL1_Source"
# Earlier names of assets this script makes; deleted once nothing points at them.
RETIRED = [PACK + "/IK_UAL1_Mannequin"]
RETARGETER = PACK + "/RTG_UAL1_To_BaseCharacter"
BASE = "/Game/Characters/Base"
BASE_MESH = BASE + "/SK_BaseCharacter"
BASE_IK_RIG = BASE + "/IK_BaseCharacter"
TUNING = BASE + "/DA_MovementTuning"
BS_GROUND = "BS_BaseCharacter_Ground"
BS_CROUCH = "BS_BaseCharacter_Crouch"
BS_STANCE = "BS_BaseCharacter_Stance"
# The stance legs (decision 0025), from import_strikes.py, with the shuffle's stride from strikes.json.
STRIKES = "/Game/Characters/Base/Strikes"
ABP = "ABP_BaseCharacter"

IMPORT_PREFIX = "UAL1_Standard"  # Interchange names clips <file name><clip name>
SOURCE_MESH = SOURCE + "/" + IMPORT_PREFIX

# Clips used (pack names). Every other clip of the pack is deleted after import.
CLIPS = ["Idle_Loop", "Walk_Loop", "Jog_Fwd_Loop", "Sprint_Loop", "Crouch_Idle_Loop", "Crouch_Fwd_Loop",
         "Jump_Start", "Jump_Loop", "Jump_Land"]

# Graph poses in ECSLocomotionPose order (CSAnimInstance.h): (asset, loop, blend-in seconds).
# The asset is a blend space name or a clip name.
POSES = [
    (BS_GROUND, True, 0.2),     # Ground: idle, walk, jog, sprint by speed
    (BS_CROUCH, True, 0.25),    # Crouch: crouch idle, crouch walk by speed
    ("Jump_Start", False, 0.1),  # JumpStart: plays once
    ("Jump_Loop", True, 0.25),  # Fall
    ("Jump_Land", False, 0.1),  # Land: plays once (UCSAnimInstance cuts it short)
    (BS_STANCE, True, 0.2),     # Stance: stance idle, shuffle, jog by speed (the combat stance)
]

# project_dir() is <repo>/Game/ (trailing slash); abspath drops the slash.
REPO = os.path.dirname(os.path.abspath(unreal.Paths.convert_relative_path_to_full(unreal.Paths.project_dir())))
PACK_DIR = os.path.join(REPO, "_scratch", "quaternius_ual", "unpacked", "Universal Animation Library[Standard]", "Unreal-Godot")


def log(msg):
    unreal.log("setup_animation: " + msg)


def fail(msg):
    raise RuntimeError("setup_animation: " + msg)


def glb_path(name):
    path = os.environ.get("CS_UAL1_GLB") if name == "UAL1_Standard.glb" else None
    path = path or os.path.join(PACK_DIR, name)
    if not os.path.isfile(path):
        fail("missing %s; download the Universal Animation Library (Standard) as this script's docstring says" % path)
    return path


def asset_tools():
    return unreal.AssetToolsHelpers.get_asset_tools()


def save(asset):
    if not unreal.EditorAssetLibrary.save_loaded_asset(asset, False):
        fail("could not save " + asset.get_path_name())


def delete_if_exists(path):
    lib = unreal.EditorAssetLibrary
    if lib.does_asset_exist(path) and not lib.delete_asset(path):
        fail("could not delete " + path)


# ----- glTF reading (no engine): root-motion stride speeds -----

def read_glb(path):
    with open(path, "rb") as f:
        data = f.read()
    json_len = struct.unpack_from("<I", data, 12)[0]
    doc = json.loads(data[20:20 + json_len])
    bin_start = 20 + json_len + 8
    return doc, data, bin_start


def accessor_values(doc, data, bin_start, index):
    acc = doc["accessors"][index]
    view = doc["bufferViews"][acc["bufferView"]]
    width = {"SCALAR": 1, "VEC3": 3, "VEC4": 4}[acc["type"]]
    offset = bin_start + view.get("byteOffset", 0) + acc.get("byteOffset", 0)
    stride = view.get("byteStride", 4 * width)
    return [struct.unpack_from("<%df" % width, data, offset + i * stride) for i in range(acc["count"])]


def stride_speeds_cm_s(path):
    """Average speed of the root bone over each clip of a root-motion glTF file, cm/s."""
    doc, data, bin_start = read_glb(path)
    nodes = doc["nodes"]
    speeds = {}
    for anim in doc["animations"]:
        speeds[anim["name"]] = 0.0
        for ch in anim["channels"]:
            target = ch["target"]
            if target["path"] != "translation" or nodes[target["node"]].get("name") != "root":
                continue
            sampler = anim["samplers"][ch["sampler"]]
            times = accessor_values(doc, data, bin_start, sampler["input"])
            moves = accessor_values(doc, data, bin_start, sampler["output"])
            duration = times[-1][0] - times[0][0]
            if duration > 0:
                dist = math.sqrt(sum((moves[-1][k] - moves[0][k]) ** 2 for k in range(3)))
                speeds[anim["name"]] = 100.0 * dist / duration
    return speeds


# ----- Import -----

def import_pack():
    pipeline = unreal.InterchangeGenericAssetsPipeline()
    pipeline.get_editor_property("mesh_pipeline").set_editor_property("build_nanite", False)
    anim = pipeline.get_editor_property("animation_pipeline")
    # The pack's Unreal setup: bake at 30 Hz (the clips are 30 fps) and snap to the closest frame.
    anim.set_editor_property("use30_hz_to_bake_bone_animation", True)
    anim.set_editor_property("frame_alignment", unreal.InterchangeFrameAlignment.SNAP_TO_CLOSEST)
    # Interchange ignores a bare pipeline in the task options; it takes a stack override.
    stack = unreal.InterchangePipelineStackOverride()
    stack.add_pipeline(pipeline)

    task = unreal.AssetImportTask()
    task.filename = glb_path("UAL1_Standard.glb")
    task.destination_path = SOURCE
    task.automated = True
    task.replace_existing = True
    task.save = False
    task.set_editor_property("options", stack)
    asset_tools().import_asset_tasks([task])
    paths = [str(p) for p in task.imported_object_paths]
    log("imported %s (%d assets new or replaced)" % (os.path.basename(task.filename), len(paths)))

    mesh = unreal.load_asset(SOURCE_MESH)
    if not isinstance(mesh, unreal.SkeletalMesh):
        fail("import did not make skeletal mesh " + SOURCE_MESH)

    keep = set(IMPORT_PREFIX + c for c in CLIPS)
    deleted = 0
    for path in paths:
        asset = unreal.load_asset(path)
        name = path.split("/")[-1].split(".")[0]
        if isinstance(asset, unreal.AnimSequence) and name not in keep:
            delete_if_exists(path.split(".")[0])
            deleted += 1
    for clip in CLIPS:
        if not isinstance(unreal.load_asset(SOURCE + "/" + IMPORT_PREFIX + clip), unreal.AnimSequence):
            fail("the pack has no clip " + clip)
    log("kept %d clips, deleted the pack's other %d" % (len(CLIPS), deleted))
    return mesh


# ----- IK Rigs and retargeter -----

def load_or_create_at(path, cls, factory):
    """The asset at path, made with factory when missing. Reused rather than deleted and remade:
    a just-deleted asset stays in memory while the commandlet runs, so remaking it fails."""
    folder, name = path.rsplit("/", 1)
    asset = unreal.load_asset(path) if unreal.EditorAssetLibrary.does_asset_exist(path) else None
    if asset is None:
        asset = asset_tools().create_asset(name, folder, cls, factory)
    if not isinstance(asset, cls):
        fail("could not make " + path)
    return asset


def make_ik_rig(path, mesh):
    rig = load_or_create_at(path, unreal.IKRigDefinition, unreal.IKRigDefinitionFactory())
    name = rig.get_name()
    ctrl = unreal.IKRigController.get_controller(rig)
    if not ctrl.set_skeletal_mesh(mesh):
        fail("could not set %s on %s" % (mesh.get_name(), name))
    # Start from an empty rig so a rerun gives the same result.
    for chain in list(ctrl.get_retarget_chains()):
        ctrl.remove_retarget_chain(chain.chain_name)
    while ctrl.get_num_solvers() > 0:
        ctrl.remove_solver(0)
    for goal in list(ctrl.get_all_goals()):
        ctrl.remove_goal(goal.get_editor_property("goal_name"))
    if not ctrl.apply_auto_generated_retarget_definition():
        fail("no auto retarget definition for " + mesh.get_name())
    if not ctrl.apply_auto_fbik():
        fail("no auto full-body IK for " + mesh.get_name())
    chains = [str(c.chain_name) for c in ctrl.get_retarget_chains()]
    log("%s: retarget root %s, %d chains (%s), %d IK goals" % (
        name, ctrl.get_retarget_root(), len(chains), ", ".join(chains), len(ctrl.get_all_goals())))
    save(rig)
    return rig, ctrl, chains


def ref_location(ctrl, bone):
    return ctrl.get_ref_pose_transform_of_bone(bone).translation


def check_facing(ctrl, label):
    """Both meshes must face +Y (toes ahead of the ankle) for the retarget pose to line up."""
    toe = ref_location(ctrl, "ball_l")
    ankle = ref_location(ctrl, "foot_l")
    if toe.y - ankle.y < 3.0:
        fail("%s does not face +Y (ball_l y %.1f, foot_l y %.1f)" % (label, toe.y, ankle.y))
    pelvis = ref_location(ctrl, "pelvis")
    log("%s: faces +Y, pelvis height %.1f cm" % (label, pelvis.z))
    return pelvis.z


def make_retargeter(source_rig, target_rig, source_mesh, target_mesh, target_chains):
    rtg = load_or_create_at(RETARGETER, unreal.IKRetargeter, unreal.IKRetargetFactory())
    name = rtg.get_name()
    ctrl = unreal.IKRetargeterController.get_controller(rtg)
    src, tgt = unreal.RetargetSourceOrTarget.SOURCE, unreal.RetargetSourceOrTarget.TARGET
    ctrl.remove_all_ops()
    ctrl.set_ik_rig(src, source_rig)
    ctrl.set_ik_rig(tgt, target_rig)
    ctrl.set_preview_mesh(src, source_mesh)
    ctrl.set_preview_mesh(tgt, target_mesh)
    ctrl.add_default_ops()
    ctrl.assign_ik_rig_to_all_ops(src, source_rig)
    ctrl.assign_ik_rig_to_all_ops(tgt, target_rig)
    ctrl.auto_map_chains(unreal.AutoMapChainType.EXACT, True)

    mapped, unmapped = [], []
    for chain in target_chains:
        source = str(ctrl.get_source_chain(chain))
        (mapped if source not in ("", "None") else unmapped).append(chain)
    for required in ["Spine", "Neck", "Head", "LeftArm", "RightArm", "LeftLeg", "RightLeg", "LeftClavicle", "RightClavicle"]:
        if required not in mapped:
            fail("target chain %s has no source chain" % required)
    log("%s: %d ops, %d chains mapped, unmapped (no source bones): %s" % (
        name, ctrl.get_num_retarget_ops(), len(mapped), ", ".join(unmapped) or "none"))

    # The pack's mannequin rests in a T-pose, the base character in an A-pose: turn the base
    # character's retarget pose to match, chain by chain.
    ctrl.reset_retarget_pose(ctrl.get_current_retarget_pose_name(tgt), [], tgt)
    ctrl.auto_align_all_bones(tgt, unreal.RetargetAutoAlignMethod.CHAIN_TO_CHAIN)
    arm = ctrl.get_rotation_offset_for_retarget_pose_bone("upperarm_l", tgt)
    log("%s: target retarget pose aligned to the source (upperarm_l turned %.1f deg)" % (
        name, math.degrees(2.0 * math.acos(min(1.0, abs(arm.w))))))
    save(rtg)
    return rtg


def retarget_clips(rtg, source_mesh, target_mesh):
    inputs = unreal.IKRetargetBatchOperationInputs()
    inputs.set_editor_property("assets_to_retarget", [
        unreal.EditorAssetLibrary.find_asset_data(SOURCE + "/" + IMPORT_PREFIX + c) for c in CLIPS])
    inputs.set_editor_property("source_mesh", source_mesh)
    inputs.set_editor_property("target_mesh", target_mesh)
    inputs.set_editor_property("ik_retarget_asset", rtg)
    inputs.set_editor_property("search", IMPORT_PREFIX)
    inputs.set_editor_property("replace", "A_")
    inputs.set_editor_property("target_path", RETARGETED)
    inputs.set_editor_property("use_source_path", False)
    inputs.set_editor_property("include_referenced_assets", False)
    inputs.set_editor_property("overwrite_existing_files", True)
    unreal.IKRetargetBatchOperation.run_batch_retarget(inputs)

    skeleton = target_mesh.get_editor_property("skeleton")
    clips = {}
    for clip in CLIPS:
        seq = unreal.load_asset(RETARGETED + "/A_" + clip)
        if not isinstance(seq, unreal.AnimSequence):
            fail("retargeting did not make " + RETARGETED + "/A_" + clip)
        if seq.get_editor_property("skeleton") != skeleton:
            fail("A_%s is not on the base skeleton" % clip)
        save(seq)
        clips[clip] = seq
    log("retargeted %d clips to %s" % (len(clips), RETARGETED))
    return clips


# ----- Blend spaces and animation Blueprint -----

def load_or_create(name, cls, factory):
    return load_or_create_at(BASE + "/" + name, cls, factory)


def make_blend_space(name, skeleton, axis_max, samples):
    factory = unreal.BlendSpaceFactory1D()
    factory.set_editor_property("target_skeleton", skeleton)
    bs = load_or_create(name, unreal.BlendSpace1D, factory)
    entries = [unreal.CSBlendSample(animation=seq, position=pos, rate_scale=rate) for seq, pos, rate in samples]
    result = unreal.CSAnimGraphBuilder.set_blend_space1d(bs, "Gait", axis_max, entries)
    if not result.success:
        fail(result.message)
    log(result.message + ": " + ", ".join("%s at %.0f x%.2f" % (s.get_name(), p, r) for s, p, r in samples))
    save(bs)
    return bs


def make_anim_blueprint(skeleton, assets):
    factory = unreal.AnimBlueprintFactory()
    factory.set_editor_property("target_skeleton", skeleton)
    factory.set_editor_property("parent_class", unreal.CSAnimInstance)
    abp = load_or_create(ABP, unreal.AnimBlueprint, factory)
    poses = [unreal.CSPoseSource(asset=assets[a], loop=loop, blend_in_seconds=blend) for a, loop, blend in POSES]
    result = unreal.CSAnimGraphBuilder.build_locomotion_graph(
        abp, poses, "PoseIndex", "LocomotionAxis", "LocomotionPlayRate", "DefaultSlot", "StrideScale", "LegYaw", "UpperBody", "spine_01")
    if not result.success:
        fail(result.message)
    for line in result.message.splitlines():
        log(line)
    save(abp)
    return abp


def set_stride_tables(abp, ground, crouch, stance):
    """Writes (stride speed cm/s, clip seconds) per blend sample to the Blueprint's class defaults."""
    defaults = unreal.get_default_object(abp.generated_class())
    for prop, rows in (("ground_stride", ground), ("crouch_stride", crouch), ("stance_stride", stance)):
        defaults.set_editor_property(prop, [unreal.CSStrideSample(stride_speed=speed, seconds=seconds) for speed, seconds in rows])
        log("%s: %s" % (prop, ", ".join("%.0f cm/s over %.2f s" % row for row in rows)))
    abp.modify()
    save(abp)


def main():
    base_mesh = unreal.load_asset(BASE_MESH)
    if not isinstance(base_mesh, unreal.SkeletalMesh):
        fail("missing %s; run import_base_character.py first" % BASE_MESH)
    tuning = unreal.load_asset(TUNING)
    if tuning is None:
        fail("missing %s; run import_base_character.py first" % TUNING)
    walk, jog, sprint, crouch = (tuning.get_editor_property(p) for p in ("walk_speed", "jog_speed", "sprint_speed", "crouch_speed"))
    log("gait speeds in %s (for the log only): walk %.0f, jog %.0f, sprint %.0f, crouch %.0f cm/s" % (TUNING, walk, jog, sprint, crouch))

    source_mesh = import_pack()
    source_rig, source_ctrl, _ = make_ik_rig(SOURCE_IK_RIG, source_mesh)
    target_rig, target_ctrl, target_chains = make_ik_rig(BASE_IK_RIG, base_mesh)
    source_pelvis = check_facing(source_ctrl, "pack mannequin")
    target_pelvis = check_facing(target_ctrl, "base character")
    scale = target_pelvis / source_pelvis

    rtg = make_retargeter(source_rig, target_rig, source_mesh, base_mesh, target_chains)
    for path in RETIRED:
        if unreal.EditorAssetLibrary.does_asset_exist(path):
            delete_if_exists(path)
            log("deleted retired " + path)
        # A force delete in the commandlet can leave the file on disk; remove it too.
        content = unreal.Paths.convert_relative_path_to_full(unreal.Paths.project_content_dir())
        stale = os.path.join(content, path[len("/Game/"):] + ".uasset")
        if os.path.isfile(stale):
            os.remove(stale)
            log("removed retired file " + stale)
    clips = retarget_clips(rtg, source_mesh, base_mesh)

    strides = stride_speeds_cm_s(glb_path("UAL1_Standard_RM.glb"))
    def stride(clip, speed=None):
        """(stride speed on the base character, clip length); speed is the gait speed, for the log."""
        seconds = clips[clip].get_play_length()
        if seconds <= 0.0:
            fail("clip %s has no length" % clip)
        if speed is None:
            return (0.0, seconds)
        natural = strides.get(clip, 0.0) * scale
        if natural <= 0.0:
            fail("no root-motion stride speed for " + clip)
        log("  %s: stride %.0f cm/s on the base character over %.2f s, plays x%.2f at %.0f cm/s"
            % (clip, natural, seconds, speed / natural, speed))
        return (natural, seconds)

    ground_clips = [("Idle_Loop", None), ("Walk_Loop", walk), ("Jog_Fwd_Loop", jog), ("Sprint_Loop", sprint)]
    crouch_clips = [("Crouch_Idle_Loop", None), ("Crouch_Fwd_Loop", crouch)]
    skeleton = base_mesh.get_editor_property("skeleton")
    ground = make_blend_space(BS_GROUND, skeleton, float(len(ground_clips) - 1),
                              [(clips[c], float(i), 1.0) for i, (c, _) in enumerate(ground_clips)])
    crouch_bs = make_blend_space(BS_CROUCH, skeleton, float(len(crouch_clips) - 1),
                                 [(clips[c], float(i), 1.0) for i, (c, _) in enumerate(crouch_clips)])

    stance_idle = unreal.load_asset(STRIKES + "/A_Stance_Idle")
    shuffle = unreal.load_asset(STRIKES + "/A_Stance_Shuffle")
    if stance_idle is None or shuffle is None:
        fail("missing the stance clips in %s; run import_strikes.py first" % STRIKES)
    meta = json.load(open(os.path.join(REPO, "Art", "Characters", "Base", "Strikes", "strikes.json"), encoding="utf-8"))
    shuffle_stride = float(meta["A_Stance_Shuffle"]["stride_cm_s"])
    log("  A_Stance_Shuffle: stride %.0f cm/s over %.2f s, plays x%.2f at %.0f cm/s"
        % (shuffle_stride, shuffle.get_play_length(), walk / shuffle_stride, walk))
    stance_bs = make_blend_space(BS_STANCE, skeleton, 2.0,
                                 [(stance_idle, 0.0, 1.0), (shuffle, 1.0, 1.0), (clips["Jog_Fwd_Loop"], 2.0, 1.0)])
    stance_rows = [(0.0, stance_idle.get_play_length()), (shuffle_stride, shuffle.get_play_length()), stride("Jog_Fwd_Loop", jog)]

    assets = {BS_GROUND: ground, BS_CROUCH: crouch_bs, BS_STANCE: stance_bs}
    assets.update(clips)
    abp = make_anim_blueprint(skeleton, assets)
    set_stride_tables(abp, [stride(c, s) for c, s in ground_clips], [stride(c, s) for c, s in crouch_clips], stance_rows)

    for folder in (PACK, BASE):
        if not unreal.EditorAssetLibrary.save_directory(folder, only_if_is_dirty=True, recursive=True):
            fail("could not save " + folder)
    log("done")


def graph_only():
    """Rebuilds the animation Blueprint's graph from the assets already in the project."""
    base_mesh = unreal.load_asset(BASE_MESH)
    if base_mesh is None:
        fail("no " + BASE_MESH)
    assets = {}
    for name, _, _ in POSES:
        path = BASE + "/" + name if name in (BS_GROUND, BS_CROUCH, BS_STANCE) else RETARGETED + "/A_" + name
        asset = unreal.load_asset(path)
        if asset is None:
            fail("no " + path + "; run the script without CS_ANIM_GRAPH_ONLY first")
        assets[name] = asset
    make_anim_blueprint(base_mesh.get_editor_property("skeleton"), assets)
    log("graph rebuilt")


if os.environ.get("CS_ANIM_GRAPH_ONLY") == "1":
    graph_only()
else:
    main()
