---
name: blender-to-unreal-export
description: The one standard Blender-to-Unreal FBX export for Combat Simulator (static meshes, skeletal meshes, animations), with naming (SM_, SK_, A_, UCX_, SOCKET_), unit and transform checks, 4-bone skinning limit, and the import steps in UE 5.8. Use when exporting anything from Blender for Unreal, or when an import arrives 100x too big, rotated, or with a root bone at scale 100.
---

# Blender to Unreal export (standard preset)

There is one FBX preset for this project: `export_unreal_fbx.py` next to this file. Do not hand-tune export settings per asset. If the preset is wrong, fix it there and record the change as a decision (see the `decide` skill).

Status: the preset was ported from an earlier Unreal project, where it was verified on Blender 5.1.1 (all parameters exist, and a 0.5 x 0.5 x 1.8 m reference box round-trips at 1.8 m with scale 1). It has not yet been run in this repo. On the first real export, check that the box imports at 180 cm and that a mesh facing Blender -Y faces the same way as the UE mannequin, then note the result in the devlog.

Blender versions: 4.4 and 5.1 are installed here. The owner's Blender-Developer-Tools target 4.5 and 5.2 LTS (open decision OD-009). The preset was verified on 5.1 only; run `--check-only` once on 4.4 before relying on it there.

## The preset (summary)

| Setting | Value | Why |
|---|---|---|
| Forward / Up | `-Y` / `Z` | Blender's own axes, no axis conversion baked in |
| Scale | `global_scale=1.0`, `apply_unit_scale=True`, `apply_scale_options=FBX_SCALE_UNITS` | Scene in meters; the FBX carries the unit and Unreal converts to centimeters. Avoids the "root bone at scale 100" problem |
| Smoothing | `FACE` | Unreal warns about missing smoothing otherwise |
| Leaf bones | off | Otherwise Unreal gets extra `_end` bones |
| Deform bones only | on | Control bones stay in Blender |
| Textures | not embedded (`path_mode=STRIP`) | Textures are imported separately |

The Blender-Developer-Tools `engine-export-presets` skill uses `-Z`/`Y` with `global_scale=100`. This preset deliberately does not; keep one convention per project.

## Before export (in Blender)

1. Scene units: Metric, Unit Scale 1.0.
2. Apply rotation and scale on every mesh and armature (Ctrl+A, All Transforms).
3. Origin: static props at the base center (sits on the ground). Characters: armature origin at the feet.
4. Names:
   - Static mesh `SM_<Name>`. Collision: `UCX_SM_<Name>_00`, `_01` (convex pieces). Sockets: empties named `SOCKET_<Name>`.
   - Skeletal mesh `SK_<Name>`; armature named `Armature` consistently.
   - Animations: action names `A_<Character>_<Action>`, for example `A_Grunt_Swing`.
5. Skinning: max 4 bone influences per vertex (Weights, Limit Total, 4). Many characters can be on screen at once.
6. Characters that share animations share one skeleton; give other creature types their own.
7. Anything not made from scratch goes through the `asset-import` skill first (license check and ledger row).

## Export

```powershell
$blender = 'C:\Program Files\Blender Foundation\Blender 5.1\blender.exe'
& $blender -b Art\Props\Barrel.blend --python-exit-code 1 `
  --python .claude\skills\blender-to-unreal-export\export_unreal_fbx.py -- `
  --out Art\Export\SM_Barrel.fbx --kind static --all
```

`--kind static | skeletal | anim`, `--all` selects every mesh, armature and empty, `--check-only` validates without writing. The script exits non-zero on: wrong units, unapplied scale or rotation, wrong name prefix, more than 4 bone influences.

Source `.blend` files go under `Art/` and exports in `Art/Export/`; both are tracked through Git LFS (see `.gitattributes`).

## Import into Unreal 5.8 (manual until an Unreal MCP import toolset is set up)

1. Content Browser: open the target folder (`Content/Props/`, `Content/Characters/<Name>/`). Third-party packs go under `ThirdParty/` only (see `asset-import`).
2. Click Import, pick the FBX.
3. Static mesh dialog: turn Generate Missing Collision off if `UCX_` meshes exist. Leave Import Uniform Scale at 1.0.
4. Skeletal mesh dialog: pick the existing skeleton under Skeleton for characters that share it; otherwise leave empty to create one. Import Animations off for mesh files.
5. Animation FBX: choose the matching skeleton.
6. Click Import All, then File, Save All.
7. Check: drag into a level next to the mannequin; a 180 cm reference box should match his height.

## Symptoms

| Symptom | Cause |
|---|---|
| 100x too big or too small | Scene unit scale not 1.0, or someone baked `global_scale=100` |
| Root bone at scale 100 | Apply Scalings set to All / Local instead of FBX Units Scale |
| Mesh lying on its back | Axis settings changed from the preset |
| Extra `_end` bones | Leaf bones on |
| Smoothing group warning | Smoothing set to Normals Only |
