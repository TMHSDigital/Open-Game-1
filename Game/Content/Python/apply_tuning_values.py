"""Writes tuning values found in a running build into DA_MovementTuning.

The owner changes movement values on the Tuning tab of the pause screen and presses "Copy values".
Save that text to a file (default: <repo>/_scratch/tuning-values.json, which is gitignored) and run
this script: the values become the asset's values, so the next build is packaged with them and the
owner's saved changes then count as "packaged".

The text is the shape of the game's overrides file:
    {"version": 1, "build": "0.1.4", "values": {"JogSpeed": 450, "HitProfile.PositionStrength": 80}}

The values pass the same checks as in the game (UCSTuningBaker.ApplyValues): every name must be a
row of the range table with a value in range, and the result must be a valid tuning. If anything is
refused, nothing is written.

Run headless (editor closed):
    UnrealEditor-Cmd.exe <repo>/Game/CombatSimulator.uproject -run=pythonscript
        -script="<repo>/Game/Content/Python/apply_tuning_values.py" -unattended -nosplash -nullrhi
Set CS_TUNING_VALUES to use another file.
"""

import os

import unreal

TUNING = "/Game/Characters/Base/DA_MovementTuning"


def log(msg):
    unreal.log("apply_tuning_values: " + msg)


def fail(msg):
    unreal.log_error("apply_tuning_values: " + msg)
    raise RuntimeError(msg)


def values_path():
    path = os.environ.get("CS_TUNING_VALUES")
    if path:
        return path
    project = unreal.Paths.convert_relative_path_to_full(unreal.Paths.project_dir())
    return os.path.normpath(os.path.join(project, "..", "_scratch", "tuning-values.json"))


def main():
    path = values_path()
    if not os.path.isfile(path):
        fail("no values file at %s (paste the Copy values text there, or set CS_TUNING_VALUES)" % path)
    with open(path, encoding="utf-8-sig") as handle:
        text = handle.read()
    tuning = unreal.load_asset(TUNING)
    if tuning is None:
        fail("missing " + TUNING)
    result = unreal.CSTuningBaker.apply_values(tuning, text)
    if not result.success:
        fail("%s refused: %s. Nothing was written." % (path, result.message))
    for line in result.message.splitlines():
        log(line)
    if not unreal.EditorAssetLibrary.save_asset(TUNING, only_if_is_dirty=False):
        fail("could not save " + TUNING)
    log("saved " + TUNING)
    log("done")


main()
