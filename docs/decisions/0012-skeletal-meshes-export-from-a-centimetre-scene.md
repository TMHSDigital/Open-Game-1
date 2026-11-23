# 0012: Skeletal meshes export from a centimetre scene

Status: Accepted
Date: 2026-11-22
Issue: none
Supersedes: none

## Context

The project has one Blender-to-Unreal FBX preset (`.claude/skills/blender-to-unreal-export/`). It assumed a metre scene (Unit Scale 1.0) for everything, and a static reference box imported into UE 5.8 at the right size with scale 1.

The first skeletal export, the base character `SK_BaseCharacter`, came out of the same metre scene and failed its round-trip. The mesh imported at 180 cm, but the root bone imported at scale 100, and every bone inherited that scale. Unreal applies the metre-to-centimetre conversion to the top `Armature` node and folds it into the root bone. A root at scale 100 breaks physics asset bodies, sockets and retargeting.

## Decision

Skeletal meshes are exported from a centimetre scene: Unit Scale 0.01, with the armature scaled 100 and the scale applied. Static meshes and animations stay in a metre scene (Unit Scale 1.0).

The preset's unit check enforces the scene scale per `--kind`, and the export skill documents both paths. Animations have not been verified against a centimetre skeleton. Check them on the first animation export, and extend this decision if they also need the centimetre scene.

This was decided during implementation of the base character, as a technical convention of the export pipeline. The owner may revisit it.

## Consequences

- From a centimetre scene the base character imports with root scale 1, pelvis at 97.6 cm, a mesh 180 cm tall, and sane physics asset bounds (verified headless in UE 5.8.3).
- Skeletal source files can be built in metres for convenience, but they must be converted before saving the export .blend. `Art/Characters/Base/build_base_character.py` shows how.
- The preset now rejects skeletal exports from a metre scene and static exports from a centimetre scene. `tools/tests/test_export_preset_units.py` covers both.
- There are now two scene scales to keep in mind. Picking the wrong one fails the export check instead of producing a bad import.

## Alternatives

Exporting from the metre scene and changing only the exporter options. Each variant was imported with the default UE 5.8 import:

- Apply Scalings `FBX_SCALE_UNITS` (the old preset): root scale 100.
- Apply Scalings `FBX_SCALE_ALL`: root scale 100.
- Apply Scalings `FBX_SCALE_NONE`: root scale 100.
- `apply_unit_scale` off: root scale 100.
- `global_scale` 0.01: root scale 1, but the whole character arrived 100 times too small.

An armature object kept at scale 0.01 over centimetre data would also import at scale 1. It was rejected because it leaves an unapplied object scale, which the preset rejects for good reason.

## Links

- `.claude/skills/blender-to-unreal-export/SKILL.md`
- `Art/Characters/Base/NOTES.md` (round-trip results)
