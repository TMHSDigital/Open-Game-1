"""Imports the base character into /Game/Characters/Base/ and builds its Unreal-side assets.

Makes or refreshes:
  SK_BaseCharacter            skeletal mesh, from Art/Characters/Base/SK_BaseCharacter.fbx, with the
                              socket `eyes` on the head bone (first-person camera, EYES_CM below)
  SK_BaseCharacter_Skeleton   its skeleton (UE5 humanoid bone names, 89 bones)
  M_BaseCharacter             the material instance the import makes for the one material slot
  PHYS_BaseCharacter          ragdoll physics asset: a capsule per major bone, human-range joint
                              limits (RAGDOLL below), built by UCSRagdollBuilder (CSCharacter plugin)
  DA_MovementTuning           UCSMovementTuning with the starting values; created only when missing,
                              so a rerun never overwrites tuning done in the editor

The FBX comes from a centimetre scene (decision 0012), so the default UE 5.8 import (Interchange,
no option changes) brings it in at scale 1 with the root at the origin, 180 cm tall, facing +Y.
The import's own SK_BaseCharacter_PhysicsAsset is replaced by PHYS_BaseCharacter and deleted.

Safe to rerun: the mesh is reimported in place and the physics asset rebuilt from scratch.

Run headless (editor closed):
    UnrealEditor-Cmd.exe <repo>/Game/CombatSimulator.uproject -run=pythonscript
        -script="<repo>/Game/Content/Python/import_base_character.py" -unattended -nosplash -nullrhi
or from a running editor through the Unreal MCP server (call_tool, Python execution).
"""

import os

import unreal

DEST = "/Game/Characters/Base"
MESH_NAME = "SK_BaseCharacter"
PHYS_PATH = DEST + "/PHYS_BaseCharacter"
AUTO_PHYS_PATH = DEST + "/SK_BaseCharacter_PhysicsAsset"
TUNING_NAME = "DA_MovementTuning"
EXPECTED_BONES = 89
EXPECTED_HEIGHT_CM = 180.0

# First-person camera socket: between the eyes, in mesh space (cm; the mesh faces +Y, feet at the
# origin). Build_base_character.py makes a featureless egg head whose widest ring (head_mid) is at
# 167 cm with its front surface about 12 cm ahead of the head axis; real eyes sit about 12 cm below
# the crown (180 cm) and 2 to 3 cm behind the face surface. The socket's X axis looks forward (+Y).
EYE_SOCKET = "eyes"
EYE_BONE = "head"
EYES_CM = (0.0, 9.5, 168.0)

# (bone, swing deg, twist deg, hinge, flex deg, extend deg). Ball joints swing up to `swing` in any
# direction from the rest (A-pose) direction, unless CONES below moves and shapes the cone; hinges
# bend from -extend to +flex measured from straight, with `swing` off the hinge plane. Ranges are a
# healthy adult's, trimmed a little so a ragdoll does not look broken. The parent of each body is
# its nearest ancestor in this list. Elbow and knee bend directions were checked in a simulated
# ragdoll (CombatSimulator.CS.Body.HingesBendTheRightWay).
RAGDOLL = [
    ("pelvis", 0, 0, False, 0, 0),  # root body, no joint
    ("spine_01", 20, 15, False, 0, 0),
    ("spine_03", 20, 15, False, 0, 0),
    ("spine_05", 15, 15, False, 0, 0),
    ("head", 40, 50, False, 0, 0),  # head and neck together on spine_05
    ("upperarm_l", 105, 45, False, 0, 0),  # cone from CONES
    ("upperarm_r", 105, 45, False, 0, 0),
    ("lowerarm_l", 5, 40, True, 145, 5),  # elbow; twist is forearm turn
    ("lowerarm_r", 5, 40, True, 145, 5),
    ("hand_l", 50, 10, False, 0, 0),
    ("hand_r", 50, 10, False, 0, 0),
    ("thigh_l", 75, 30, False, 0, 0),  # cone from CONES
    ("thigh_r", 75, 30, False, 0, 0),
    ("calf_l", 3, 5, True, 140, 2),  # knee
    ("calf_r", 3, 5, True, 140, 2),
    ("foot_l", 30, 10, False, 0, 0),
    ("foot_r", 30, 10, False, 0, 0),
]

# Hips and shoulders: a cone centred on the rest A-pose would be symmetric about it (the hip bending
# as far back as forward, the arm unable to rise above the shoulder). These cones are centred where
# a person's range is centred and are elliptical: bone (without side), cone centre and Swing1 axis
# as (forward, outward, up) in the mesh's space, the swing about that axis (`swing` in RAGDOLL) and
# the swing at right angles to it (swing2, degrees).
#   Hip: centre 50 deg forward of hanging and 10 deg out; 75 deg either way front to back gives
#   about 125 deg of flexion and 25 of extension, 45 sideways about 55 of abduction and 35 of
#   adduction (less with the leg straight).
#   Shoulder: a round 105 deg cone centred 10 deg below horizontal and 25 deg forward of straight out
#   to the side: overhead, hanging, a running swing back of 50 deg and across the chest are inside,
#   straight back at shoulder height and reaching behind the back are not. (An elliptical cone
#   separated these cases no better.)
CONES = {
    "thigh": ((0.766, 0.176, -0.643), (0.0, 1.0, 0.0), 45),
    "upperarm": ((0.416, 0.893, -0.174), None, 105),
}


def mesh_vector(side_sign, forward, outward, up):
    """Mesh space: +Y forward, +Z up, the character's left at +X."""
    return unreal.Vector(outward * side_sign, forward, up)


def cone_settings(bone):
    """(cone direction, Swing1 axis, swing2 deg) for a CONES bone, else None."""
    name, _, side = bone.rpartition("_")
    if name not in CONES or side not in ("l", "r"):
        return None
    sign = 1.0 if side == "l" else -1.0
    centre, axis, swing2 = CONES[name]
    direction = mesh_vector(sign, *centre)
    if axis is None:
        # Swing1 lifts the bone up and down: about the horizontal axis at right angles to the centre.
        axis_vec = unreal.Vector(direction.y, -direction.x, 0.0)
    else:
        axis_vec = mesh_vector(sign, *axis)
    return direction, axis_vec, swing2


def log(msg):
    unreal.log("import_base_character: " + msg)


def fbx_path():
    game_dir = os.path.abspath(unreal.Paths.convert_relative_path_to_full(unreal.Paths.project_dir()))
    path = os.path.join(os.path.dirname(game_dir), "Art", "Characters", "Base", MESH_NAME + ".fbx")
    if not os.path.isfile(path):
        raise RuntimeError("Missing " + path + " (git lfs pull?)")
    with open(path, "rb") as f:
        if f.read(64).startswith(b"version https://git-lfs"):
            raise RuntimeError(path + " is a Git LFS pointer; run git lfs pull")
    return path


def import_mesh():
    task = unreal.AssetImportTask()
    task.filename = fbx_path()
    task.destination_path = DEST
    task.automated = True
    task.replace_existing = True
    task.save = False
    unreal.AssetToolsHelpers.get_asset_tools().import_asset_tasks([task])
    paths = [str(p) for p in task.imported_object_paths]
    log("imported " + ", ".join(paths))
    mesh = unreal.load_asset(DEST + "/" + MESH_NAME)
    if not isinstance(mesh, unreal.SkeletalMesh):
        raise RuntimeError("Import did not produce skeletal mesh " + DEST + "/" + MESH_NAME + ": " + repr(paths))
    return mesh


def check_mesh(mesh):
    comp = unreal.new_object(unreal.SkeletalMeshComponent)
    comp.set_skeletal_mesh_asset(mesh)
    bones = comp.get_num_bones()
    if bones != EXPECTED_BONES:
        raise RuntimeError("Expected %d bones, got %d" % (EXPECTED_BONES, bones))
    if str(comp.get_bone_name(0)) != "root":
        raise RuntimeError("First bone is %s, not root" % comp.get_bone_name(0))
    bounds = mesh.get_bounds()
    lo = bounds.origin - bounds.box_extent
    hi = bounds.origin + bounds.box_extent
    height = hi.z - lo.z
    if abs(height - EXPECTED_HEIGHT_CM) > 1.0 or abs(lo.z) > 1.0:
        raise RuntimeError("Mesh spans z %.2f to %.2f cm; expected 0 to %.0f (scale 1)" % (lo.z, hi.z, EXPECTED_HEIGHT_CM))
    log("mesh: %d bones, z %.2f to %.2f cm, x %.1f to %.1f, y %.1f to %.1f (toes toward +y)"
        % (bones, lo.z, hi.z, lo.x, hi.x, lo.y, hi.y))


def add_eye_socket(mesh):
    """Adds (or moves) the `eyes` socket on the head bone so it sits at EYES_CM in the rest pose.

    Python cannot name a socket, so UCSSocketBuilder (CSCharacter editor module) does the work.
    """
    # A bool UFUNCTION with an out parameter returns the out value on success and None on failure
    # (the reason is in the log).
    message = unreal.CSSocketBuilder.set_mesh_socket(
        mesh, EYE_SOCKET, EYE_BONE, unreal.Vector(*EYES_CM), unreal.Rotator(roll=0.0, pitch=0.0, yaw=90.0))
    if message is None:
        raise RuntimeError("Could not place socket %s on %s (see the UCSSocketBuilder error above)" % (EYE_SOCKET, EYE_BONE))
    log(message)


def build_physics(mesh):
    joints = []
    for bone, swing, twist, hinge, flex, extend in RAGDOLL:
        joint = unreal.CSRagdollJoint(
            bone=bone, swing_limit_deg=swing, twist_limit_deg=twist,
            hinge=hinge, hinge_flex_deg=flex, hinge_extend_deg=extend)
        cone = cone_settings(bone)
        if cone is not None:
            joint.set_editor_property("cone_direction", cone[0])
            joint.set_editor_property("swing1_axis", cone[1])
            joint.set_editor_property("swing2_limit_deg", cone[2])
        joints.append(joint)
    result = unreal.CSRagdollBuilder.build_ragdoll_physics_asset(mesh, PHYS_PATH, joints)
    if not result.success:
        raise RuntimeError("Physics asset build failed: " + result.message)
    for line in result.message.strip().splitlines():
        log("  " + line)
    if result.body_count != len(RAGDOLL) or result.constraint_count != len(RAGDOLL) - 1:
        raise RuntimeError("Physics asset has %d bodies and %d constraints; expected %d and %d"
                           % (result.body_count, result.constraint_count, len(RAGDOLL), len(RAGDOLL) - 1))
    log("PHYS_BaseCharacter: %d capsules, %d constraints" % (result.body_count, result.constraint_count))


def ensure_tuning():
    lib = unreal.EditorAssetLibrary
    path = DEST + "/" + TUNING_NAME
    if lib.does_asset_exist(path):
        log(TUNING_NAME + " exists; left as it is")
        return
    factory = unreal.DataAssetFactory()
    factory.set_editor_property("data_asset_class", unreal.CSMovementTuning)
    asset = unreal.AssetToolsHelpers.get_asset_tools().create_asset(TUNING_NAME, DEST, unreal.CSMovementTuning, factory)
    if asset is None:
        raise RuntimeError("Could not create " + path)
    log("created " + path + " with the starting values")


def main():
    lib = unreal.EditorAssetLibrary
    mesh = import_mesh()
    check_mesh(mesh)
    add_eye_socket(mesh)
    build_physics(mesh)

    if not lib.save_loaded_asset(mesh, False) or not lib.save_asset(PHYS_PATH, False):
        raise RuntimeError("Could not save the mesh or the physics asset")
    if lib.does_asset_exist(AUTO_PHYS_PATH):
        if not lib.delete_asset(AUTO_PHYS_PATH):
            raise RuntimeError("Could not delete the import's own " + AUTO_PHYS_PATH)
        log("deleted the import's own SK_BaseCharacter_PhysicsAsset")

    ensure_tuning()
    if not lib.save_directory(DEST, only_if_is_dirty=True, recursive=True):
        raise RuntimeError("Could not save " + DEST)
    log("done")


main()
