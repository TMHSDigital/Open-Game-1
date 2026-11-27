"""The export preset's scene unit check (.claude/skills/blender-to-unreal-export/export_unreal_fbx.py):
static meshes come from a metre scene (Unit Scale 1.0), skeletal meshes from a centimetre scene
(Unit Scale 0.01), because from a metre scene UE 5.8 imports the root bone at scale 100.

Runs headless Blender through tools/blender/run.ps1 with --check-only; skips without Blender.
"""

import os
import shutil
import subprocess
from pathlib import Path

import pytest

ROOT = Path(__file__).resolve().parents[2]
RUN_PS1 = ROOT / "tools" / "blender" / "run.ps1"
PRESET = ROOT / ".claude" / "skills" / "blender-to-unreal-export" / "export_unreal_fbx.py"
BLENDER = Path(os.environ.get(
    "BLENDER_PATH", r"C:\Program Files\Blender Foundation\Blender 5.1\blender.exe"))
PWSH = shutil.which("pwsh")

pytestmark = [pytest.mark.slow, pytest.mark.skipif(
    os.name != "nt" or PWSH is None or not BLENDER.exists(),
    reason="needs Windows, pwsh and Blender 5.1 (set BLENDER_PATH); skipped on CI without Blender")]

MAKE_SCENE = '''
import sys, bpy
argv = sys.argv[sys.argv.index("--") + 1:]
out, unit_scale, mesh_name = argv[0], float(argv[1]), argv[2]
for o in list(bpy.data.objects):
    bpy.data.objects.remove(o, do_unlink=True)
bpy.context.scene.unit_settings.system = "METRIC"
bpy.context.scene.unit_settings.scale_length = unit_scale
me = bpy.data.meshes.new(mesh_name)
me.from_pydata([(0, 0, 0), (1, 0, 0), (0, 1, 0)], [], [(0, 1, 2)])
mesh = bpy.data.objects.new(mesh_name, me)
bpy.context.scene.collection.objects.link(mesh)
arm = bpy.data.objects.new("Armature", bpy.data.armatures.new("Armature"))
bpy.context.scene.collection.objects.link(arm)
bpy.context.view_layer.objects.active = arm
bpy.ops.object.mode_set(mode="EDIT")
b = arm.data.edit_bones.new("root")
b.head, b.tail = (0, 0, 0), (0, 1, 0)
bpy.ops.object.mode_set(mode="OBJECT")
mesh.parent = arm
bpy.ops.wm.save_as_mainfile(filepath=out)
'''


def _run(args):
    return subprocess.run([PWSH, "-NoProfile", "-File", str(RUN_PS1)] + args,
                          capture_output=True, text=True, timeout=300)


@pytest.fixture(scope="module")
def scenes(tmp_path_factory):
    d = tmp_path_factory.mktemp("units")
    maker = d / "make_scene.py"
    maker.write_text(MAKE_SCENE, encoding="utf-8")
    out = {}
    for unit, label in ((1.0, "m"), (0.01, "cm")):
        for prefix in ("SK", "SM"):
            blend = d / f"{prefix}_{label}.blend"
            r = _run(["-Script", str(maker), "--", str(blend), str(unit), f"{prefix}_Test"])
            assert r.returncode == 0 and blend.exists(), r.stdout + r.stderr
            out[(prefix, label)] = blend
    return d, out


def _check(d, blend, kind):
    return _run(["-Script", str(PRESET), "-Blend", str(blend), "--",
                 "--out", str(d / "x.fbx"), "--kind", kind, "--all", "--check-only"])


@pytest.mark.parametrize("prefix,unit,kind,ok", [
    ("SK", "cm", "skeletal", True),
    ("SK", "m", "skeletal", False),
    ("SM", "m", "static", True),
    ("SM", "cm", "static", False),
])
def test_unit_scale_per_kind(scenes, prefix, unit, kind, ok):
    d, blends = scenes
    r = _check(d, blends[(prefix, unit)], kind)
    log = r.stdout + r.stderr
    if ok:
        assert r.returncode == 0 and "EXPORT CHECK OK" in log, log[-2000:]
    else:
        assert r.returncode != 0 and "Unit Scale" in log, log[-2000:]
