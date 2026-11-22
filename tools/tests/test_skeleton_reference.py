"""Checks on Art/Characters/Base/skeleton_reference.json, the transcribed UE5 humanoid bone layout,
and on tools/blender/run.ps1.

The JSON holds bone names, parents, side, group, an optional flag and roll notes. Joint positions
transcribed from Epic's mannequin are derived Epic data and must never be committed. It is written
by tools/ue/dump_skeleton.py in a throwaway scratch project and read by the base character builder.
"""

import json
import os
import shutil
import subprocess
from fnmatch import fnmatch
from pathlib import Path

import pytest

ROOT = Path(__file__).resolve().parents[2]
REFERENCE = ROOT / "Art" / "Characters" / "Base" / "skeleton_reference.json"
RUN_PS1 = ROOT / "tools" / "blender" / "run.ps1"

BONE_KEYS = {"name", "parent", "side", "group", "optional", "roll_deg"}
TOP_KEYS = {"source", "note", "bone_count", "bones"}

CORE = [
    "root", "pelvis",
    "spine_01", "spine_02", "spine_03", "spine_04", "spine_05",
    "neck_01", "neck_02", "head",
]
SIDED = ["clavicle", "upperarm", "lowerarm", "hand", "thigh", "calf", "foot", "ball"]
FINGERS = ["thumb", "index", "middle", "ring", "pinky"]
METACARPALS = ["index", "middle", "ring", "pinky"]  # the UE5 thumb has no metacarpal bone
TWISTS = ["upperarm", "lowerarm", "thigh", "calf"]
OPTIONAL = [
    "ik_foot_root", "ik_foot_l", "ik_foot_r",
    "ik_hand_root", "ik_hand_gun", "ik_hand_l", "ik_hand_r",
    "interaction", "center_of_mass",
]


def expected_names():
    """All 89 bones of the UE5 humanoid layout, by category."""
    names = list(CORE)
    for side in ("l", "r"):
        names += [f"{b}_{side}" for b in SIDED]
        names += [f"{f}_{i:02d}_{side}" for f in FINGERS for i in (1, 2, 3)]
        names += [f"{f}_metacarpal_{side}" for f in METACARPALS]
        names += [f"{t}_twist_{i:02d}_{side}" for t in TWISTS for i in (1, 2)]
    names += OPTIONAL
    return names


def load():
    return json.loads(REFERENCE.read_text(encoding="utf-8"))


def tracked_files():
    return subprocess.run(["git", "ls-files"], cwd=ROOT, capture_output=True, text=True,
                          check=True).stdout.splitlines()


def test_reference_shape():
    data = load()
    assert data["source"] == "UE5 humanoid (transcribed)"
    for b in data["bones"]:
        assert set(b) >= {"name", "parent", "roll_deg"}, b
        assert isinstance(b["roll_deg"], (int, float)), b["name"]


def test_reference_keys_are_allowed_only():
    data = load()
    assert set(data) <= TOP_KEYS, set(data) - TOP_KEYS
    for b in data["bones"]:
        assert set(b) <= BONE_KEYS, (b["name"], set(b) - BONE_KEYS)


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


def test_reference_lists_exactly_the_89_bones():
    data = load()
    expected = expected_names()
    assert len(expected) == len(set(expected)) == 89
    names = [b["name"] for b in data["bones"]]
    assert len(names) == data["bone_count"] == 89
    assert sorted(names) == sorted(expected), (
        sorted(set(expected) - set(names)), sorted(set(names) - set(expected)))


def test_optional_bones_marked():
    flagged = sorted(b["name"] for b in load()["bones"] if b["optional"] is True)
    assert flagged == sorted(OPTIONAL)
    for b in load()["bones"]:
        assert b["optional"] in (True, False), b["name"]


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
    files = tracked_files()
    stray = [f for f in files if f.endswith(".uasset") and not f.startswith("Game/Content/")]
    assert not stray, stray
    epic_names = ("*manny*", "*mannequin*", "*quinn*")
    epic = [f for f in files
            if (f.startswith("Game/Content/") or f.lower().endswith((".fbx", ".uasset")))
            and any(fnmatch(f.rsplit("/", 1)[-1].lower(), p) for p in epic_names)]
    assert not epic, epic


def test_no_local_position_dump_committed():
    leaked = [f for f in tracked_files()
              if fnmatch(f.rsplit("/", 1)[-1], "skeleton_positions*")
              or f.endswith("_local.json")]
    assert not leaked, leaked


# --- tools/blender/run.ps1, with a stand-in for Blender that echoes its arguments ---

PWSH = shutil.which("pwsh")


@pytest.fixture
def fake_blender(tmp_path):
    fake = tmp_path / "fake_blender.cmd"
    fake.write_text("@echo off\r\necho ARGS %*\r\nexit /b 7\r\n", encoding="ascii")
    script = tmp_path / "noop.py"
    script.write_text("pass\n", encoding="ascii")
    env = dict(os.environ, BLENDER_PATH=str(fake))
    return script, env


def _args_line(out):
    lines = [l for l in out.splitlines() if l.startswith("ARGS ")]
    assert lines, out
    return lines[-1]


@pytest.mark.skipif(os.name != "nt" or PWSH is None, reason="needs Windows and pwsh")
def test_run_ps1_file_invocation_passes_script_args_and_exit_code(fake_blender):
    script, env = fake_blender
    r = subprocess.run([PWSH, "-NoProfile", "-File", str(RUN_PS1), "-Script", str(script),
                        "--", "--out", "x.fbx"], capture_output=True, text=True, env=env)
    assert r.returncode == 7, r.stdout + r.stderr
    line = _args_line(r.stdout)
    assert "-b --factory-startup --python-exit-code 1 --python" in line
    assert line.endswith("-- --out x.fbx"), line


@pytest.mark.skipif(os.name != "nt" or PWSH is None, reason="needs Windows and pwsh")
def test_run_ps1_in_process_invocation(fake_blender):
    script, env = fake_blender
    cmd = f"& '{RUN_PS1}' -Script '{script}' -- --out x.fbx; exit $LASTEXITCODE"
    r = subprocess.run([PWSH, "-NoProfile", "-Command", cmd], capture_output=True, text=True, env=env)
    assert r.returncode == 7, r.stdout + r.stderr
    assert _args_line(r.stdout).endswith("-- --out x.fbx")


@pytest.mark.skipif(os.name != "nt" or PWSH is None, reason="needs Windows and pwsh")
def test_run_ps1_missing_script_fails(fake_blender, tmp_path):
    _, env = fake_blender
    r = subprocess.run([PWSH, "-NoProfile", "-File", str(RUN_PS1), "-Script",
                        str(tmp_path / "missing.py")], capture_output=True, text=True, env=env)
    assert r.returncode == 1
    assert "ARGS" not in r.stdout
