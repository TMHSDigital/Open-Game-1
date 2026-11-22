"""Export from Blender to Unreal Engine 5.8 with the one standard Combat Simulator FBX preset.

Run headless:
    blender -b Art/Props/Barrel.blend --python-exit-code 1 \
        --python .claude/skills/blender-to-unreal-export/export_unreal_fbx.py -- \
        --out Art/Export/SM_Barrel.fbx --kind static --all

--kind static    meshes (+ UCX_ collision, SOCKET_ empties)
--kind skeletal  armature + skinned meshes, no animation
--kind anim      armature animation only (bakes the active action)
--all            select every exportable object first (otherwise uses the saved selection)
--check-only     validate without writing the file

Exits non-zero when a check fails, so a batch job stops on bad input.
"""

import argparse
import sys

import bpy

# The single Combat Simulator preset. Change it only together with a decision record in docs/decisions/.
UE_FBX_PRESET = dict(
    # Blender is Z-up, like Unreal. -Y forward / Z up keeps Blender's own axes, so no axis
    # conversion is baked in; Unreal's importer handles the right-to-left-handed conversion.
    axis_forward="-Y",
    axis_up="Z",
    # Scene in meters (scale_length 1.0). FBX_SCALE_UNITS writes the unit into the file and
    # Unreal converts meters to centimeters on import. Never bake 100x into the vertices.
    global_scale=1.0,
    apply_unit_scale=True,
    apply_scale_options="FBX_SCALE_UNITS",
    bake_space_transform=False,
    use_mesh_modifiers=True,
    mesh_smooth_type="FACE",  # export smoothing; Unreal warns about missing smoothing groups otherwise
    use_tspace=True,
    use_triangles=False,  # let Unreal triangulate unless a mesh needs a fixed triangulation
    add_leaf_bones=False,  # leaf bones add junk "_end" bones to the Unreal skeleton
    primary_bone_axis="Y",
    secondary_bone_axis="X",
    armature_nodetype="NULL",
    use_armature_deform_only=True,
    path_mode="STRIP",  # textures are imported separately, never embedded
    embed_textures=False,
)

MAX_BONE_INFLUENCES = 4  # crowd-friendly skinning: many characters on screen share skeletons and must stay cheap


def parse_args():
    argv = sys.argv[sys.argv.index("--") + 1:] if "--" in sys.argv else []
    p = argparse.ArgumentParser()
    p.add_argument("--out", required=True)
    p.add_argument("--kind", choices=["static", "skeletal", "anim"], required=True)
    p.add_argument("--all", action="store_true")
    p.add_argument("--check-only", action="store_true")
    return p.parse_args(argv)


def fail(msg):
    print(f"EXPORT CHECK FAILED: {msg}")
    sys.exit(2)


def check_scene_units(scene):
    u = scene.unit_settings
    if u.system not in {"METRIC", "NONE"} or abs(u.scale_length - 1.0) > 1e-6:
        fail(f"scene units must be metric with Unit Scale 1.0 (got {u.system}, {u.scale_length})")


def check_transforms(objs):
    for o in objs:
        if o.type not in {"MESH", "ARMATURE"}:
            continue
        if any(abs(s - 1.0) > 1e-4 for s in o.scale):
            fail(f"'{o.name}' has unapplied scale {tuple(round(s, 4) for s in o.scale)}; apply it (Ctrl+A > Scale)")
        if any(abs(r) > 1e-4 for r in o.rotation_euler) and o.type == "MESH" and o.parent is None:
            fail(f"'{o.name}' has unapplied rotation; apply it (Ctrl+A > Rotation)")


def check_names(objs, kind):
    prefix = {"static": "SM_", "skeletal": "SK_", "anim": None}[kind]
    for o in objs:
        if o.type != "MESH" or o.name.startswith(("UCX_", "UBX_", "USP_", "UCP_")):
            continue
        if prefix and not o.name.startswith(prefix):
            fail(f"mesh '{o.name}' should start with {prefix} (collision meshes use UCX_<MeshName>_00)")


def check_bone_influences(objs):
    for o in objs:
        if o.type != "MESH" or not o.vertex_groups:
            continue
        worst = max((sum(1 for g in v.groups if g.weight > 0.0) for v in o.data.vertices), default=0)
        if worst > MAX_BONE_INFLUENCES:
            fail(f"'{o.name}' has vertices with {worst} bone influences (max {MAX_BONE_INFLUENCES}); use Weights > Limit Total")


def main():
    args = parse_args()
    scene = bpy.context.scene

    if args.all:
        for o in scene.objects:
            o.select_set(o.type in {"MESH", "ARMATURE", "EMPTY"})

    objs = list(bpy.context.selected_objects)
    if not objs:
        fail("nothing selected (use --all or save the .blend with the export objects selected)")

    check_scene_units(scene)
    check_transforms(objs)
    check_names(objs, args.kind)
    if args.kind == "skeletal":
        check_bone_influences(objs)

    kwargs = dict(UE_FBX_PRESET)
    kwargs["filepath"] = bpy.path.abspath(args.out) if args.out.startswith("//") else args.out
    kwargs["use_selection"] = True
    if args.kind == "static":
        kwargs["object_types"] = {"MESH", "EMPTY"}  # EMPTY carries SOCKET_ attach points
        kwargs["bake_anim"] = False
    elif args.kind == "skeletal":
        kwargs["object_types"] = {"MESH", "ARMATURE"}
        kwargs["bake_anim"] = False
    else:
        kwargs["object_types"] = {"ARMATURE"}
        kwargs["bake_anim"] = True
        kwargs["bake_anim_use_all_actions"] = False
        kwargs["bake_anim_use_nla_strips"] = False
        kwargs["bake_anim_force_startend_keying"] = True
        kwargs["bake_anim_simplify_factor"] = 0.0  # keep every key; Unreal compresses on import

    if args.check_only:
        print(f"EXPORT CHECK OK ({len(objs)} objects, kind={args.kind})")
        return

    result = bpy.ops.export_scene.fbx(**kwargs)
    if result != {"FINISHED"}:
        fail(f"FBX exporter returned {result}")
    print(f"EXPORTED {kwargs['filepath']} ({len(objs)} objects, kind={args.kind})")


main()
