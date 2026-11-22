"""Checks on Art/Characters/Base/skeleton_reference.json, the transcribed UE5 humanoid bone layout.

The JSON holds bone names, parents, side, group, an optional flag and roll notes. Joint positions
transcribed from Epic's mannequin are derived Epic data and must never be committed. It is written
by tools/ue/dump_skeleton.py in a throwaway scratch project and read by the base character builder.
"""

import json
import subprocess
from fnmatch import fnmatch
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
REFERENCE = ROOT / "Art" / "Characters" / "Base" / "skeleton_reference.json"

CORE = [
    "root", "pelvis",
    "spine_01", "spine_02", "spine_03", "spine_04", "spine_05",
    "neck_01", "neck_02", "head",
]
SIDED = ["clavicle", "upperarm", "lowerarm", "hand", "thigh", "calf", "foot", "ball"]
FINGERS = ["thumb", "index", "middle", "ring", "pinky"]


def expected_names():
    names = list(CORE)
    for side in ("l", "r"):
        names += [f"{b}_{side}" for b in SIDED]
        names += [f"{f}_{i:02d}_{side}" for f in FINGERS for i in (1, 2, 3)]
    return names


def load():
    return json.loads(REFERENCE.read_text(encoding="utf-8"))


def test_reference_shape():
    data = load()
    assert data["source"] == "UE5 humanoid (transcribed)"
    for b in data["bones"]:
        assert set(b) >= {"name", "parent", "roll_deg"}, b
        assert isinstance(b["roll_deg"], (int, float)), b["name"]


def _numbers(value):
    if isinstance(value, bool):
        return []
    if isinstance(value, (int, float)):
        return [value]
    if isinstance(value, list):
        return [n for v in value for n in _numbers(v)]
    if isinstance(value, dict):
        return [n for v in value.values() for n in _numbers(v)]
    return []


def test_reference_has_no_transform_values():
    """Only roll_deg may be numeric per bone; no positions, rotations or sizes anywhere."""
    data = load()
    for b in data["bones"]:
        extra = {k: v for k, v in b.items() if k != "roll_deg" and _numbers(v)}
        assert not extra, (b["name"], extra)
    top = {k: v for k, v in data.items() if k not in ("bones", "bone_count") and _numbers(v)}
    assert not top, top


def test_optional_bones_marked():
    bones = {b["name"]: b for b in load()["bones"]}
    for n, b in bones.items():
        want = n.startswith("ik_") or n in ("interaction", "center_of_mass")
        assert b.get("optional") is want, n


def test_reference_has_ue5_core_bones():
    names = {b["name"] for b in load()["bones"]}
    expected = expected_names()
    assert len(expected) == 10 + 2 * (8 + 15)
    missing = [n for n in expected if n not in names]
    assert not missing, missing


def test_parents_precede_children_and_root_unique():
    bones = load()["bones"]
    assert bones[0]["name"] == "root" and bones[0]["parent"] is None
    roots = [b["name"] for b in bones if b["parent"] is None]
    assert roots == ["root"], roots
    seen = set()
    for b in bones:
        assert b["name"] not in seen, f"duplicate bone {b['name']}"
        if b["parent"] is not None:
            assert b["parent"] in seen, f"{b['name']} listed before its parent {b['parent']}"
        seen.add(b["name"])


def test_no_epic_asset_paths_committed():
    files = subprocess.run(["git", "ls-files"], cwd=ROOT, capture_output=True, text=True,
                           check=True).stdout.splitlines()
    stray = [f for f in files if f.endswith(".uasset") and not f.startswith("Game/Content/")]
    assert not stray, stray
    epic = [f for f in files if f.startswith("Game/Content/")
            and (fnmatch(f.rsplit("/", 1)[-1], "*Manny*")
                 or fnmatch(f.rsplit("/", 1)[-1], "*Mannequin*"))]
    assert not epic, epic
