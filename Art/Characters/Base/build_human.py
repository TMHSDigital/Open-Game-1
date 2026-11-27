"""Builds the realistic base character (decision 0022) and exports it as SK_BaseCharacter.

The body is generated with MPFB (the MakeHuman plugin for Blender) from MakeHuman's CC0 core assets:
an athletic adult male (the build chosen on 2026-11-27), 180 cm tall. MPFB's own "game_engine" rig
gives the joint positions and the skin weights; this script then fits the project's 89-bone UE5
layout (skeleton_reference.json) onto it, so everything in the game that names bones keeps working:

- bones that exist in both (pelvis, thighs, arms, hands, fingers, head...) take MPFB's joints;
- the UE5 spine has five bones and the neck two, where the rig has three and one: they are spaced
  along MPFB's spine and neck, and the spine and neck weights are split onto them by height;
- twist bones sit along their limb where build_base_character.py puts them and take a share of
  the limb's weight around them (a clip's twist spreads along the forearm, not at the wrist);
- metacarpals sit between the wrist and the knuckles, and root, the ik and helper bones at the
  origin or on the bone they copy. These carry no weights;
- the arms are posed into the scripted body's rest pose (55 degrees below horizontal, elbows bent
  12 degrees) before the mesh is baked, and every bone takes that body's direction and roll: the
  joint limits, motors and retargets of the game were all built around those frames.

Every vertex keeps at most four influences. The mesh is the MakeHuman body with its helper
geometry removed (eyes, teeth and clothes helpers are separate assets, not part of this step).
Checks run before anything is saved; a failed check writes nothing. Export goes through the
blender-to-unreal-export skill (centimetre scene, decision 0012), reusing build_base_character.py.

Needs MPFB installed in Blender (extensions.blender.org, "mpfb"); run with the user's add-ons:
    pwsh tools/blender/run.ps1 -UserAddons -Script Art/Characters/Base/build_human.py [-- --out-dir D] [--check-only] [--install]

By default it writes to _scratch/human; --install writes SK_BaseCharacter.blend and .fbx here, replacing
the game's character (only once the fit in NOTES.md is done and the game's tests pass on it).
"""

import importlib.util
import json
import sys
from pathlib import Path

import bpy
from mathutils import Matrix, Vector

HERE = Path(__file__).resolve().parent
_spec = importlib.util.spec_from_file_location("base_builder", HERE / "build_base_character.py")
base = importlib.util.module_from_spec(_spec)
_spec.loader.exec_module(base)

from bl_ext.blender_org.mpfb.services.humanservice import HumanService  # noqa: E402
from bl_ext.blender_org.mpfb.services.targetservice import TargetService  # noqa: E402

NAME = base.NAME
HEIGHT = base.HEIGHT
# The athletic build (2026-11-27): MakeHuman macro values, 0 to 1.
MACROS = {"gender": 1.0, "age": 0.5, "muscle": 0.9, "weight": 0.6, "proportions": 1.0}
MAX_INFLUENCES = base.MAX_INFLUENCES
SPINE = ["spine_01", "spine_02", "spine_03", "spine_04", "spine_05"]
NECK = ["neck_01", "neck_02"]
# Where along its limb each twist bone sits, and the old builder's weight share around it: a clip's
# forearm twist spread over the forearm instead of landing at the wrist (a candy-wrapper twist).
TWIST_AT = {"upperarm": (0.20, 0.55), "lowerarm": (0.75, 0.45), "thigh": (0.20, 0.55), "calf": (0.75, 0.45)}
TWIST_SPREAD = 0.3
TWIST_SHARE = 0.5


def generate_human():
    """An MPFB human of the chosen build whose height macro is searched until it stands HEIGHT tall."""
    def make(height_macro):
        for o in list(bpy.data.objects):
            bpy.data.objects.remove(o, do_unlink=True)
        macros = TargetService.get_default_macro_info_dict()
        macros.update(MACROS)
        macros["height"] = height_macro
        # feet_on_ground=False: MPFB fits its rig in MakeHuman's own frame (hips near z = 0) and would
        # not follow a mesh moved up; mesh and joints are moved up together after baking (main).
        human = HumanService.create_human(mask_helpers=True, detailed_helpers=False, feet_on_ground=False,
                                          extra_vertex_groups=True, macro_detail_dict=macros)
        ev = human.evaluated_get(bpy.context.evaluated_depsgraph_get())
        zs = [(ev.matrix_world @ v.co).z for v in ev.data.vertices]
        return human, max(zs) - min(zs)

    lo, hi = 0.0, 1.0
    human, height = None, 0.0
    for _ in range(14):
        mid = (lo + hi) / 2
        human, height = make(mid)
        if abs(height - HEIGHT) < 0.002:
            break
        lo, hi = (mid, hi) if height < HEIGHT else (lo, mid)
    print("human: height macro %.4f gives %.4f m" % (mid, height))
    return human


def bake(human, rig):
    """Shape keys and the helper mask applied: a plain mesh with MPFB's vertex groups."""
    bpy.context.view_layer.objects.active = human
    for o in bpy.context.scene.objects:
        o.select_set(o is human)
    if human.data.shape_keys:
        bpy.ops.object.shape_key_remove(all=True, apply_mix=True)
    # The armature first: the posed rest pose (match_rest_pose) becomes the mesh's shape.
    for mod in sorted(human.modifiers, key=lambda m: m.type != "ARMATURE"):
        if mod.type in ("ARMATURE", "MASK"):
            bpy.ops.object.modifier_apply(modifier=mod.name)
        else:
            human.modifiers.remove(mod)
    world = human.matrix_world.copy()
    human.parent = None
    human.matrix_world = world
    bpy.ops.object.transform_apply(location=True, rotation=True, scale=True)


def rig_joints(rig):
    """Head and tail of every MPFB rig bone, world space. Fails unless the pelvis is at hip height:
    a rig read before its transform is up to date gives joints far below the body."""
    bpy.context.view_layer.update()
    m = rig.matrix_world
    # The posed joints: the rest pose may have been turned to the reference's (match_rest_pose).
    joints = {pb.name: (m @ pb.head, m @ pb.tail) for pb in rig.pose.bones}
    return joints


def match_rest_pose(rig):
    """Poses MPFB's arms into the first scripted body's rest pose: each upper arm and forearm
    pointing as there (arm 55 degrees below horizontal, elbow bent 12 degrees; MPFB's own has the
    arm at 48 and the elbow at 43). Every joint limit, motor and retarget of the game was built
    around that rest pose; the pose is baked into the mesh with MPFB's own weights (bake)."""
    ref, _ = base.all_joints()
    bpy.context.view_layer.objects.active = rig
    bpy.ops.object.mode_set(mode="POSE")
    try:
        for side in ("l", "r"):
            for bone, frm, to in ((f"upperarm_{side}", f"upperarm_{side}", f"lowerarm_{side}"),
                                  (f"lowerarm_{side}", f"lowerarm_{side}", f"hand_{side}")):
                bpy.context.view_layer.update()
                pb = rig.pose.bones[bone]
                want = (ref[to][0] - ref[frm][0]).normalized()
                have = (pb.tail - pb.head).normalized()
                turn = have.rotation_difference(want).to_matrix().to_4x4()
                pivot = Matrix.Translation(pb.head)
                pb.matrix = pivot @ turn @ pivot.inverted() @ pb.matrix
    finally:
        bpy.ops.object.mode_set(mode="OBJECT")
    bpy.context.view_layer.update()


def feet_to_ground(human, joints):
    """Moves the baked mesh and the joints up together so the soles are at z = 0, and checks the
    pelvis then sits at hip height (a rig read in another frame than the mesh would not)."""
    lift = -min(v.co.z for v in human.data.vertices)
    for v in human.data.vertices:
        v.co.z += lift
    up = Vector((0.0, 0.0, lift))
    moved = {n: (h + up, t + up) for n, (h, t) in joints.items()}
    pelvis_z = moved["pelvis"][0].z
    print("feet to ground: lifted %.3f m, pelvis at %.3f m" % (lift, pelvis_z))
    if not 0.45 * HEIGHT < pelvis_z < 0.65 * HEIGHT:
        raise RuntimeError(f"rig pelvis at {pelvis_z:.3f} m, not at hip height: the joints are off")
    return moved


def ue5_joints(ref, src):
    """A head and tail for every bone of the UE5 layout, from the MPFB rig's joints."""
    out = {}
    origin = (Vector((0.0, 0.0, 0.0)), Vector((0.0, 0.1, 0.0)))  # +Y, roll 0: imports unrotated
    spine_a, spine_b = src["spine_01"][0], src["neck_01"][0]
    neck_a, neck_b = src["neck_01"][0], src["head"][0]
    for b in ref:
        n = b["name"]
        if n in src and n not in SPINE and n not in NECK:
            out[n] = (src[n][0].copy(), src[n][1].copy())
        elif n in SPINE:
            i = SPINE.index(n)
            out[n] = (spine_a.lerp(spine_b, i / 5.0), spine_a.lerp(spine_b, (i + 1) / 5.0))
        elif n in NECK:
            i = NECK.index(n)
            out[n] = (neck_a.lerp(neck_b, i / 2.0), neck_a.lerp(neck_b, (i + 1) / 2.0))
        elif n in base.ORIGIN_BONES:
            out[n] = (origin[0].copy(), origin[1].copy())
    for b in ref:
        n = b["name"]
        if n in out:
            continue
        if "_twist_" in n:
            part, rest = n.split("_twist_")
            index, side = int(rest[:2]), rest[-1]
            h, t = out[f"{part}_{side}"]
            at = TWIST_AT[part][index - 1]
            out[n] = (h.lerp(t, at), h.lerp(t, at + 0.2))
        elif "_metacarpal_" in n:
            finger, side = n.split("_metacarpal_")
            hand = out["hand_" + side][0]
            knuckle = out[f"{finger}_01_{side}"][0]
            out[n] = (hand.lerp(knuckle, 0.25), knuckle.copy())
        elif n.startswith("ik_foot_") and n != "ik_foot_root":
            out[n] = tuple(v.copy() for v in out["foot_" + n[-1]])
        elif n.startswith("ik_hand_") and n not in ("ik_hand_root", "ik_hand_gun"):
            out[n] = tuple(v.copy() for v in out["hand_" + n[-1]])
        elif n == "ik_hand_gun":
            out[n] = tuple(v.copy() for v in out["hand_r"])
    missing = [b["name"] for b in ref if b["name"] not in out]
    if missing:
        raise RuntimeError(f"no joint for {missing}")
    return out


def reference_axes(ref):
    """Each bone's local Z axis on the first scripted body (build_base_character.py), whose joints
    and the reference rolls together give the frames the game was built on: the ragdoll's hinge
    axes, the strike's fist and every motor target are set in these frames. The same rolls on
    MPFB's joints (another arm angle, another elbow bend) would turn those frames, twisting a
    driven forearm into a ribbon, so the new bones are rolled to match these axes instead."""
    joints, _ = base.all_joints()
    old = base.build_armature(ref, joints)
    bpy.context.view_layer.objects.active = old
    bpy.ops.object.mode_set(mode="EDIT")
    try:
        axes = {eb.name: (eb.y_axis.copy(), eb.z_axis.copy(), eb.length) for eb in old.data.edit_bones}
    finally:
        bpy.ops.object.mode_set(mode="OBJECT")
    data = old.data
    bpy.data.objects.remove(old, do_unlink=True)
    bpy.data.armatures.remove(data)
    return axes


def match_rolls(arm, axes):
    """Gives every bone the reference body's orientation: its own head and length, the reference's
    direction (Y) and roll (Z). The retargeted clips and the physics treat a bone's whole frame as
    the reference, and MPFB's hands, fingers, feet and head point 13 to 31 degrees off the scripted
    body's; the long bones barely change, the arms being posed to the reference (match_rest_pose)."""
    bpy.context.view_layer.objects.active = arm
    bpy.ops.object.mode_set(mode="EDIT")
    try:
        for eb in arm.data.edit_bones:
            if eb.name in axes:
                y, z, ref_length = axes[eb.name]
                # End bones (hands, fingers, feet, head) take the reference length too: the retarget's
                # IK chains scale by bone length.
                end = eb.name.startswith(("hand_", "thumb_", "index_", "middle_", "ring_", "pinky_", "foot_", "ball_", "head"))
                length = ref_length if end else (eb.tail - eb.head).length
                eb.tail = eb.head + y * length
                eb.align_roll(z)
    finally:
        bpy.ops.object.mode_set(mode="OBJECT")


def _param(p, a, b):
    ab = b - a
    return max(0.0, min(1.0, (p - a).dot(ab) / ab.length_squared))


def _split(t, names):
    """Shares of names for a point at t (0..1) along a chain of len(names) equal segments: a linear
    blend between the centres of the two nearest segments."""
    k = len(names)
    x = t * k - 0.5
    if x <= 0.0:
        return {names[0]: 1.0}
    if x >= k - 1:
        return {names[-1]: 1.0}
    i = int(x)
    f = x - i
    return {names[i]: 1.0 - f, names[i + 1]: f}


def transfer_weights(obj, src, joints, ref):
    """MPFB's weights onto the UE5 bones: same-named bones keep theirs, spine and neck are split by
    height, four influences at most, normalised."""
    names = {g.index: g.name for g in obj.vertex_groups}
    layout = {b["name"] for b in ref}
    spine_src = {"spine_01", "spine_02", "spine_03"}
    weights = []
    for v in obj.data.vertices:
        w = {}
        spine = neck = 0.0
        for g in v.groups:
            n = names.get(g.group)
            if not n or g.weight <= 0.0:
                continue
            if n in spine_src:
                spine += g.weight
            elif n == "neck_01":
                neck += g.weight
            elif n in layout:
                w[n] = w.get(n, 0.0) + g.weight
        if spine > 0.0:
            t = _param(v.co, joints["spine_01"][0], joints["spine_05"][1])
            for n, s in _split(t, SPINE).items():
                w[n] = w.get(n, 0.0) + spine * s
        if neck > 0.0:
            t = _param(v.co, joints["neck_01"][0], joints["neck_02"][1])
            for n, s in _split(t, NECK).items():
                w[n] = w.get(n, 0.0) + neck * s
        for part in TWIST_AT:
            for side in ("l", "r"):
                limb = f"{part}_{side}"
                wl = w.get(limb, 0.0)
                if wl <= 0.0:
                    continue
                h, e = joints[limb]
                t = _param(v.co, h, e)
                shares = [max(0.0, 1.0 - abs(t - f) / TWIST_SPREAD) * TWIST_SHARE for f in TWIST_AT[part]]
                w[limb] = wl * (1.0 - sum(shares))
                for i, sh in enumerate(shares, start=1):
                    if sh > 0.0:
                        tb = f"{part}_twist_{i:02d}_{side}"
                        w[tb] = w.get(tb, 0.0) + wl * sh
        top = sorted(w.items(), key=lambda kv: -kv[1])[:MAX_INFLUENCES]
        total = sum(x for _, x in top)
        weights.append({n: x / total for n, x in top} if total > 0 else {})
    for g in list(obj.vertex_groups):
        obj.vertex_groups.remove(g)
    groups = {b["name"]: obj.vertex_groups.new(name=b["name"]) for b in ref}
    for i, w in enumerate(weights):
        for n, x in w.items():
            groups[n].add([i], x, "REPLACE")
    return sum(1 for w in weights if not w)


def checks(obj, arm, ref, unweighted):
    zs = [v.co.z for v in obj.data.vertices]
    tris = sum(len(p.vertices) - 2 for p in obj.data.polygons)
    max_inf = max(len([g for g in v.groups if g.weight > 0]) for v in obj.data.vertices)
    c = {
        "height_m": round(max(zs) - min(zs), 4),
        "feet_on_ground": abs(min(zs)) < 1e-3,
        "triangles": tris,
        "bones": len(arm.data.bones),
        "bone_names_match": [b.name for b in arm.data.bones] == [b["name"] for b in ref],
        "max_influences": max_inf,
        "unweighted_vertices": unweighted,
        "material_slots": len(obj.material_slots),
        "has_uvs": len(obj.data.uv_layers) > 0,
    }
    ok = (abs(c["height_m"] - HEIGHT) < base.HEIGHT_TOLERANCE_CM / 100.0 and c["feet_on_ground"]
          and c["bones"] == len(ref) and c["bone_names_match"] and max_inf <= MAX_INFLUENCES
          and unweighted == 0 and c["material_slots"] == 1 and c["has_uvs"] and 10000 < tris < 60000)
    return c, ok


def main():
    install = "--install" in sys.argv
    if install:
        sys.argv.remove("--install")
    args = base.parse_args()
    default = HERE if install else base.REPO / "_scratch" / "human"
    out_dir = Path(args.out_dir).resolve() if args.out_dir else default
    scratch = Path(args.out_dir).resolve() if args.out_dir else base.REPO / "_scratch" / "human"
    out_dir.mkdir(parents=True, exist_ok=True)
    scratch.mkdir(parents=True, exist_ok=True)

    ref = base.load_reference()
    human = generate_human()
    rig = HumanService.add_builtin_rig(human, "game_engine")
    match_rest_pose(rig)
    src = rig_joints(rig)
    bake(human, rig)
    src = feet_to_ground(human, src)
    bpy.data.objects.remove(rig, do_unlink=True)
    for coll in (bpy.data.armatures,):
        for block in list(coll):
            if block.users == 0:
                coll.remove(block)

    human.name = NAME
    human.data.name = NAME
    human.data.materials.clear()
    base.add_material(human)
    joints = ue5_joints(ref, src)
    axes = reference_axes(ref)
    arm = base.build_armature(ref, joints)
    match_rolls(arm, axes)
    unweighted = transfer_weights(human, src, joints, ref)
    human.parent = arm
    mod = human.modifiers.new("Armature", "ARMATURE")
    mod.object = arm

    c, ok = checks(human, arm, ref, unweighted)
    (scratch / "checks.json").write_text(json.dumps(c, indent=2), encoding="utf-8")
    print("checks: " + json.dumps(c))
    if not ok:
        print("BUILD CHECK FAILED (no .blend or .fbx written)")
        sys.exit(1)
    if not args.check_only:
        base.to_centimetre_scene(human, arm)
        base.save_and_export(out_dir)
    print("BUILD OK")


if __name__ == "__main__":
    main()
