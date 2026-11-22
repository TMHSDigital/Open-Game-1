"""Writes Art/Characters/Base/skeleton_reference.json: the UE5 humanoid bone layout, transcribed.

The committed file holds bone names, parents, side, group, an optional flag and a roll note per
bone. Joint positions are derived Epic data and are NOT committed: the full dump with positions
goes to _scratch/skeldump/skeleton_positions_local.json (gitignored), a local sanity reference
only. Run it in a throwaway scratch project that has the engine's example content enabled, never
in Game/ (Epic sample content stays out of the public repo):

    UnrealEditor-Cmd.exe <scratch>/SkelDump.uproject -run=pythonscript
        -script="<repo>/tools/ue/dump_skeleton.py <skeletal mesh path> [<reference.json> [<positions.json>]]"
        -unattended -nosplash -nullrhi

Local positions file (see Art/Characters/Base/NOTES.md):
- Centimetres in Blender's frame: Blender (x, y, z) = Unreal component space (x, -y, z), which is
  what the export round-trip showed the project FBX preset does.
- Unreal bones have no tail. tail_cm is the head of the next bone in the chain (spine_01 ->
  spine_02, hand -> middle_01, and so on); fingertips continue the previous bone at its length;
  other leaf bones (twist, ik, helpers) continue their parent's direction at half its length; head
  reaches the top of the source mesh's bounds; root points up.

roll_deg (both files) is Blender's bone roll that best lines up the Blender bone's Z axis with the
Unreal bone's local Z axis (mirrored into Blender's frame). It is a note for matching
orientations, not a requirement: the IK Retargeter maps chains by name.
"""

import json
import math
import re
import sys
from pathlib import Path

import unreal

SOURCE = "UE5 humanoid (transcribed)"
LEAF_FRACTION = 0.5  # leaf length as a fraction of the parent's length (twist, ik and helper bones)
CHAIN_END_FRACTION = 1.0  # last bone of a numbered chain (fingertips) is as long as the one before
MIN_LENGTH_CM = 1.0  # Blender drops zero-length bones; nothing shorter than this
ROOT_LENGTH_CM = 10.0

# Tail targets across bone types (same-type chains like spine_01 -> spine_02 are found by name).
# The hand ends at the middle knuckle rather than at the short metacarpal bone.
CHAIN = {
    "pelvis": "spine_01",
    "spine_05": "neck_01",
    "neck_02": "head",
    "clavicle": "upperarm",
    "upperarm": "lowerarm",
    "lowerarm": "hand",
    "hand": "middle_01",
    "thigh": "calf",
    "calf": "foot",
    "foot": "ball",
    "thumb_metacarpal": "thumb_01",
    "index_metacarpal": "index_01",
    "middle_metacarpal": "middle_01",
    "ring_metacarpal": "ring_01",
    "pinky_metacarpal": "pinky_01",
}
SIDE_RE = re.compile(r"^(?P<base>.+?)_(?P<side>[lr])$")
INDEX_RE = re.compile(r"^(?P<stem>.+)_(?P<num>\d\d)$")


# --- small vector helpers (plain tuples, so the maths reads without the unreal types) ---

def sub(a, b):
    return (a[0] - b[0], a[1] - b[1], a[2] - b[2])


def add(a, b):
    return (a[0] + b[0], a[1] + b[1], a[2] + b[2])


def scale(a, s):
    return (a[0] * s, a[1] * s, a[2] * s)


def dot(a, b):
    return a[0] * b[0] + a[1] * b[1] + a[2] * b[2]


def cross(a, b):
    return (a[1] * b[2] - a[2] * b[1], a[2] * b[0] - a[0] * b[2], a[0] * b[1] - a[1] * b[0])


def length(a):
    return math.sqrt(dot(a, a))


def normalized(a):
    n = length(a)
    return scale(a, 1.0 / n) if n > 1e-9 else (0.0, 0.0, 0.0)


def to_blender(v):
    """Unreal component space (cm) -> Blender frame (cm). See the module docstring."""
    return (v.x, -v.y, v.z)


def blender_roll(head, tail, z_axis):
    """Blender's roll for a bone head->tail whose Z axis should point along z_axis.

    Uses Blender's own zero-roll frame (vec_roll_to_mat3): the Z axis it picks for roll 0, then
    the signed angle about the bone axis from that Z to the wanted Z.
    """
    y = normalized(sub(tail, head))
    x, yy, z = y
    theta = 1.0 + yy
    theta_alt = x * x + z * z
    if theta > 1e-5 or theta_alt > 1e-6:
        if theta <= 1e-5:
            theta = theta_alt * 0.5 + theta_alt * theta_alt * 0.125
        z0 = (-x * z / theta, -z, 1.0 - z * z / theta)
    else:  # bone points straight down -Y
        z0 = (0.0, 0.0, 1.0)
    want = normalized(sub(z_axis, scale(y, dot(z_axis, y))))
    if length(want) < 1e-6:
        return 0.0
    return math.degrees(math.atan2(dot(cross(z0, want), y), dot(z0, want)))


# --- skeleton reading ---

def split_side(name):
    m = SIDE_RE.match(name)
    return (m["base"], "_" + m["side"]) if m else (name, "")


def tail_target(name, names):
    """Bone whose head is this bone's tail, or None for a leaf (twist, ik, helper, chain end)."""
    base, suffix = split_side(name)
    if "_twist_" in base:
        return None  # twist bones are siblings along their parent, not a chain
    if base in CHAIN:
        target = CHAIN[base] + suffix
    else:
        im = INDEX_RE.match(base)
        if not im:
            return None
        target = f"{im['stem']}_{int(im['num']) + 1:02d}{suffix}"
    return target if target in names else None


def is_chain_end(name, parent):
    """True for the last bone of a numbered chain, such as index_03_l after index_02_l."""
    base, _ = split_side(name)
    pbase, _ = split_side(parent or "")
    im, pm = INDEX_RE.match(base), INDEX_RE.match(pbase)
    return bool(im and pm and im["stem"] == pm["stem"])


def read_skeleton(mesh):
    """Bones of the mesh's own reference skeleton, parent-first.

    The SkeletonModifier reads the skeletal mesh's reference skeleton. (The USkeleton asset can
    hold extra bones that other meshes sharing it use; those are not part of this layout.)
    """
    modifier = unreal.SkeletonModifier()
    if not modifier.set_skeletal_mesh(mesh):
        raise RuntimeError("SkeletonModifier could not read " + mesh.get_path_name())
    bones = []
    for n in [str(x) for x in modifier.get_all_bone_names()]:
        parent = str(modifier.get_parent_name(n))
        t = modifier.get_bone_transform(n, True)  # True: component space
        z_axis = t.rotation.rotate_vector(unreal.Vector(0.0, 0.0, 1.0))
        bones.append({
            "name": n,
            "parent": None if parent in ("None", "") else parent,
            "head": to_blender(t.translation),
            "z_axis": to_blender(z_axis),
        })
    seen = set()
    for b in bones:
        if b["parent"] is not None and b["parent"] not in seen:
            raise RuntimeError(f"bone order: {b['name']} comes before its parent {b['parent']}")
        seen.add(b["name"])
    return bones


def mesh_top_cm(mesh):
    b = mesh.get_imported_bounds()
    return b.origin.z + b.box_extent.z


def build_reference(mesh):
    bones = read_skeleton(mesh)
    by_name = {b["name"]: b for b in bones}
    top = mesh_top_cm(mesh)

    # Bones come parent-first (Unreal's reference skeleton order), so a parent's tail is known
    # before its leaf children need it.
    for b in bones:
        head = b["head"]
        target = tail_target(b["name"], by_name)
        tail = None
        if b["name"] == "root":
            tail = add(head, (0.0, 0.0, ROOT_LENGTH_CM))
        elif b["name"] == "head":
            tail = (head[0], head[1], top)
        elif target is not None and length(sub(by_name[target]["head"], head)) >= MIN_LENGTH_CM:
            tail = by_name[target]["head"]
        if tail is None:
            p = by_name[b["parent"]]
            direction = normalized(sub(p["tail"], p["head"]))
            frac = CHAIN_END_FRACTION if is_chain_end(b["name"], b["parent"]) else LEAF_FRACTION
            seg = max(length(sub(p["tail"], p["head"])) * frac, MIN_LENGTH_CM)
            tail = add(head, scale(direction, seg))
        b["tail"] = tail
        b["roll"] = blender_roll(head, tail, b["z_axis"])

    return bones, top


def rest_pose(by_name):
    """'A' or 'T' from the left upper arm's angle below horizontal."""
    ua = by_name["upperarm_l"]
    d = normalized(sub(ua["tail"], ua["head"]))
    angle = math.degrees(math.asin(max(-1.0, min(1.0, -d[2]))))
    return ("A" if angle > 20.0 else "T"), angle


def facing(by_name):
    """Forward direction in Blender's frame, from foot to ball."""
    d = normalized(sub(by_name["ball_l"]["head"], by_name["foot_l"]["head"]))
    return "-Y" if d[1] < 0 and abs(d[1]) > abs(d[0]) else "+Y" if abs(d[1]) > abs(d[0]) else "X"


def r2(v):
    return [round(c, 2) + 0.0 for c in v]  # + 0.0 turns -0.0 into 0.0


OPTIONAL_PREFIXES = ("ik_",)
OPTIONAL_NAMES = {"interaction", "center_of_mass"}


def side_of(name):
    _, suffix = split_side(name)
    return {"_l": "left", "_r": "right"}.get(suffix, "center")


def group_of(name):
    base, _ = split_side(name)
    if name.startswith("ik_"):
        return "ik"
    if name in OPTIONAL_NAMES:
        return "helper"
    if "_twist_" in base:
        return "twist"
    if base.startswith(("thumb", "index", "middle", "ring", "pinky")):
        return "finger"
    if base in ("clavicle", "upperarm", "lowerarm", "hand"):
        return "arm"
    if base in ("thigh", "calf", "foot", "ball"):
        return "leg"
    return "spine"  # root, pelvis, spine_*, neck_*, head


def is_optional(name):
    return name.startswith(OPTIONAL_PREFIXES) or name in OPTIONAL_NAMES


def write_lines(path, header, rows):
    lines = ["{"]
    for k, v in header.items():
        lines.append(f"  {json.dumps(k)}: {json.dumps(v)},")
    lines.append('  "bones": [')
    lines.append(",\n".join("    " + json.dumps(r) for r in rows))
    lines.append("  ]")
    lines.append("}")
    Path(path).parent.mkdir(parents=True, exist_ok=True)
    Path(path).write_text("\n".join(lines) + "\n", encoding="utf-8", newline="\n")


def write_reference(path, bones):
    """The committed file: names, parents, side, group, optional flag and roll notes. No positions."""
    header = {
        "source": SOURCE,
        "note": "Names and hierarchy only; joint positions are deliberately not committed.",
        "bone_count": len(bones),
    }
    rows = [{"name": b["name"], "parent": b["parent"], "side": side_of(b["name"]),
             "group": group_of(b["name"]), "optional": is_optional(b["name"]),
             "roll_deg": round(b["roll"], 2)} for b in bones]
    write_lines(path, header, rows)


def write_positions(path, bones, top, pose, arm_angle, forward):
    """The local-only file (under _scratch/, gitignored): the full dump with positions."""
    header = {
        "source": SOURCE,
        "local_only": "Derived from Epic sample content. Never commit this file.",
        "frame": "Blender axes, centimetres: Blender (x, y, z) = Unreal component space (x, -y, z)",
        "forward": forward,
        "rest_pose": pose,
        "upperarm_angle_below_horizontal_deg": round(arm_angle, 1),
        "source_mesh_top_cm": round(top, 2),
        "bone_count": len(bones),
    }
    rows = [{"name": b["name"], "parent": b["parent"], "head_cm": r2(b["head"]),
             "tail_cm": r2(b["tail"]), "roll_deg": round(b["roll"], 2)} for b in bones]
    write_lines(path, header, rows)


def main():
    args = sys.argv[1:]
    if not args:
        raise SystemExit("usage: dump_skeleton.py <skeletal mesh path> [<reference.json> [<positions.json>]]")
    repo = Path(__file__).resolve().parents[2]
    mesh_path = args[0]
    out = args[1] if len(args) > 1 else str(repo / "Art" / "Characters" / "Base" / "skeleton_reference.json")
    local = args[2] if len(args) > 2 else str(repo / "_scratch" / "skeldump" / "skeleton_positions_local.json")
    mesh = unreal.load_asset(mesh_path)
    if not isinstance(mesh, unreal.SkeletalMesh):
        raise RuntimeError(f"not a skeletal mesh: {mesh_path} ({mesh!r})")
    bones, top = build_reference(mesh)
    by_name = {b["name"]: b for b in bones}
    pose, arm_angle = rest_pose(by_name)
    forward = facing(by_name)
    write_reference(out, bones)
    write_positions(local, bones, top, pose, arm_angle, forward)
    unreal.log(f"dump_skeleton: wrote {len(bones)} bones to {out} and positions to {local} "
               f"(rest pose {pose}, upper arm {arm_angle:.1f} deg below horizontal, faces Blender "
               f"{forward}, top {top:.1f} cm)")


main()
