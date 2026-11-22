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

Scope: only a static mesh round-trip was verified. The skeletal export (armature orientation,
root bone, bone axes, scale on the root) has not been verified yet; Task 2 must check it on its
first export of the base character (skeleton imports at scale 1, root at the origin, character
faces Unreal +Y, 180 cm tall).

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
