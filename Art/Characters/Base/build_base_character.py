"""Builds the Combat Simulator base character from scratch: a faceless, gender-neutral 180 cm
mannequin on the UE5 humanoid bone layout. This script is the source of truth; rerunning it
rebuilds SK_BaseCharacter.blend and SK_BaseCharacter.fbx.

Run headless through the project runner:
    pwsh -NoProfile -File tools/blender/run.ps1 -Script Art/Characters/Base/build_base_character.py
    pwsh -NoProfile -File tools/blender/run.ps1 -Script Art/Characters/Base/build_base_character.py -- --out-dir <dir>
    ... -- --check-only        build and check only, write no .blend or .fbx

Without --out-dir the .blend and .fbx go next to this script and checks.json plus the turnaround
renders (turn_front.png, turn_side.png, turn_back.png) go to _scratch/base_character/. With
--out-dir everything goes to that directory. Exits non-zero when any check fails.

How it is built:
1. Joint positions come from documented human proportions for a 180 cm adult (see JOINTS below),
   never from an Epic asset. Bone names, parents and roll notes come from skeleton_reference.json.
2. The body is a skin modifier over a graph of nodes that follows the joints (extra nodes just
   above and below shoulders, elbows, hips and knees give separate edge loops there), subdivided
   twice and applied. Palms are flat through elliptical skin radii; soles are flattened after.
   Each thumb is a separate piece of the graph; overlapping shells (the thumb, and any branch
   whose skin hull fails) are joined with an exact boolean union so the body is one piece.
3. Rest pose: A-pose, upper arm 55 degrees below horizontal, small elbow and knee bends so IK
   solvers know the bend direction. Faces Blender -Y, left side on +X (export skill convention).
4. Weights: Blender's automatic (bone heat) weights on the main deform bones, then corrections:
   no weight from the opposite leg, the crotch follows the pelvis, a light Laplacian smoothing,
   fingers get distance-based weights per finger, twist bones get a share of their limb, at most
   4 influences per vertex, renormalized, no vertex left unweighted. ik_*, interaction and
   center_of_mass are built (so retargeting and IK tools find them) but carry no weights.
5. One neutral matte material, smart UV unwrap. The scene is built in metres, then converted to
   the export skill's skeletal path (centimetre scene: Unit Scale 0.01, armature scaled 100 and
   applied) before the .blend is saved and exported through the skill's own script (its checks
   run too). checks.json values are measured before the conversion, in metres and centimetres.
"""

import argparse
import json
import math
import subprocess
import sys
from pathlib import Path

import bmesh
import bpy
from mathutils import Vector

HERE = Path(__file__).resolve().parent
REPO = HERE.parents[2]
REFERENCE = HERE / "skeleton_reference.json"
EXPORT_SCRIPT = REPO / ".claude" / "skills" / "blender-to-unreal-export" / "export_unreal_fbx.py"
# Local-only dump of the engine skeleton (gitignored). Used for a printed comparison, never as input.
LOCAL_POSITIONS = REPO / "_scratch" / "skeldump" / "skeleton_positions_local.json"
SCRATCH = REPO / "_scratch" / "base_character"

NAME = "SK_BaseCharacter"
ARMATURE = "Armature"
HEIGHT = 1.80  # metres; Unreal gets 180 cm through the export preset's unit conversion
MAX_INFLUENCES = 4

# Height range and other limits the checks enforce (Global Constraints of the base character plan).
HEIGHT_TOLERANCE_CM = 1.0
TRIS_RANGE = (15000, 25000)


def parse_args():
    argv = sys.argv[sys.argv.index("--") + 1:] if "--" in sys.argv else []
    p = argparse.ArgumentParser()
    p.add_argument("--out-dir", default=None)
    p.add_argument("--check-only", action="store_true")
    return p.parse_args(argv)


# ---------------------------------------------------------------------------------------------
# Joint positions
#
# Segment fractions of stature H from Drillis and Contini (1966), as tabulated in Winter,
# "Biomechanics and Motor Control of Human Movement" (anthropometry chapter):
#   chin 0.870 H, shoulder (acromion) 0.818 H, elbow 0.630 H, wrist 0.485 H, hip (greater
#   trochanter) 0.530 H, knee 0.285 H, ankle 0.039 H; shoulder width 0.259 H, hip width 0.191 H,
#   foot length 0.152 H, foot width 0.055 H, hand length 0.108 H.
# Elbow and wrist heights are for the arm hanging, so upper arm = (0.818 - 0.630) H and forearm =
# (0.630 - 0.485) H; thigh = (0.530 - 0.285) H and shank = (0.285 - 0.039) H.
# The glenohumeral joint centre sits about 4 cm inside and below the acromion; hip joint centres
# sit at about half the bitrochanteric half-width from the midline.
# ---------------------------------------------------------------------------------------------

H = HEIGHT
CHIN_Z = 0.870 * H
SHOULDER_Z = 0.818 * H - 0.035
HIP_Z = 0.530 * H
KNEE_Z = 0.285 * H
ANKLE_Z = 0.039 * H
SHOULDER_X = 0.259 * H / 2 - 0.040
HIP_X = 0.191 * H / 4
UPPER_ARM = (0.818 - 0.630) * H - 0.035
FOREARM = (0.630 - 0.485) * H
HAND = 0.108 * H
FOOT = 0.152 * H

A_POSE_DEG = 55.0  # upper arm below horizontal, the UE5 humanoid rest pose (NOTES.md)
ELBOW_BEND_DEG = 12.0
FORWARD = Vector((0.0, -1.0, 0.0))  # the character faces Blender -Y


def _rotate_toward(v, toward, deg):
    """Rotate unit vector v by deg toward the direction 'toward' (in the plane of both)."""
    t = (toward - v * toward.dot(v)).normalized()
    r = math.radians(deg)
    return (v * math.cos(r) + t * math.sin(r)).normalized()


def left_joints():
    """Head and tail of every left-side and centre bone, in metres. The right side is mirrored."""
    j = {}
    # Spine: pelvis just above the hip joints, neck base at about acromion height, head joint
    # just above chin height. The column sits slightly behind the body centre (+Y).
    spine_z = [HIP_Z + 0.015, HIP_Z + 0.055, 1.105, 1.190, 1.275, 1.365, 1.480, 1.530, CHIN_Z + 0.02]
    spine_y = [0.0, 0.012, 0.020, 0.022, 0.020, 0.018, 0.022, 0.018, 0.008]
    names = ["pelvis", "spine_01", "spine_02", "spine_03", "spine_04", "spine_05",
             "neck_01", "neck_02", "head"]
    pts = [Vector((0.0, y, z)) for y, z in zip(spine_y, spine_z)]
    for i, n in enumerate(names):
        tail = pts[i + 1] if i + 1 < len(pts) else Vector((0.0, 0.0, H - 0.04))
        j[n] = (pts[i], tail)

    # Arm (left = +X). Upper arm 55 degrees below horizontal with a slight forward tilt.
    s = Vector((SHOULDER_X, 0.012, SHOULDER_Z))
    a = math.radians(A_POSE_DEG)
    du = Vector((math.cos(a), 0.0, -math.sin(a)))
    du = _rotate_toward(du, FORWARD, 4.0)
    df = _rotate_toward(du, FORWARD, ELBOW_BEND_DEG)
    e = s + du * UPPER_ARM
    w = e + df * FOREARM
    j["clavicle_l"] = (Vector((0.035, -0.020, SHOULDER_Z - 0.02)), s.copy())
    j["upperarm_l"] = (s, e)
    j["lowerarm_l"] = (e, w)

    # Hand frame: along the forearm, fingers fanned toward the front, palm facing the body.
    ha = df
    hf = (FORWARD - ha * FORWARD.dot(ha)).normalized()
    hn = ha.cross(hf).normalized()  # palm normal, toward the body for the left hand
    j["hand_l"] = (w, w + ha * 0.075)
    # Fingers: knuckle offset across the palm, knuckle distance from the wrist, fan, phalanx lengths.
    # Phalanx lengths follow the usual adult ratios (proximal about 1.6 to 2 times distal), scaled
    # so the middle finger tip lands at hand length (0.108 H) from the wrist.
    fingers = {
        "index": (0.032, 0.088, 0.12, (0.041, 0.025, 0.020)),
        "middle": (0.009, 0.092, 0.02, (0.045, 0.028, 0.022)),
        "ring": (-0.011, 0.088, -0.06, (0.042, 0.027, 0.021)),
        "pinky": (-0.030, 0.080, -0.18, (0.033, 0.020, 0.018)),
    }
    j["_fingertips_l"] = {}
    for f, (off, knuckle, fan, lengths) in fingers.items():
        base = w + ha * 0.014 + hf * off * 0.55
        k = w + ha * knuckle + hf * off
        j[f"{f}_metacarpal_l"] = (base, k)
        d = (ha + hf * fan).normalized()
        p = k
        for i, (ln, curl) in enumerate(zip(lengths, (6.0, 14.0, 22.0)), start=1):
            di = _rotate_toward(d, hn, curl)
            q = p + di * ln
            j[f"{f}_{i:02d}_l"] = (p, q)
            p = q
        j["_fingertips_l"][f] = p
    # Thumb: thumb_01 is the metacarpal, from the base of the palm toward the front and palm side.
    t = (ha * 0.70 + hf * 0.60 + hn * 0.38).normalized()
    p = w + ha * 0.022 + hf * 0.018 + hn * 0.008
    for i, (ln, bend) in enumerate(zip((0.046, 0.033, 0.027), (0.0, 18.0, 30.0)), start=1):
        di = _rotate_toward(t, ha, bend)
        q = p + di * ln
        j[f"thumb_{i:02d}_l"] = (p, q)
        p = q
    j["_fingertips_l"]["thumb"] = p

    # Leg: knee slightly forward so the bend direction is defined, feet a little apart.
    hip = Vector((HIP_X, 0.0, HIP_Z))
    knee = Vector((HIP_X + 0.016, -0.012, KNEE_Z))
    ankle = Vector((HIP_X + 0.030, 0.022, ANKLE_Z))
    heel_y = ankle.y + 0.058
    ball = Vector((HIP_X + 0.040, heel_y - 0.73 * FOOT, 0.022))
    toe = Vector((HIP_X + 0.046, heel_y - FOOT + 0.012, 0.020))
    j["thigh_l"] = (hip, knee)
    j["calf_l"] = (knee, ankle)
    j["foot_l"] = (ankle, ball)
    j["ball_l"] = (ball, toe)
    j["_heel_y"] = heel_y

    # Twist bones lie along their limb: (fraction of the limb where the head sits).
    twist = {"upperarm": (0.20, 0.55), "lowerarm": (0.75, 0.45),
             "thigh": (0.20, 0.55), "calf": (0.75, 0.45)}
    for limb, fracs in twist.items():
        h0, t0 = j[f"{limb}_l"]
        for i, fr in enumerate(fracs, start=1):
            j[f"{limb}_twist_{i:02d}_l"] = (h0.lerp(t0, fr), h0.lerp(t0, fr + 0.2))
    return j


# Root-level helpers sit at the origin (animation moves them). They point along +Y with roll 0,
# so their Blender bone matrix is the identity and they import into Unreal with no rotation.
ORIGIN_BONES = ("root", "ik_foot_root", "ik_hand_root", "interaction", "center_of_mass")


def origin_bone():
    return Vector((0.0, 0.0, 0.0)), Vector((0.0, 0.10, 0.0))


def mirror(v):
    return Vector((-v.x, v.y, v.z))


def all_joints():
    j = left_joints()
    out = {}
    for n, v in j.items():
        if n.startswith("_"):
            continue
        out[n] = v
        if n.endswith("_l"):
            out[n[:-2] + "_r"] = (mirror(v[0]), mirror(v[1]))
    for n in ORIGIN_BONES:
        out[n] = origin_bone()
    for side in ("l", "r"):
        out[f"ik_foot_{side}"] = tuple(v.copy() for v in out[f"foot_{side}"])
        out[f"ik_hand_{side}"] = tuple(v.copy() for v in out[f"hand_{side}"])
    out["ik_hand_gun"] = tuple(v.copy() for v in out["hand_r"])
    return out, j


# ---------------------------------------------------------------------------------------------
# Skin graph: nodes (position, radius across, radius front-back) and edges
# ---------------------------------------------------------------------------------------------

class Graph:
    def __init__(self):
        self.pos, self.rad, self.edges, self.index = [], [], [], {}
        self.roots = []  # one per separate piece of the graph (see merge_islands)

    def node(self, name, p, r):
        rx, ry = (r, r) if isinstance(r, (int, float)) else r
        self.index[name] = len(self.pos)
        self.pos.append(Vector(p))
        self.rad.append((rx, ry))
        return name

    def chain(self, *names):
        for a, b in zip(names, names[1:]):
            self.edges.append((self.index[a], self.index[b]))


def build_graph(j):
    g = Graph()
    # Torso and head, bottom to top. Radii are skin radii (the subdivided surface ends up a little
    # inside them). Head: an egg, slightly deeper than wide, no features.
    torso = [
        ("pelvis_c", (0, 0.026, HIP_Z - 0.030), (0.158, 0.126)),
        ("hip_band", (0, 0.018, HIP_Z + 0.030), (0.152, 0.118)),
        ("abdomen", (0, 0.008, 1.040), (0.147, 0.113)),
        ("waist", (0, 0.004, 1.105), (0.146, 0.111)),
        ("ribs_low", (0, -0.002, 1.165), (0.152, 0.117)),
        ("ribs", (0, -0.008, 1.225), (0.162, 0.127)),
        ("chest", (0, -0.012, 1.290), (0.176, 0.138)),
        ("chest_up", (0, -0.004, 1.360), (0.188, 0.134)),
        ("shoulder_line", (0, 0.010, 1.420), (0.168, 0.110)),
        ("neck_base", (0, 0.020, 1.472), (0.098, 0.080)),
        ("neck_lo", (0, 0.022, 1.515), (0.072, 0.074)),
        ("neck_hi", (0, 0.016, 1.560), (0.070, 0.072)),
        ("jaw", (0, -0.004, 1.605), (0.080, 0.098)),
        ("head_mid", (0, -0.008, 1.670), (0.098, 0.120)),
        ("head_up", (0, -0.004, 1.735), (0.086, 0.106)),
        ("crown", (0, 0.000, 1.775), (0.046, 0.058)),
    ]
    for n, p, r in torso:
        g.node(n, p, r)
    g.chain(*[n for n, _, _ in torso])

    for side, sx in (("l", 1.0), ("r", -1.0)):
        def P(v):
            return Vector((v.x * sx, v.y, v.z))

        def N(name, p, r, k=1.0):
            r = (r * k, r * k) if isinstance(r, (int, float)) else (r[0] * k, r[1] * k)
            return g.node(f"{name}_{side}", P(p), r)

        # Arm
        s, e = j["upperarm_l"]
        w = j["lowerarm_l"][1]
        du = (e - s).normalized()
        df = (w - e).normalized()
        arm = [
            N("trap", Vector((0.110, 0.018, SHOULDER_Z + 0.022)), 0.068),
            N("deltoid", s + Vector((0.012, 0.0, 0.000)), 0.074),
            N("arm_a", s + du * 0.060, 0.068),
            N("arm_b", s + du * 0.120, 0.062),
            N("arm_c", s + du * 0.185, 0.058),
            N("elbow_up", e - du * 0.045, 0.052),
            N("elbow", e, 0.049),
            N("elbow_dn", e + df * 0.040, 0.052),
            N("forearm", e + df * 0.090, 0.055),
            N("forearm_mid", e + df * 0.140, 0.050),
            N("forearm_lo", e + df * 0.195, 0.042),
            N("wrist", w, (0.036, 0.028)),
            N("palm", w + df * 0.066, (0.050, 0.019)),
        ]
        g.edges.append((g.index["shoulder_line"], g.index[arm[0]]))
        g.chain(*arm)
        # Knuckle row and fingers
        knuckles = [N(f"k_{f}", j[f"{f}_metacarpal_l"][1], 0.0125)
                    for f in ("index", "middle", "ring", "pinky")]
        for k in knuckles:
            g.chain(arm[-1], k)
        for f, k in zip(("index", "middle", "ring", "pinky"), knuckles):
            # A ring at every finger joint and one mid-phalanx, so each of the three segments
            # bends on its own loops; the radius tapers toward the tip.
            r0 = 0.0106 if f != "pinky" else 0.0094
            b1, b2, b3 = (j[f"{f}_{i:02d}_l"] for i in (1, 2, 3))
            n0 = N(f"{f}_1m", b1[0].lerp(b1[1], 0.5), r0)
            n1 = N(f"{f}_2", b2[0], r0 * 0.94)
            n1m = N(f"{f}_2m", b2[0].lerp(b2[1], 0.5), r0 * 0.90)
            n2 = N(f"{f}_3", b3[0], r0 * 0.87)
            tip = j["_fingertips_l"][f]
            n3 = N(f"{f}_tip", tip - (tip - b3[0]).normalized() * 0.004, r0 * 0.80)
            g.chain(k, n0, n1, n1m, n2, n3)
        th1, th2, th3 = (j[f"thumb_{i:02d}_l"] for i in (1, 2, 3))
        # The thumb is its own piece of the graph, starting inside the ball of the thumb in the
        # palm; the union in merge_islands joins it. A fifth branch on the palm node makes the
        # skin hull fail, and a branch off the wrist roots it on the forearm.
        t0 = N("thumb_0", th1[0], 0.0200)
        g.roots.append(t0)
        t1 = N("thumb_1", th1[0].lerp(th1[1], 0.55), 0.0175)
        t2 = N("thumb_2", th2[0], 0.0140)
        t2m = N("thumb_2m", th2[0].lerp(th2[1], 0.5), 0.0132)
        t3 = N("thumb_3", th3[0], 0.0124)
        tip = j["_fingertips_l"]["thumb"]
        t4 = N("thumb_tip", tip - (tip - th3[0]).normalized() * 0.004, 0.0108)
        g.chain(t0, t1, t2, t2m, t3, t4)

        # Leg
        hip, knee = j["thigh_l"]
        ankle, ball = j["foot_l"]
        toe = j["ball_l"][1]
        heel_y = j["_heel_y"]
        leg = [
            N("hips", Vector((HIP_X + 0.006, 0.016, HIP_Z - 0.085)), 0.096),
            N("thigh_up", hip.lerp(knee, 0.25), 0.098),
            N("thigh_mid", hip.lerp(knee, 0.45), 0.092),
            N("thigh_lo", hip.lerp(knee, 0.68), 0.082),
            N("knee_up", knee + Vector((0, 0, 0.060)), 0.072),
            N("knee", knee, 0.067),
            N("knee_dn", knee - Vector((0, -0.004, 0.050)), 0.064),
            N("calf", knee.lerp(ankle, 0.28) + Vector((0, 0.016, 0)), 0.074),
            N("calf_lo", knee.lerp(ankle, 0.46) + Vector((0, 0.010, 0)), 0.066),
            N("shin", knee.lerp(ankle, 0.66), 0.054),
            N("ankle_up", knee.lerp(ankle, 0.86), 0.046),
            N("ankle", ankle, 0.046),
            N("instep", Vector((ankle.x + 0.004, ankle.y - 0.060, 0.052)), (0.054, 0.044)),
            N("ball", Vector((ball.x, ball.y, 0.034)), (0.060, 0.033)),
            N("toe", Vector((toe.x, toe.y + 0.004, 0.030)), (0.048, 0.027)),
        ]
        g.edges.append((g.index["pelvis_c"], g.index[leg[0]]))
        g.chain(*leg)
        heel = N("heel", Vector((ankle.x, heel_y - 0.016, 0.042)), 0.044)
        g.chain(leg[11], heel)
    return g


def skin_mesh(g):
    """Skin modifier over the graph, subdivided twice; returns a new applied mesh datablock."""
    me = bpy.data.meshes.new("skin_graph")
    me.from_pydata([tuple(p) for p in g.pos], g.edges, [])
    me.update()
    obj = bpy.data.objects.new("skin_graph", me)
    bpy.context.scene.collection.objects.link(obj)
    skin = obj.modifiers.new("Skin", "SKIN")
    skin.branch_smoothing = 0.6
    skin.use_smooth_shade = True
    data = me.skin_vertices[0].data
    for i, (rx, ry) in enumerate(g.rad):
        data[i].radius = (rx, ry)
    for name in ["pelvis_c"] + g.roots:
        data[g.index[name]].use_root = True
    sub = obj.modifiers.new("Subsurf", "SUBSURF")
    sub.levels = 2
    sub.render_levels = 2
    deps = bpy.context.evaluated_depsgraph_get()
    evaluated = obj.evaluated_get(deps)
    result = bpy.data.meshes.new_from_object(evaluated)
    bpy.data.objects.remove(obj, do_unlink=True)
    bpy.data.meshes.remove(me)
    return merge_islands(result)


def _islands(bm):
    """Vertex index sets of the connected pieces of a bmesh."""
    seen, out = set(), []
    for v in bm.verts:
        if v.index in seen:
            continue
        comp, stack = {v.index}, [v]
        seen.add(v.index)
        while stack:
            for e in stack.pop().link_edges:
                for w in e.verts:
                    if w.index not in seen:
                        seen.add(w.index)
                        comp.add(w.index)
                        stack.append(w)
        out.append(comp)
    return out


def _mesh_from_subset(bm, keep, name):
    part = bm.copy()
    try:
        part.verts.ensure_lookup_table()
        bmesh.ops.delete(part, geom=[v for v in part.verts if v.index not in keep], context="VERTS")
        me = bpy.data.meshes.new(name)
        part.to_mesh(me)
    finally:
        part.free()
    return me


def merge_islands(me):
    """Make the body one closed piece.

    At busy branch nodes (palm, wrist) the skin modifier's hull can fail and leave a finger or the
    hand as a separate shell that only overlaps its neighbour, and a stray loose vertex or two.
    Loose vertices are removed and the overlapping shells are unioned with the exact boolean.
    """
    bm = bmesh.new()
    try:
        bm.from_mesh(me)
        bmesh.ops.delete(bm, geom=[v for v in bm.verts if not v.link_faces], context="VERTS")
        bm.verts.index_update()
        pieces = sorted(_islands(bm), key=len, reverse=True)
        if len(pieces) == 1:
            bm.to_mesh(me)
            return me
        main = _mesh_from_subset(bm, pieces[0], "merge_main")
        rest = _mesh_from_subset(bm, set().union(*pieces[1:]), "merge_rest")
    finally:
        bm.free()
    a = bpy.data.objects.new("merge_main", main)
    b = bpy.data.objects.new("merge_rest", rest)
    for o in (a, b):
        bpy.context.scene.collection.objects.link(o)
    mod = a.modifiers.new("Union", "BOOLEAN")
    mod.operation, mod.operand_type, mod.object = "UNION", "OBJECT", b
    mod.solver, mod.use_self = "EXACT", True
    deps = bpy.context.evaluated_depsgraph_get()
    result = bpy.data.meshes.new_from_object(a.evaluated_get(deps))
    for o, m in ((a, main), (b, rest)):
        bpy.data.objects.remove(o, do_unlink=True)
        bpy.data.meshes.remove(m)
    bpy.data.meshes.remove(me)
    result.materials.clear()  # the boolean adds an empty slot for its operand
    return result


# ---------------------------------------------------------------------------------------------
# Shaping after the skin: flat soles, exact height, origin between the feet
# ---------------------------------------------------------------------------------------------

def _seg_t(p, a, b):
    ab = b - a
    return max(0.0, min(1.0, (p - a).dot(ab) / ab.length_squared))


def shape(me):
    co = [0.0] * (len(me.vertices) * 3)
    me.vertices.foreach_get("co", co)
    pts = [Vector(co[i:i + 3]) for i in range(0, len(co), 3)]

    # Soles: squash what is below sole height so the feet stand flat.
    sole = 0.022
    for p in pts:
        if p.z < sole and abs(p.x) > 0.04:
            p.z = sole + (p.z - sole) * 0.25

    zmin = min(p.z for p in pts)
    zmax = max(p.z for p in pts)
    scale = H / (zmax - zmin)
    shift = Vector((0.0, 0.0, -zmin))
    pts = [(p + shift) * scale for p in pts]
    me.vertices.foreach_set("co", [c for p in pts for c in p])
    me.update()
    return shift, scale


# ---------------------------------------------------------------------------------------------
# Armature
# ---------------------------------------------------------------------------------------------

def load_reference():
    data = json.loads(REFERENCE.read_text(encoding="utf-8"))
    return data["bones"]


def build_armature(ref, joints):
    arm_data = bpy.data.armatures.new(ARMATURE)
    arm = bpy.data.objects.new(ARMATURE, arm_data)
    bpy.context.scene.collection.objects.link(arm)
    bpy.context.view_layer.objects.active = arm
    arm.select_set(True)
    bpy.ops.object.mode_set(mode="EDIT")
    try:
        for b in ref:  # parents precede children in the reference
            eb = arm_data.edit_bones.new(b["name"])
            head, tail = joints[b["name"]]
            eb.head, eb.tail = head, tail
            eb.roll = math.radians(b["roll_deg"])
            if b["parent"]:
                eb.parent = arm_data.edit_bones[b["parent"]]
                eb.use_connect = False
    finally:
        bpy.ops.object.mode_set(mode="OBJECT")
    return arm


# ---------------------------------------------------------------------------------------------
# Weights
# ---------------------------------------------------------------------------------------------

FINGER_BONES = [f"{f}_{i:02d}" for f in ("thumb", "index", "middle", "ring", "pinky") for i in (1, 2, 3)]
FINGER_BONES += [f"{f}_metacarpal" for f in ("index", "middle", "ring", "pinky")]
TWIST = {"upperarm": ("upperarm_twist_01", "upperarm_twist_02"),
         "lowerarm": ("lowerarm_twist_01", "lowerarm_twist_02"),
         "thigh": ("thigh_twist_01", "thigh_twist_02"),
         "calf": ("calf_twist_01", "calf_twist_02")}


def heat_weights(obj, arm, ref):
    """Automatic weights on the main deform bones only (no twist, finger, ik or helper bones)."""
    no_heat = {b["name"] for b in ref if b["optional"] or b["name"] == "root"
               or "_twist_" in b["name"]}
    no_heat |= {f"{n}_{s}" for n in FINGER_BONES for s in ("l", "r")}
    for bone in arm.data.bones:
        bone.use_deform = bone.name not in no_heat
    for o in bpy.context.scene.objects:
        o.select_set(o in (obj, arm))
    bpy.context.view_layer.objects.active = arm
    with bpy.context.temp_override(active_object=arm, object=arm,
                                   selected_objects=[obj, arm], selected_editable_objects=[obj, arm]):
        bpy.ops.object.parent_set(type="ARMATURE_AUTO")
    for bone in arm.data.bones:
        bone.use_deform = True  # every bone exports (preset exports deform bones only)


def read_weights(obj):
    names = {vg.index: vg.name for vg in obj.vertex_groups}
    return [{names[g.group]: g.weight for g in v.groups if g.weight > 0.0} for v in obj.data.vertices]


def write_weights(obj, weights):
    for vg in list(obj.vertex_groups):
        obj.vertex_groups.remove(vg)
    groups = {}
    for vi, wd in enumerate(weights):
        for name, wt in wd.items():
            vg = groups.get(name)
            if vg is None:
                vg = groups[name] = obj.vertex_groups.new(name=name)
            vg.add([vi], wt, "REPLACE")


def _dist_to_bone(p, head, tail):
    return (p - head.lerp(tail, _seg_t(p, head, tail))).length


def smooth_weights(obj, weights, iterations, factor):
    """Laplacian smoothing of all weights along mesh edges (in place)."""
    me = obj.data
    ev = [0] * (len(me.edges) * 2)
    me.edges.foreach_get("vertices", ev)
    nbrs = [[] for _ in me.vertices]
    for a, b in zip(ev[0::2], ev[1::2]):
        nbrs[a].append(b)
        nbrs[b].append(a)
    for _ in range(iterations):
        new = []
        for wd, nb in zip(weights, nbrs):
            avg = {}
            for n in nb:
                for b, wt in weights[n].items():
                    avg[b] = avg.get(b, 0.0) + wt / len(nb)
            keys = set(wd) | set(avg)
            new.append({b: (1.0 - factor) * wd.get(b, 0.0) + factor * avg.get(b, 0.0) for b in keys})
        weights[:] = new


def correct_weights(obj, joints):
    pts = [v.co.copy() for v in obj.data.vertices]
    weights = read_weights(obj)

    # Legs: no weight from the other side's leg, and the crotch follows the pelvis. Bone heat lets
    # both thighs reach the crotch, which pinches it when one leg spreads.
    for p, wd in zip(pts, weights):
        if p.z > HIP_Z + 0.05:
            continue
        other = "_r" if p.x > 0.0 else "_l"
        for b in [b for b in wd if b.endswith(other) and b.startswith(("thigh", "calf"))]:
            if abs(p.x) > 0.012:
                del wd[b]
        if p.z > HIP_Z - 0.16 and abs(p.x) < 0.05:
            f = 1.0 - abs(p.x) / 0.05
            tot = sum(wd.values()) or 1.0
            for b in wd:
                wd[b] *= (1.0 - f) / tot
            wd["pelvis"] = wd.get("pelvis", 0.0) + f
    smooth_weights(obj, weights, iterations=3, factor=0.5)

    for side in ("l", "r"):
        # Fingers: the heat solver only knows the hand bone; split the hand's weight among the
        # nearest finger bones for vertices past the base of the palm.
        hand = f"hand_{side}"
        w0, w1 = joints[hand]
        a = (w1 - w0).normalized()
        bones = [f"{n}_{side}" for n in FINGER_BONES] + [hand]
        for p, wd in zip(pts, weights):
            wh = wd.get(hand, 0.0)
            if wh <= 0.0:
                continue
            t = (p - w0).dot(a)
            # Palm and finger region; near the wrist the hand bone keeps everything.
            if t < 0.015:
                continue
            dists = sorted((_dist_to_bone(p, *joints[b]), b) for b in bones)
            near = dists[:3]
            raw = [(b, 1.0 / max(d, 0.002) ** 4) for d, b in near]
            tot = sum(r for _, r in raw)
            # Blend in over the first few centimetres of the palm so the wrist stays smooth.
            k = min(1.0, (t - 0.015) / 0.03)
            wd[hand] = wh * (1.0 - k)
            for b, r in raw:
                wd[b] = wd.get(b, 0.0) + wh * k * r / tot

        # Twist bones: share of their limb along its length (tent around each twist head).
        for limb, (t1, t2) in TWIST.items():
            prim = f"{limb}_{side}"
            h, e = joints[prim]
            heads = [joints[f"{t1}_{side}"][0], joints[f"{t2}_{side}"][0]]
            fr = [_seg_t(x, h, e) for x in heads]
            for p, wd in zip(pts, weights):
                wp = wd.get(prim, 0.0)
                if wp <= 0.0:
                    continue
                t = _seg_t(p, h, e)
                shares = [max(0.0, 1.0 - abs(t - f) / 0.3) * 0.5 for f in fr]
                wd[prim] = wp * (1.0 - sum(shares))
                for tb, sh in zip((t1, t2), shares):
                    if sh > 0.0:
                        wd[f"{tb}_{side}"] = wd.get(f"{tb}_{side}", 0.0) + wp * sh

    # Unweighted vertices: give them to the nearest weighted bone.
    deform = [n for n in joints if not n.startswith(("ik_", "interaction", "center_of_mass", "root"))]
    for p, wd in zip(pts, weights):
        if sum(wd.values()) <= 1e-6:
            d, b = min((_dist_to_bone(p, *joints[n]), n) for n in deform)
            wd.clear()
            wd[b] = 1.0

    # At most 4 influences, tiny weights dropped, renormalized.
    for wd in weights:
        top = sorted(wd.items(), key=lambda kv: kv[1], reverse=True)[:MAX_INFLUENCES]
        top = [(n, wt) for n, wt in top if wt >= 0.01] or top[:1]
        tot = sum(wt for _, wt in top)
        wd.clear()
        wd.update({n: wt / tot for n, wt in top})
    write_weights(obj, weights)


# ---------------------------------------------------------------------------------------------
# Material and UVs
# ---------------------------------------------------------------------------------------------

def add_material(obj):
    mat = bpy.data.materials.new("M_BaseCharacter")
    mat.use_nodes = True
    bsdf = mat.node_tree.nodes.get("Principled BSDF")
    bsdf.inputs["Base Color"].default_value = (0.55, 0.55, 0.55, 1.0)
    bsdf.inputs["Roughness"].default_value = 0.85
    mat.diffuse_color = (0.55, 0.55, 0.55, 1.0)
    obj.data.materials.append(mat)


def unwrap(obj):
    obj.data.uv_layers.new(name="UVMap")
    for o in bpy.context.scene.objects:
        o.select_set(o == obj)
    bpy.context.view_layer.objects.active = obj
    with bpy.context.temp_override(active_object=obj, object=obj, edit_object=obj,
                                   selected_objects=[obj], selected_editable_objects=[obj]):
        bpy.ops.object.mode_set(mode="EDIT")
        bpy.ops.mesh.select_all(action="SELECT")
        bpy.ops.uv.smart_project(angle_limit=math.radians(66.0), island_margin=0.004,
                                 scale_to_bounds=False)
        bpy.ops.object.mode_set(mode="OBJECT")
    uv = obj.data.uv_layers["UVMap"]
    obj.data.uv_layers.active = uv
    uv.active_render = True


# ---------------------------------------------------------------------------------------------
# Checks
# ---------------------------------------------------------------------------------------------

def _count_islands(bm):
    """Connected pieces of the mesh; the body must be one piece (no floating fingers)."""
    seen, islands = set(), 0
    for v in bm.verts:
        if v.index in seen:
            continue
        islands += 1
        stack = [v]
        seen.add(v.index)
        while stack:
            for e in stack.pop().link_edges:
                for w in e.verts:
                    if w.index not in seen:
                        seen.add(w.index)
                        stack.append(w)
    return islands


def run_checks(obj, arm, ref):
    me = obj.data
    zs = [v.co.z for v in me.vertices]
    height_cm = round((max(zs) - min(zs)) * 100.0, 3)
    tris = sum(len(p.vertices) - 2 for p in me.polygons)
    bone_names = {b.name for b in arm.data.bones}
    group_names = {vg.index: vg.name for vg in obj.vertex_groups}
    max_inf, unweighted = 0, 0
    for v in me.vertices:
        gs = [g for g in v.groups if g.weight > 0.0 and group_names[g.group] in bone_names]
        max_inf = max(max_inf, len(gs))
        if sum(g.weight for g in gs) <= 1e-6:
            unweighted += 1
    expected = [b["name"] for b in ref]
    bm = bmesh.new()
    try:
        bm.from_mesh(me)
        non_manifold = sum(1 for e in bm.edges if not e.is_manifold)
        islands = _count_islands(bm)
        volume = bm.calc_volume(signed=True)
    finally:
        bm.free()
    checks = {
        "height_cm": height_cm,
        "tris": tris,
        "max_influences": max_inf,
        "unweighted_verts": unweighted,
        "bone_names_match": len(arm.data.bones) == len(expected) == 89 and bone_names == set(expected),
        "material_slots": len(obj.material_slots),
        "has_uvs": len(me.uv_layers) > 0,
        # extra, from the spec's check list
        "bone_count": len(arm.data.bones),
        "non_manifold_edges": non_manifold,
        "mesh_islands": islands,
        "normals_outward": volume > 0.0,
        "min_z_cm": round(min(zs) * 100.0, 3),
    }
    ok = (abs(height_cm - HEIGHT * 100.0) <= HEIGHT_TOLERANCE_CM
          and TRIS_RANGE[0] <= tris <= TRIS_RANGE[1]
          and max_inf <= MAX_INFLUENCES and unweighted == 0 and checks["bone_names_match"]
          and checks["material_slots"] == 1 and checks["has_uvs"]
          and non_manifold == 0 and islands == 1 and checks["normals_outward"] and abs(min(zs)) < 1e-4)
    return checks, ok


def compare_local(joints):
    """Print how far the joints are from the local engine dump, if present. Never used as input."""
    if not LOCAL_POSITIONS.exists():
        print("sanity: no local skeleton dump, comparison skipped")
        return
    try:
        dump = json.loads(LOCAL_POSITIONS.read_text(encoding="utf-8"))
        ref = {b["name"]: Vector(b["head_cm"]) / 100.0 for b in dump["bones"]}
    except (KeyError, ValueError, TypeError) as exc:
        print(f"sanity: local dump unreadable ({exc}), comparison skipped")
        return
    rows = sorted(((joints[n][0] - ref[n]).length * 100.0, n) for n in joints if n in ref)
    worst = rows[-6:]
    mean = sum(d for d, _ in rows) / len(rows)
    print(f"sanity: mean joint distance to local dump {mean:.1f} cm; largest: "
          + ", ".join(f"{n} {d:.1f} cm" for d, n in reversed(worst)))


# ---------------------------------------------------------------------------------------------
# Turnaround renders (scratch only, never saved into the .blend)
# ---------------------------------------------------------------------------------------------

def render_turnarounds(out_dir):
    scene = bpy.context.scene
    scene.render.engine = "BLENDER_WORKBENCH"
    scene.display.shading.light = "STUDIO"
    scene.display.shading.color_type = "SINGLE"
    scene.display.shading.single_color = (0.62, 0.62, 0.62)
    scene.display.shading.show_cavity = True
    scene.render.resolution_x, scene.render.resolution_y = 640, 1000
    scene.render.film_transparent = False
    scene.render.image_settings.file_format = "PNG"
    world = bpy.data.worlds.new("turn_world") if scene.world is None else scene.world
    scene.world = world
    cam_data = bpy.data.cameras.new("turn_cam")
    cam_data.type = "ORTHO"
    cam_data.ortho_scale = 2.05
    cam = bpy.data.objects.new("turn_cam", cam_data)
    scene.collection.objects.link(cam)
    scene.camera = cam
    views = {
        "front": ((0.0, -6.0, 0.92), (math.pi / 2, 0.0, 0.0)),
        "side": ((6.0, 0.0, 0.92), (math.pi / 2, 0.0, math.pi / 2)),
        "back": ((0.0, 6.0, 0.92), (math.pi / 2, 0.0, math.pi)),
    }
    for view, (loc, rot) in views.items():
        cam.location, cam.rotation_euler = loc, rot
        scene.render.filepath = str(Path(out_dir) / f"turn_{view}.png")
        bpy.ops.render.render(write_still=True)
    bpy.data.objects.remove(cam, do_unlink=True)


# ---------------------------------------------------------------------------------------------

def reset_scene():
    for o in list(bpy.data.objects):
        bpy.data.objects.remove(o, do_unlink=True)
    for coll in (bpy.data.meshes, bpy.data.materials, bpy.data.cameras, bpy.data.lights,
                 bpy.data.armatures):
        for block in list(coll):
            coll.remove(block)
    u = bpy.context.scene.unit_settings
    u.system, u.scale_length, u.length_unit = "METRIC", 1.0, "METERS"


def to_centimetre_scene(obj, arm):
    """Skeletal export path of the export skill: Unit Scale 0.01, armature scaled 100 and applied.

    Built in metres, saved and exported in centimetres: from a metre scene UE 5.8 imports the root
    bone at scale 100. The mesh is a child of the armature and follows it.
    """
    u = bpy.context.scene.unit_settings
    u.scale_length, u.length_unit = 0.01, "CENTIMETERS"
    arm.scale = (100.0, 100.0, 100.0)
    bpy.context.view_layer.update()
    for o in bpy.context.scene.objects:
        o.select_set(o in (arm, obj))
    bpy.context.view_layer.objects.active = arm
    with bpy.context.temp_override(active_object=arm, object=arm, selected_objects=[arm, obj],
                                   selected_editable_objects=[arm, obj]):
        bpy.ops.object.transform_apply(location=False, rotation=False, scale=True)
    zs = [v.co.z for v in obj.data.vertices]
    if abs(max(zs) - min(zs) - HEIGHT * 100.0) > HEIGHT_TOLERANCE_CM or tuple(arm.scale) != (1.0, 1.0, 1.0):
        raise RuntimeError("centimetre conversion went wrong")


def export_fbx(blend_path, fbx_path):
    """Export through the blender-to-unreal-export skill script, so its preset and checks apply."""
    cmd = [bpy.app.binary_path, "-b", str(blend_path), "--factory-startup", "--python-exit-code", "1",
           "--python", str(EXPORT_SCRIPT), "--", "--out", str(fbx_path), "--kind", "skeletal", "--all"]
    r = subprocess.run(cmd, capture_output=True, text=True)
    tail = [ln for ln in r.stdout.splitlines() if "EXPORT" in ln]
    print("\n".join(tail))
    if r.returncode != 0:
        print(r.stdout[-3000:], r.stderr[-2000:])
        raise RuntimeError(f"FBX export failed with exit code {r.returncode}")


def main():
    args = parse_args()
    if args.out_dir:
        out_dir = scratch_dir = Path(args.out_dir).resolve()
    else:
        out_dir, scratch_dir = HERE, SCRATCH
    scratch_dir.mkdir(parents=True, exist_ok=True)
    out_dir.mkdir(parents=True, exist_ok=True)

    reset_scene()
    ref = load_reference()
    joints, left = all_joints()
    missing = [b["name"] for b in ref if b["name"] not in joints]
    if missing:
        raise RuntimeError(f"no joint position for {missing}")

    me = skin_mesh(build_graph(left))
    me.name = NAME
    shift, scale = shape(me)
    joints = {n: ((h + shift) * scale, (t + shift) * scale) for n, (h, t) in joints.items()}
    joints.update({n: origin_bone() for n in ORIGIN_BONES})  # the origin stays the origin
    compare_local(joints)

    obj = bpy.data.objects.new(NAME, me)
    bpy.context.scene.collection.objects.link(obj)
    me.shade_smooth()
    add_material(obj)
    unwrap(obj)

    arm = build_armature(ref, joints)
    heat_weights(obj, arm, ref)
    correct_weights(obj, joints)

    checks, ok = run_checks(obj, arm, ref)
    (scratch_dir / "checks.json").write_text(json.dumps(checks, indent=2), encoding="utf-8")
    print("checks: " + json.dumps(checks))

    render_turnarounds(scratch_dir)
    if not args.check_only:
        to_centimetre_scene(obj, arm)
        blend = out_dir / f"{NAME}.blend"
        bpy.ops.wm.save_as_mainfile(filepath=str(blend), compress=True)
        export_fbx(blend, out_dir / f"{NAME}.fbx")

    if not ok:
        print("BUILD CHECK FAILED")
        sys.exit(1)
    print("BUILD OK")


main()
