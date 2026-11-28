"""Keyframe the fighting stance and the strikes on the base character's rig (decision 0025).

Run headless on the committed character:
    pwsh tools/blender/run.ps1 -Blend Art/Characters/Base/SK_BaseCharacter.blend \
        -Script Art/Characters/Base/Strikes/build_strikes.py -- [--install] [--render DIR]

Every clip is posed from a few key poses written in character space (centimetres, the character
faces -Y, its left is +X, up is +Z): where the pelvis is and how it turns, how far the spine
turns and leans, where each wrist and ankle goes and which way the elbows and knees point, and
how the hands and feet are turned. Each frame is solved by script (two-bone IK for the limbs,
the hands and feet turned to their targets, the fingers closed into a fist) and keyed on every
bone, so the clip is the same every time it is built.

Orthodox stance: left foot and left hand lead. A clip starts and ends in the stance, so they
blend into each other and into the stance loop.

Writes, per clip, an action A_<Name> in a copy of the character's .blend and an FBX through the
export skill (--kind anim), into _scratch/strikes (or into this folder with --install), plus
strikes.json with each clip's length and its active frames (when the fist or foot can land).
"""

import argparse
import json
import math
import subprocess
import sys
from pathlib import Path

import bpy
from mathutils import Matrix, Quaternion, Vector

HERE = Path(__file__).resolve().parent
REPO = HERE.parents[3]
SCRATCH = REPO / "_scratch" / "strikes"
EXPORT_SCRIPT = REPO / ".claude" / "skills" / "blender-to-unreal-export" / "export_unreal_fbx.py"
FPS = 30

FORWARD = Vector((0.0, -1.0, 0.0))
UP = Vector((0.0, 0.0, 1.0))

# How far each finger joint closes for a fist, degrees (knuckle, middle, tip), and the thumb.
FIST = {"index": (85, 105, 70), "middle": (90, 105, 70), "ring": (92, 105, 70), "pinky": (95, 105, 70)}
THUMB = {"thumb_01": 50, "thumb_02": 35, "thumb_03": 35}
SPINE = ("spine_01", "spine_02", "spine_03", "spine_04", "spine_05")
# Share of the spine's turn each bone takes, bottom to top.
SPINE_SHARE = (0.15, 0.2, 0.2, 0.25, 0.2)


def parse_args():
    argv = sys.argv[sys.argv.index("--") + 1:] if "--" in sys.argv else []
    p = argparse.ArgumentParser()
    p.add_argument("--install", action="store_true", help="write into Art/Characters/Base/Strikes")
    p.add_argument("--render", help="render preview frames of every clip into this folder")
    p.add_argument("--only", help="comma-separated clip names")
    return p.parse_args(argv)


# --------------------------------------------------------------------------------------------
# Poses


def V(x, y, z):
    return Vector((x, y, z))


def stance(**over):
    """The orthodox guard every clip starts and ends in. Keyword arguments override parts."""
    pose = {
        "pelvis": V(0.0, 1.5, -5.5),    # offset from the rest pelvis: a little back, knees bent
        "pelvis_yaw": -28.0,            # degrees about +Z; negative turns the left hip forward
        "pelvis_pitch": 0.0,            # degrees; positive tips the pelvis forward
        "spine_yaw": -8.0,              # added on the pelvis, spread over the spine
        "spine_pitch": 6.0,             # positive leans forward
        "spine_roll": 0.0,              # positive leans to the character's left
        "head_yaw": 0.0,                # the head stays on the opponent (-Y), whatever the spine does
        "head_pitch": 8.0,              # chin down
        "clav_l": 0.0, "clav_r": 0.0,   # degrees the shoulder rises (protects the chin on a punch)
        "wrist_l": V(13.0, -30.0, 143.0),
        "wrist_r": V(-17.0, -11.0, 143.0),
        "elbow_l": V(30.0, 0.0, 95.0),  # pole points: which way the elbow goes
        "elbow_r": V(-30.0, 10.0, 95.0),
        "palm_l": V(-1.0, -0.2, 0.0),   # which way the palm faces (wrist kept straight)
        "palm_r": V(1.0, -0.3, 0.0),
        "ankle_l": V(15.0, -18.0, 7.5),
        "ankle_r": V(-15.0, 20.0, 7.5),
        "foot_yaw_l": -12.0, "foot_yaw_r": -40.0,  # toe direction, degrees from forward; negative turns right
        "heel_l": 0.0, "heel_r": 0.0,   # degrees the heel is lifted, turning about the ball
        "knee_l": V(40.0, -60.0, 50.0),
        "knee_r": V(-30.0, -40.0, 50.0),
        "toe_dir_r": None,              # a kicking foot: where the toes point (overrides yaw and heel)
    }
    pose.update(over)
    return pose


def jab():
    peak = stance(
        pelvis=V(0.0, -3.0, -5.5), pelvis_yaw=-36.0, spine_yaw=-14.0, spine_pitch=9.0, spine_roll=-3.0,
        clav_l=14.0, wrist_l=V(5.0, -66.0, 149.0), elbow_l=V(60.0, -30.0, 110.0), palm_l=V(0.0, 0.0, -1.0),
        wrist_r=V(-9.0, -16.0, 146.0), ankle_l=V(15.0, -24.0, 7.5), knee_l=V(40.0, -70.0, 50.0))
    load = stance(pelvis=V(0.0, 2.5, -6.5), wrist_l=V(9.0, -32.0, 140.0))
    return [(0, stance(), "inout"), (2, load, "out"), (5, peak, "inout"), (6, peak, "inout"), (13, stance(), None)], (3, 6)


def cross():
    load = stance(pelvis=V(0.0, 2.0, -7.0), pelvis_yaw=-32.0, spine_yaw=-10.0)
    peak = stance(
        pelvis=V(-2.0, -7.0, -6.0), pelvis_yaw=2.0, spine_yaw=28.0, spine_pitch=12.0, spine_roll=2.0,
        clav_r=14.0, wrist_r=V(-3.0, -65.0, 148.0), elbow_r=V(-60.0, -30.0, 110.0), palm_r=V(0.0, 0.0, -1.0),
        wrist_l=V(4.0, -24.0, 147.0), palm_l=V(-1.0, 0.3, 0.0),
        heel_r=38.0, foot_yaw_r=-10.0, ankle_r=V(-14.0, 17.0, 7.5), knee_r=V(10.0, -80.0, 45.0))
    return [(0, stance(), "inout"), (3, load, "out"), (6, peak, "inout"), (7, peak, "inout"), (16, stance(), None)], (4, 7)


def hook_lead():
    load = stance(pelvis=V(1.0, 1.0, -7.5), pelvis_yaw=-18.0, spine_yaw=4.0,
                  wrist_l=V(22.0, -28.0, 144.0), elbow_l=V(70.0, -10.0, 150.0), palm_l=V(-0.3, 0.0, -1.0))
    peak = stance(
        pelvis=V(-1.0, -8.0, -7.0), pelvis_yaw=-62.0, spine_yaw=-26.0, spine_pitch=14.0, spine_roll=-4.0,
        clav_l=10.0, wrist_l=V(-3.0, -57.0, 148.0), elbow_l=V(70.0, -40.0, 160.0), palm_l=V(0.0, 0.3, -1.0),
        wrist_r=V(-10.0, -18.0, 147.0),
        foot_yaw_l=-65.0, heel_l=18.0, knee_l=V(-10.0, -70.0, 50.0))
    return [(0, stance(), "inout"), (3, load, "out"), (7, peak, "inout"), (8, peak, "inout"), (18, stance(), None)], (5, 8)


def hook_rear():
    load = stance(pelvis=V(-1.0, 2.0, -7.0), pelvis_yaw=-34.0, spine_yaw=-14.0,
                  wrist_r=V(-24.0, -10.0, 144.0), elbow_r=V(-70.0, 10.0, 150.0), palm_r=V(0.3, 0.0, -1.0))
    peak = stance(
        pelvis=V(-1.0, -11.0, -7.0), pelvis_yaw=6.0, spine_yaw=30.0, spine_pitch=14.0, spine_roll=4.0,
        clav_r=10.0, wrist_r=V(3.0, -58.0, 148.0), elbow_r=V(-70.0, -40.0, 160.0), palm_r=V(0.0, 0.3, -1.0),
        wrist_l=V(5.0, -24.0, 147.0), palm_l=V(-1.0, 0.3, 0.0),
        heel_r=40.0, foot_yaw_r=0.0, knee_r=V(20.0, -80.0, 45.0))
    return [(0, stance(), "inout"), (3, load, "out"), (7, peak, "inout"), (8, peak, "inout"), (18, stance(), None)], (5, 8)


def front_kick():
    # Rear-leg push kick: the knee comes up, the foot drives out to the body, the torso leans back
    # over the standing leg, and the foot comes back down to the stance.
    chamber = stance(
        pelvis=V(3.0, -2.0, -4.0), pelvis_yaw=-12.0, spine_yaw=-10.0, spine_pitch=-4.0,
        ankle_r=V(-9.0, -22.0, 62.0), knee_r=V(-10.0, -80.0, 110.0), toe_dir_r=V(0.0, -0.6, -0.8),
        wrist_r=V(-14.0, -14.0, 140.0), ankle_l=V(13.0, -16.0, 7.5), knee_l=V(40.0, -60.0, 50.0))
    peak = stance(
        pelvis=V(3.0, -6.0, -3.0), pelvis_yaw=-6.0, pelvis_pitch=-12.0, spine_yaw=-8.0, spine_pitch=-10.0,
        ankle_r=V(-8.0, -80.0, 98.0), knee_r=V(-10.0, -60.0, 140.0), toe_dir_r=V(0.0, -0.35, 0.94),
        wrist_r=V(-20.0, -6.0, 130.0), wrist_l=V(10.0, -26.0, 141.0), ankle_l=V(13.0, -16.0, 7.5),
        knee_l=V(40.0, -60.0, 50.0), foot_yaw_l=-25.0)
    back = stance(
        pelvis=V(2.0, -2.0, -4.5), pelvis_yaw=-14.0, ankle_r=V(-10.0, -18.0, 55.0),
        knee_r=V(-10.0, -80.0, 100.0), toe_dir_r=V(0.0, -0.6, -0.8), ankle_l=V(13.0, -16.0, 7.5))
    return [(0, stance(), "inout"), (6, chamber, "out"), (11, peak, "inout"), (13, peak, "inout"),
            (19, back, "inout"), (26, stance(), None)], (9, 13)


def stance_loop():
    # A loose bounce on the balls of the feet, one cycle per clip, so it loops.
    keys = []
    n = 40
    for f in range(0, n + 1, 5):
        a = 2.0 * math.pi * f / n
        b = math.sin(a)
        c = math.sin(a + 1.2)
        keys.append((f, stance(
            pelvis=V(0.0, 1.5, -5.5 + 1.3 * b), spine_yaw=-8.0 + 2.0 * c,
            wrist_l=V(13.0, -30.0 + 1.0 * c, 143.0 + 1.2 * b), wrist_r=V(-17.0, -11.0, 143.0 + 1.0 * c)), "linear"))
    return keys, None


def block_loop():
    # The high guard: forearms up in front of the face, fists at the forehead, elbows in over the
    # ribs, chin down behind the gloves and the knees a little deeper. A slow breath so it loops.
    keys = []
    n = 40
    for f in range(0, n + 1, 10):
        b = math.sin(2.0 * math.pi * f / n)
        keys.append((f, stance(
            pelvis=V(0.0, 2.0, -8.0 + 0.8 * b), pelvis_yaw=-22.0, spine_yaw=-6.0, spine_pitch=14.0,
            head_pitch=16.0, clav_l=6.0, clav_r=6.0,
            wrist_l=V(8.0, -27.0, 147.0 + 0.6 * b), wrist_r=V(-8.0, -23.0, 148.0 + 0.6 * b),
            elbow_l=V(10.0, -30.0, 85.0), elbow_r=V(-10.0, -30.0, 85.0),
            palm_l=V(-0.3, 1.0, 0.0), palm_r=V(0.3, 1.0, 0.0)), "linear"))
    return keys, None


# The shuffle: how far each foot travels per cycle (cm) and the cycle length (frames). In place, so
# the planted foot slides back at the ground speed while the other swings forward.
SHUFFLE_STRIDE = 30.0
SHUFFLE_FRAMES = 12


def shuffle_loop():
    # Forward in the stance: the lead foot steps, the rear foot follows, low and short, the heel of
    # the rear foot up. Played backward for backing off; the legs turn toward a sideways move.
    def foot(phase, start, swing):
        local = (phase - start) % 1.0
        if local < swing:
            t = local / swing
            return SHUFFLE_STRIDE * (0.5 - (t * t * (3.0 - 2.0 * t))), 5.0 * math.sin(math.pi * t)
        t = (local - swing) / (1.0 - swing)
        return SHUFFLE_STRIDE * (-0.5 + t), 0.0
    keys = []
    for f in range(SHUFFLE_FRAMES + 1):
        phase = f / SHUFFLE_FRAMES
        lead, lead_up = foot(phase, 0.0, 0.36)
        rear, rear_up = foot(phase, 0.4, 0.36)
        keys.append((f, stance(
            pelvis=V(0.0, 1.5, -5.5 - 0.8 * abs(math.sin(2.0 * math.pi * phase))),
            ankle_l=V(15.0, -18.0 + lead, 7.5 + lead_up), ankle_r=V(-15.0, 20.0 + rear, 7.5 + rear_up),
            heel_r=12.0 if rear_up < 0.5 else 0.0), "linear"))
    return keys, None


def side_shuffle_loop():
    # Sideways in the stance, to the right: the right foot steps out, the left follows, the stance
    # kept (no crossing the feet). Played backward for a step to the left.
    def foot(phase, start, swing):
        local = (phase - start) % 1.0
        if local < swing:
            t = local / swing
            return SHUFFLE_STRIDE * (0.5 - (t * t * (3.0 - 2.0 * t))), 5.0 * math.sin(math.pi * t)
        t = (local - swing) / (1.0 - swing)
        return SHUFFLE_STRIDE * (-0.5 + t), 0.0
    keys = []
    for f in range(SHUFFLE_FRAMES + 1):
        phase = f / SHUFFLE_FRAMES
        right, right_up = foot(phase, 0.0, 0.36)
        left, left_up = foot(phase, 0.4, 0.36)
        # Moving right is -X: a planted foot slides toward +X (behind the motion).
        keys.append((f, stance(
            pelvis=V(0.0, 1.5, -5.5 - 0.8 * abs(math.sin(2.0 * math.pi * phase))),
            ankle_l=V(15.0 + left, -18.0, 7.5 + left_up), ankle_r=V(-15.0 + right, 20.0, 7.5 + right_up),
            heel_r=12.0 if right_up < 0.5 else 0.0), "linear"))
    return keys, None


CLIPS = {
    "A_Stance_Idle": stance_loop,
    "A_Stance_ShuffleSide": side_shuffle_loop,
    "A_Stance_Shuffle": shuffle_loop,
    "A_Block_High": block_loop,
    "A_Strike_Jab": jab,
    "A_Strike_Cross": cross,
    "A_Strike_HookLead": hook_lead,
    "A_Strike_HookRear": hook_rear,
    "A_Strike_FrontKick": front_kick,
}


# --------------------------------------------------------------------------------------------
# Interpolation


def ease(kind, t):
    if kind == "out":       # fast start, soft stop: a strike going out
        return 1.0 - (1.0 - t) ** 3
    if kind == "inout":
        return t * t * (3.0 - 2.0 * t)
    return t


def blend(a, b, t):
    out = {}
    for k, va in a.items():
        vb = b[k]
        if va is None or vb is None:
            out[k] = vb if t >= 0.5 else va
        elif isinstance(va, Vector):
            out[k] = va.lerp(vb, t)
        else:
            out[k] = va + (vb - va) * t
    return out


def pose_at(keys, frame):
    for (f0, p0, kind), (f1, p1, _) in zip(keys, keys[1:]):
        if f0 <= frame <= f1:
            return blend(p0, p1, ease(kind, (frame - f0) / max(f1 - f0, 1)))
    return keys[-1][1]


# --------------------------------------------------------------------------------------------
# Solving a pose


class Rig:
    def __init__(self, arm):
        self.arm = arm
        self.pb = arm.pose.bones
        self.rest = {b.name: b.matrix_local.copy() for b in arm.data.bones}
        for p in self.pb:
            p.rotation_mode = "QUATERNION"
        self.len = {n: (self.rest_head(c) - self.rest_head(n)).length for n, c in (
            ("upperarm_l", "lowerarm_l"), ("lowerarm_l", "hand_l"), ("upperarm_r", "lowerarm_r"), ("lowerarm_r", "hand_r"),
            ("thigh_l", "calf_l"), ("calf_l", "foot_l"), ("thigh_r", "calf_r"), ("calf_r", "foot_r"))}
        self.fist_basis = self._fist_basis()

    def rest_head(self, name):
        return self.rest[name].translation.copy()

    def update(self):
        bpy.context.view_layer.update()

    def head(self, name):
        return self.pb[name].matrix.translation.copy()

    def dir(self, name):
        m = self.pb[name].matrix
        return Vector((m[0][1], m[1][1], m[2][1])).normalized()

    def rotate_about(self, name, rot, pivot=None):
        """Turn a bone (and so everything below it) by rot (a 3x3) in armature space."""
        p = self.pb[name]
        c = self.head(name) if pivot is None else pivot
        m = Matrix.Translation(c) @ rot.to_4x4() @ Matrix.Translation(-c)
        p.matrix = m @ p.matrix
        self.update()

    def aim(self, name, target):
        """Swing a bone so its tail points at target, the least turn from where it is now."""
        d = (target - self.head(name)).normalized()
        self.rotate_about(name, self.dir(name).rotation_difference(d).to_matrix())

    def two_bone(self, upper, lower, end_target, pole):
        """Place upper and lower so the end of lower reaches end_target, bending toward pole."""
        root = self.head(upper)
        a, b = self.len[upper], self.len[lower]
        to = end_target - root
        d = min(max(to.length, abs(a - b) + 0.1), a + b - 0.05)
        axis = to.normalized()
        # Elbow (knee) on the side of the pole, in the plane of root, target and pole.
        side = (pole - root) - axis * (pole - root).dot(axis)
        side = side.normalized() if side.length > 1e-3 else UP.cross(axis).normalized()
        along = (a * a - b * b + d * d) / (2.0 * d)
        up = math.sqrt(max(a * a - along * along, 0.0))
        mid = root + axis * along + side * up
        self.aim(upper, mid)
        self.aim(lower, root + axis * d)

    def orient(self, name, forward, normal, rest_forward, rest_normal):
        """Turn a bone so rest_forward (armature space at rest) goes to forward, rest_normal to normal."""
        def frame(f, n):
            f = f.normalized()
            n = (n - f * n.dot(f)).normalized()
            return Matrix((f, n, f.cross(n))).transposed()
        want = frame(forward, normal) @ frame(rest_forward, rest_normal).inverted()
        p = self.pb[name]
        rest_rot = self.rest[name].to_3x3()
        m = (want @ rest_rot).to_4x4()
        m.translation = self.head(name)
        p.matrix = m
        self.update()

    def _fist_basis(self):
        """Local rotations that close each hand into a fist (signs found against the palm)."""
        out = {}
        for side, medial in (("l", -1.0), ("r", 1.0)):
            n = self.palm_normal(side, medial)
            for finger, angles in FIST.items():
                for i, deg in enumerate(angles, start=1):
                    name = f"{finger}_0{i}_{side}"
                    out[name] = self._curl(name, deg, n)
            for name, deg in THUMB.items():
                # The thumb folds across the front of the fingers: about the hand's length.
                along = (self.rest_head(f"middle_01_{side}") - self.rest_head(f"hand_{side}")).normalized()
                out[f"{name}_{side}"] = self._curl(f"{name}_{side}", deg, n, along)
        return out

    def palm_normal(self, side, medial):
        """At rest the palms face the thighs: the normal is the one pointing to the midline."""
        idx, pky = self.rest_head(f"index_01_{side}"), self.rest_head(f"pinky_01_{side}")
        down = (self.rest_head(f"middle_01_{side}") - self.rest_head(f"hand_{side}")).normalized()
        n = (pky - idx).cross(down).normalized()
        return n if n.x * medial > 0 else -n

    def _curl(self, name, deg, palm, axis=None):
        # Flexion turns a finger about the axis across it in the plane of the palm (not about the
        # bone's own X, which on this rig leans about 30 degrees and bent the fingers into claws).
        rest = self.rest[name]
        tail = self.arm.data.bones[name].tail_local - self.arm.data.bones[name].head_local
        world_axis = axis if axis is not None else tail.normalized().cross(palm).normalized()
        local_axis = rest.to_3x3().inverted() @ world_axis
        best = None
        for sign in (1.0, -1.0):
            q = Quaternion(local_axis, math.radians(deg) * sign)
            moved = rest.to_3x3() @ q.to_matrix() @ rest.to_3x3().inverted() @ tail
            score = moved.dot(palm)
            if best is None or score > best[0]:
                best = (score, q)
        return best[1]

    def solve(self, pose):
        """Set every bone for one pose."""
        for p in self.pb:
            p.matrix_basis = Matrix.Identity(4)
        self.update()

        # Pelvis: moved and turned about its own head.
        pel = self.rest_head("pelvis")
        rot = (Matrix.Rotation(math.radians(pose["pelvis_yaw"]), 3, "Z")
               @ Matrix.Rotation(math.radians(pose["pelvis_pitch"]), 3, "X"))
        p = self.pb["pelvis"]
        m = Matrix.Translation(pel + pose["pelvis"]) @ rot.to_4x4() @ Matrix.Translation(-pel) @ self.rest["pelvis"]
        p.matrix = m
        self.update()

        # Spine: the turn and lean on top of the pelvis, spread up the spine. The lean is about the
        # character's own side axis, so it leans toward where the chest faces.
        for name, share in zip(SPINE, SPINE_SHARE):
            yaw = Matrix.Rotation(math.radians(pose["spine_yaw"] * share), 3, "Z")
            self.rotate_about(name, yaw)
            facing = Matrix.Rotation(math.radians(pose["pelvis_yaw"] + pose["spine_yaw"]), 3, "Z")
            side_axis = facing @ Vector((1.0, 0.0, 0.0))
            fwd_axis = facing @ FORWARD
            self.rotate_about(name, Matrix.Rotation(math.radians(pose["spine_pitch"] * share), 3, side_axis))
            self.rotate_about(name, Matrix.Rotation(math.radians(-pose["spine_roll"] * share), 3, fwd_axis))

        # Neck and head: back to looking at the opponent, chin down.
        want = (Matrix.Rotation(math.radians(pose["head_yaw"]), 3, "Z")
                @ Matrix.Rotation(math.radians(pose["head_pitch"]), 3, "X"))
        have = self.pb["head"].matrix.to_3x3() @ self.rest["head"].to_3x3().inverted()
        fix = (want @ have.inverted()).to_quaternion()
        half = Quaternion().slerp(fix, 0.5).to_matrix()
        self.rotate_about("neck_01", half)
        have = self.pb["head"].matrix.to_3x3() @ self.rest["head"].to_3x3().inverted()
        self.rotate_about("head", want @ have.inverted())

        # Shoulders up on the punching side.
        for side in ("l", "r"):
            deg = pose[f"clav_{side}"]
            if deg:
                axis = self.dir(f"clavicle_{side}").cross(UP).normalized()
                self.rotate_about(f"clavicle_{side}", Matrix.Rotation(math.radians(deg), 3, axis))

        # Arms and hands.
        for side in ("l", "r"):
            self.two_bone(f"upperarm_{side}", f"lowerarm_{side}", pose[f"wrist_{side}"], pose[f"elbow_{side}"])
            fwd = self.dir(f"lowerarm_{side}")
            rest_fwd = (self.rest_head(f"middle_01_{side}") - self.rest_head(f"hand_{side}")).normalized()
            rest_palm = self.palm_normal(side, -1.0 if side == "l" else 1.0)
            before = self.pb[f"hand_{side}"].matrix.to_3x3()
            self.orient(f"hand_{side}", fwd, pose[f"palm_{side}"], rest_fwd, rest_palm)
            # Half the hand's twist about the forearm goes to the forearm twist bone, so the wrist
            # does not wring the skin.
            after = self.pb[f"hand_{side}"].matrix.to_3x3()
            q = (after @ before.inverted()).to_quaternion()
            proj = Vector((q.x, q.y, q.z)).dot(fwd)
            twist = Quaternion((q.w, fwd.x * proj, fwd.y * proj, fwd.z * proj)).normalized()
            self.rotate_about(f"lowerarm_twist_01_{side}", Quaternion().slerp(twist, 0.5).to_matrix())

        # Legs and feet.
        for side in ("l", "r"):
            toe_dir = pose.get(f"toe_dir_{side}")
            ball_rest = self.rest_head(f"ball_{side}")
            ankle_rest = self.rest_head(f"foot_{side}")
            yaw = Matrix.Rotation(math.radians(pose[f"foot_yaw_{side}"]), 3, "Z")
            if toe_dir is None:
                # Planted: the ball on the floor where the ankle target puts it; a lifted heel turns
                # the foot about the ball.
                heel = math.radians(pose[f"heel_{side}"])
                lift = Matrix.Rotation(heel, 3, yaw @ Vector((1.0, 0.0, 0.0)))
                ankle_xy = pose[f"ankle_{side}"]
                ball = ankle_xy + yaw @ (ball_rest - ankle_rest)
                ankle = ball + lift @ (yaw @ (ankle_rest - ball_rest))
                self.two_bone(f"thigh_{side}", f"calf_{side}", ankle, pose[f"knee_{side}"])
                self.orient(f"foot_{side}", lift @ yaw @ (ball_rest - ankle_rest), lift @ yaw @ UP,
                            ball_rest - ankle_rest, UP)
                if heel:
                    self.orient(f"ball_{side}", yaw @ self._rest_dir(f"ball_{side}"), yaw @ UP,
                                self._rest_dir(f"ball_{side}"), UP)
            else:
                self.two_bone(f"thigh_{side}", f"calf_{side}", pose[f"ankle_{side}"], pose[f"knee_{side}"])
                rest_toe = (ball_rest - ankle_rest).normalized()
                self.orient(f"foot_{side}", toe_dir, self._foot_top(toe_dir), rest_toe, UP)

        # Fists.
        for name, q in self.fist_basis.items():
            self.pb[name].rotation_quaternion = q
        self.update()

    def _rest_dir(self, name):
        b = self.arm.data.bones[name]
        return (b.tail_local - b.head_local).normalized()

    @staticmethod
    def _foot_top(toe_dir):
        """Which way the top of a foot faces when its toes point along toe_dir (no roll about it)."""
        # Toes forward gives +Z, toes up gives a top facing back (the sole forward).
        return toe_dir.cross(Vector((1.0, 0.0, 0.0))).normalized()

    def key(self, frame):
        for p in self.pb:
            p.keyframe_insert("location", frame=frame, group=p.name)
            p.keyframe_insert("rotation_quaternion", frame=frame, group=p.name)


# --------------------------------------------------------------------------------------------


def checks(rig, name, keys, active):
    """Numbers to compare against what the clip should do."""
    out = {}
    if active:
        peak = keys[[k[0] for k in keys].index(active[1])][1] if active[1] in [k[0] for k in keys] else None
        frame = active[1]
        rig.solve(pose_at(keys, frame))
        for side in ("l", "r"):
            fist = rig.head(f"middle_01_{side}")
            out[f"fist_{side}"] = [round(c, 1) for c in fist]
        out["foot_r"] = [round(c, 1) for c in rig.head("ball_r")]
        del peak
    return out


def build(arm, only):
    rig = Rig(arm)
    arm.animation_data_create()
    meta = {}
    for name, make in CLIPS.items():
        if only and name not in only:
            continue
        keys, active = make()
        action = bpy.data.actions.new(name)
        action.use_fake_user = True
        arm.animation_data.action = action
        last = keys[-1][0]
        for f in range(0, last + 1):
            rig.solve(pose_at(keys, f))
            rig.key(f)
        meta[name] = {
            "frames": last + 1, "seconds": round(last / FPS, 4),
            "active": None if active is None else {"start": round(active[0] / FPS, 4), "end": round(active[1] / FPS, 4)},
            "loop": active is None,
            "stride_cm_s": round(SHUFFLE_STRIDE * FPS / SHUFFLE_FRAMES, 2) if name in ("A_Stance_Shuffle", "A_Stance_ShuffleSide") else 0.0,
            "checks": checks(rig, name, keys, active),
        }
        print(f"STRIKES {name}: {last + 1} frames, {meta[name]}")
    return meta


def render(arm, out_dir, only):
    """Side and front frames of each clip, workbench, for review."""
    out_dir.mkdir(parents=True, exist_ok=True)
    scene = bpy.context.scene
    scene.render.engine = "BLENDER_WORKBENCH"
    scene.render.resolution_x, scene.render.resolution_y = 480, 480
    scene.render.film_transparent = False
    cams = {}
    for label, loc, rot in (("side", (320.0, -40.0, 110.0), (90.0, 0.0, 90.0)),
                            ("front", (60.0, -330.0, 120.0), (88.0, 0.0, 10.0))):
        cam = bpy.data.objects.new(f"CAM_{label}", bpy.data.cameras.new(label))
        cam.location = loc
        cam.rotation_euler = [math.radians(a) for a in rot]
        cam.data.lens = 40
        cam.data.clip_end = 5000
        scene.collection.objects.link(cam)
        cams[label] = cam
    for action in bpy.data.actions:
        if only and action.name not in only:
            continue
        arm.animation_data.action = action
        start, end = (int(v) for v in action.frame_range)
        for f in range(start, end + 1, 2 if end - start > 14 else 1):
            scene.frame_set(f)
            for label, cam in cams.items():
                scene.camera = cam
                scene.render.filepath = str(out_dir / f"{action.name}_{label}_{f:02d}.png")
                bpy.ops.render.render(write_still=True)


def export(blend_path, out_dir, names):
    for name in names:
        fbx = out_dir / f"{name}.fbx"
        cmd = [bpy.app.binary_path, "-b", str(blend_path), "--factory-startup", "--python-exit-code", "1",
               "--python", str(EXPORT_SCRIPT), "--", "--out", str(fbx), "--kind", "anim", "--all", "--action", name]
        r = subprocess.run(cmd, capture_output=True, text=True)
        print("\n".join(ln for ln in r.stdout.splitlines() if "EXPORT" in ln))
        if r.returncode != 0:
            print(r.stdout[-3000:], r.stderr[-2000:])
            raise RuntimeError(f"export of {name} failed")


def main():
    args = parse_args()
    only = set(args.only.split(",")) if args.only else None
    arm = next(o for o in bpy.data.objects if o.type == "ARMATURE")
    for o in bpy.data.objects:
        if o.type == "MESH":
            for m in o.modifiers:
                if m.type == "ARMATURE":
                    m.show_viewport = not not args.render  # skinning only matters for the previews
    bpy.context.scene.render.fps = FPS
    meta = build(arm, only)
    out_dir = HERE if args.install else SCRATCH
    out_dir.mkdir(parents=True, exist_ok=True)
    for o in bpy.data.objects:
        for m in getattr(o, "modifiers", []):
            m.show_viewport = True
        o.select_set(o.type == "ARMATURE")
    blend = (SCRATCH if not args.install else REPO / "_scratch" / "strikes") / "strikes_rig.blend"
    blend.parent.mkdir(parents=True, exist_ok=True)
    arm.animation_data.action = None
    bpy.ops.wm.save_as_mainfile(filepath=str(blend), copy=True)
    export(blend, out_dir, list(meta))
    (out_dir / "strikes.json").write_text(json.dumps(meta, indent=2) + "\n", encoding="utf-8")
    if args.render:
        render(arm, Path(args.render).resolve(), only)
    print("STRIKES done")


main()
