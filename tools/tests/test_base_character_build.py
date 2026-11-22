"""Builds the faceless base character with Art/Characters/Base/build_base_character.py (headless
Blender through tools/blender/run.ps1) into a temp dir and checks every value in checks.json
against the base model constraints: 180 cm (within 1 cm), 15,000 to 25,000 triangles, at most
4 bone influences, no unweighted vertices, the full 89-bone UE5 humanoid name list, one material
slot, UVs present, one closed mesh.

Skips when Blender or pwsh is not available (for example CI on Linux).
"""

import json
import os
import shutil
import subprocess
from pathlib import Path

import pytest

ROOT = Path(__file__).resolve().parents[2]
SCRIPT = ROOT / "Art" / "Characters" / "Base" / "build_base_character.py"
RUN_PS1 = ROOT / "tools" / "blender" / "run.ps1"
BLENDER = Path(os.environ.get(
    "BLENDER_PATH", r"C:\Program Files\Blender Foundation\Blender 5.1\blender.exe"))
PWSH = shutil.which("pwsh")

needs_blender = pytest.mark.skipif(
    os.name != "nt" or PWSH is None or not BLENDER.exists(),
    reason="needs Windows, pwsh and Blender 5.1 (set BLENDER_PATH); skipped on CI without Blender")


@pytest.fixture(scope="module")
def build(tmp_path_factory):
    out = tmp_path_factory.mktemp("base_character")
    r = subprocess.run([PWSH, "-NoProfile", "-File", str(RUN_PS1), "-Script", str(SCRIPT),
                        "--", "--out-dir", str(out)],
                       capture_output=True, text=True, timeout=900)
    return out, r


@needs_blender
def test_build_passes_all_checks(build):
    out, r = build
    log = (r.stdout + r.stderr)[-4000:]
    checks_file = out / "checks.json"
    assert checks_file.exists(), log
    checks = json.loads(checks_file.read_text(encoding="utf-8"))
    assert 179.0 <= checks["height_cm"] <= 181.0, checks
    assert 15000 <= checks["tris"] <= 25000, checks
    assert checks["max_influences"] <= 4, checks
    assert checks["unweighted_verts"] == 0, checks
    assert checks["bone_names_match"] is True, checks
    assert checks["material_slots"] == 1, checks
    assert checks["has_uvs"] is True, checks
    assert checks["mesh_islands"] == 1, checks  # one closed body, no floating fingers
    assert r.returncode == 0, log


@needs_blender
def test_build_writes_outputs(build):
    out, r = build
    for name in ("SK_BaseCharacter.blend", "SK_BaseCharacter.fbx",
                 "turn_front.png", "turn_side.png", "turn_back.png"):
        assert (out / name).exists(), name
