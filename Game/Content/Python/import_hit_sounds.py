"""Import the hit sounds (decision 0025) from Art/ThirdParty/KenneyImpactSounds (CC0, Kenney).

Run headless:
    UnrealEditor-Cmd.exe Game/CombatSimulator.uproject -run=pythonscript -script=Game/Content/Python/import_hit_sounds.py
Writes SoundWaves into /Game/ThirdParty/KenneyImpactSounds; UCSHitFeedback plays them. Safe to rerun.
"""

import glob
import os

import unreal

DEST = "/Game/ThirdParty/KenneyImpactSounds"


def main():
    game_dir = os.path.abspath(unreal.Paths.convert_relative_path_to_full(unreal.Paths.project_dir()))
    folder = os.path.join(os.path.dirname(game_dir), "Art", "ThirdParty", "KenneyImpactSounds")
    files = sorted(glob.glob(os.path.join(folder, "*.wav")))
    if not files:
        raise RuntimeError("no .wav files in " + folder)
    tasks = []
    for path in files:
        with open(path, "rb") as f:
            if f.read(64).startswith(b"version https://git-lfs"):
                raise RuntimeError(path + " is a Git LFS pointer; run git lfs pull")
        task = unreal.AssetImportTask()
        task.filename = path
        task.destination_path = DEST
        task.automated = True
        task.replace_existing = True
        task.save = True
        tasks.append(task)
    unreal.AssetToolsHelpers.get_asset_tools().import_asset_tasks(tasks)
    count = 0
    for path in files:
        name = os.path.splitext(os.path.basename(path))[0]
        wave = unreal.load_asset(DEST + "/" + name)
        if not isinstance(wave, unreal.SoundWave):
            raise RuntimeError("no SoundWave for " + name)
        count += 1
    unreal.log("import_hit_sounds: %d sounds in %s" % (count, DEST))


main()
