# Base character: build notes

Read by `build_base_character.py` (the Blender builder). These are the verified facts it builds on.

## Skeleton reference: names and hierarchy only

`skeleton_reference.json` is the UE5 humanoid bone layout, transcribed: 89 bone names with their
parents (parent before child, `root` first), side, group, an `optional` flag and a roll note.
It was written by `tools/ue/dump_skeleton.py`, run in a throwaway scratch project (not `Game/`)
against the UE5 mannequin that ships with the engine's example content (UE 5.8.3).

- Joint positions are deliberately not committed. Positions transcribed from Epic's mannequin are
  derived Epic data, and the public repo holds no Epic content. The builder sets its own joint
  positions from documented human proportions for a 180 cm adult.
- The dump also writes the full positions to `_scratch/skeldump/skeleton_positions_local.json`
  (gitignored). If that file exists locally, the builder may use it only as a sanity comparison
  (for example, warn when a joint is far off); it must work without it and never copy from it.
- `optional: true` marks the ik bones (`ik_*`), `interaction` and `center_of_mass`. They are part
  of the UE5 layout but carry no mesh weights; the builder decides whether to include them. Twist
  bones are not optional.
- `roll_deg` is the Blender roll that lines the Blender bone's Z axis up with the Unreal bone's
  local Z axis. The values are derived from the engine skeleton's rest rotations (allowed by the
  design as roll notes) and are the only numbers in the file taken from it. A note for matching
  orientations, not a requirement: the IK Retargeter maps chains by name.

## Rest pose: A-pose

The UE5 humanoid rest pose is an A-pose, with the upper arm about 55 degrees below horizontal.
Build the base character in an A-pose at about that angle, so the IK Retargeter needs little or
no pose correction against UE5 humanoid animations.

## Forward axis: build facing Blender -Y

Scope: this section records the static mesh round-trip. The skeletal export (armature
orientation, root bone, bone axes, scale on the root) was verified afterwards with the base
character; see "Skeletal round-trip" below.

Verified on 2026-11-22 with the export skill's reference-box round-trip (Blender 5.1.1, UE 5.8.3):

1. A 1 m cube on the ground (`SM_RefBox`, one mesh) with two markers: a bar out of the -Y face to
   y = -1.0 m (the preset's forward) and a shorter bar out of the +X face to x = +0.75 m.
2. Exported headless through `tools/blender/run.ps1` with the project preset
   (`.claude/skills/blender-to-unreal-export/export_unreal_fbx.py --kind static --all`).
3. Imported headless into a scratch project with the default UE 5.8 import (Interchange, FBX SDK
   translator, no option changes), placed in a scratch map at the origin with no rotation.
4. Read the static mesh bounds: min (-50, -50, 0) cm, max (75, 100, 100) cm, actor scale 1.

Result:

| Blender | Unreal |
|---|---|
| +X | +X |
| -Y (preset forward) | +Y |
| +Z | +Z |
| 1 m | 100 cm, scale 1 |

So Blender (x, y, z) m becomes Unreal (x, -y, z) x 100 cm. The UE5 mannequin mesh faces Unreal +Y,
which is Blender -Y. Build the base character facing Blender -Y, as the export skill says, with
its left side on Blender +X, and it faces the same way as the mannequin after import. As with the
mannequin, the character's mesh component needs -90 degrees yaw so it faces actor +X.

## Base character build

`build_base_character.py` builds the armature and the faceless mesh from scratch, checks them,
saves `SK_BaseCharacter.blend` and exports `SK_BaseCharacter.fbx` through the export skill's own
script. Build choices:

- Joint positions come from Drillis and Contini (1966) segment fractions of stature for a 180 cm
  adult (listed in the script). The local engine dump is only printed as a comparison: body
  joints land within about 3 cm of it; the hands sit about 10 cm further back because the
  forearm swings forward only 12 degrees.
- All 89 bones are built. `root`, `ik_foot_root`, `ik_hand_root`, `interaction` and
  `center_of_mass` sit at the origin pointing +Y with roll 0, so they import with no rotation.
  `ik_foot_*` and `ik_hand_*` copy the foot and hand bones. The ik and helper bones, and `root`,
  carry no weights.
- Mesh: skin modifier over a node graph, subdivided twice (about 17,000 triangles). Rest pose is
  the A-pose above, with a 12 degree elbow bend and a slight knee bend. The arm branches straight
  off the shoulder line (a separate trapezius node folded the hull into a groove that showed as
  a collar round the moving arm). Each hand has a palm, four fingers with a ring at every joint
  and a crease ring past the knuckle, and a thumb that is its own graph piece, starting buried
  in the palm's thickness so it has no end cap; an exact boolean union joins the thumbs, and
  the ball of the thumb is a smooth bulge of the palm surface. The body is one closed piece
  (checked: `mesh_islands` 1). Glutes are a smooth displacement of the back of the pelvis.
- Weights: bone heat on the main bones, then corrections (legs, crotch, a smooth upper-arm ramp
  across the shoulder joint with the rib-cage flank kept on the torso, one finger chain per
  finger vertex with linear blends only near the joints, a slow thumb_01 ramp, twist shares,
  4 influences). Left and right weights mirror within 0.05 except a few hand vertices.
- A failed check exits non-zero before anything is saved; the .blend and .fbx are written to
  temporary names and moved into place only when both succeed.
- The scene is built in metres and converted to a centimetre scene (Unit Scale 0.01, armature
  scaled 100 and applied) before it is saved and exported: the export skill's skeletal path.

## Realistic base character (decision 0022, 2026-11-27)

`build_human.py` builds the realistic base character. **It is not the game's character yet**: the committed `SK_BaseCharacter.blend` and `.fbx` still come from `build_base_character.py` until the fit below is finished, and then switch over in one tested commit. Build it to a scratch folder with:

    pwsh tools/blender/run.ps1 -UserAddons -Script Art/Characters/Base/build_human.py [-- --out-dir D] [--check-only]

- The body is generated with MPFB (the MakeHuman plugin, installed from extensions.blender.org;
  build 20261010 was used) from MakeHuman's CC0 core assets: an adult male, the athletic build the
  owner picked (gender 1, age 0.5, muscle 0.9, weight 0.6, proportions 1). The height macro is
  searched until the body is 180 cm (it settled at 0.5625).
- `-UserAddons` is needed because MPFB is a user extension; the export step still runs with
  factory settings.
- MPFB fits its rig in MakeHuman's own frame (hips near z = 0), so the human is generated without
  feet_on_ground and the baked mesh and the joints are moved up together; the build fails unless
  the pelvis then sits at hip height.
- MPFB's `game_engine` rig (53 bones, the UE4 layout) gives the joints and the weights. The UE5
  layout's extra bones are placed by rule: five spine bones evenly from spine_01 to the neck, two
  neck bones evenly from the neck to the head, metacarpals from a quarter of the way from wrist
  to knuckle, twist bones at a third and two thirds of their limb. Spine and neck weights are split
  onto the extra bones by height; metacarpal and twist bones carry none yet.
- Helper geometry (eyes, teeth, clothes helpers) is removed: the mesh is the body only, 26,756
  triangles. Eyes, teeth, brows and skin textures come next, through the asset import.
- Checked (build): 180.1 cm, feet on the ground, 89 bones in reference order, at most 4 influences, no
  unweighted vertex, one material, UVs. A posed render (arms overhead, a deep squat, a guard)
  showed no tearing.
- The ledger row is `makehuman-core-assets` (CC0-1.0). The MPFB FAQ: "There are in practice no
  restrictions on neither output nor bundled graphical assets".

What a trial import into the game showed on 2026-11-27, and must be fixed before the switch:

- Bone frames: `build_human.py` rolls every bone so its local Z axis matches the first scripted
  body (the reference rolls alone turned the frames on MPFB's joints). That fixed most of the
  ragdoll hinges; `calf_r` still bends backward in CombatSimulator.CS.Body.HingesBendTheRightWay.
- Strikes: the jab never extends (the left arm stays down) and the hand twists against the
  forearm into a ribbon. The twist bones carry no weight yet, and the strike's fist and hand
  targets are worth checking in the new frames.
- Reach: the realistic arm (upper arm plus forearm 48.1 cm, against 56.4 on the scripted body)
  lands about 49 cm from the chest. With the 35 cm capsule two fighters cannot stand close enough;
  a 28 cm capsule (the body is about 26 cm wide without the arms) and a step-in scaled by arm
  length got the straight and both hooks landing. PunchReach itself must stay 68: it is also the
  drive distance, and lowering it made the fist go less far.
- Motors: the chest strayed 6.5 cm (limit 5) stopping from a jog; NormalProfile at 1.6 times its
  strength brought it back within the limit.
- The `eyes` socket for this head: (0, 13, 169) cm (bridge of the nose 15.3 cm ahead of the head
  axis at 169 cm).
- Fixed on 2026-11-27 (second session), in `build_human.py` and the import:
  - MPFB rests with the elbows bent 43 degrees and the arms 48 degrees below horizontal; the game
    was built around 55 and 12. The arms are now posed to the scripted body's rest pose before
    the mesh is baked.
  - Every bone takes the scripted body's direction and roll (end bones its length too); MPFB's
    hands, fingers, feet and head pointed 13 to 31 degrees off.
  - Twist bones take a share of their limb's weight, as in `build_base_character.py`.
  - `import_base_character.py` now updates the Skeleton's reference pose on reimport (an
    Interchange pipeline with `update_skeleton_reference_pose`). Without it the Skeleton kept the
    first body's pose (forearm 31.1 cm against the new mesh's 24.2) and every retargeted clip
    played in the old frames.
- Still open: in every clip the hands curl into hooks (also with the physics layer hidden), while
  the hand deforms cleanly in Blender, so the fault is in the retarget, most likely the finger and
  hand chains on the new proportions. Strikes and the knee hinge were not re-tested after the
  fixes above.
- The asset ledger row (`makehuman-core-assets`, CC0-1.0) is added in the commit that makes the
  generated files the game's character.

`build_base_character.py` stays as the game's character until then, and afterwards for its helpers
(armature, centimetre scene, export) and the reference frames.

## Skeletal round-trip (verified 2026-11-22, Blender 5.1.1, UE 5.8.3)

Method: the committed FBX was imported headless into a throwaway scratch project with the default
UE 5.8 import (Interchange, automated `AssetImportTask`, no option changes), placed in a scratch
map at the origin with no rotation, and read back through the skeletal mesh component and the
skeleton's reference pose.

| Check | Result |
|---|---|
| Bone count | 89, names as in `skeleton_reference.json` |
| Height | mesh bounds z 0 to 180 cm |
| Root bone | at the origin, no rotation, scale 1 (local and component space) |
| Facing | Unreal +Y (toes and hands toward +Y), left side on +X |
| Actor scale | 1 |

Why the centimetre scene: the first export came from a metre scene, and its root bone imported at
scale 100 with every Apply Scalings option (Unreal folds the metre-to-centimetre conversion of the
`Armature` node into `root`); the export skill now exports skeletal meshes from a centimetre scene (decision 0012).
