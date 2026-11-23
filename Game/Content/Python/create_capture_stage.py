"""Creates (or rebuilds) /Game/Maps/L_CaptureStage: the neutral stage used for captures.

Contents: a 100 m floor plane, sky (atmosphere, sky light, height fog), a directional sun,
a 1 m reference cube, a 10 m scale ruler with a label every metre, and a PlayerStart where the
character cameras in docs/captures/cameras.json expect the character: feet at (300, 300, 0),
facing -X (yaw 180). The game mode (ACSGameMode) spawns the player's ACSCharacter there.

Safe to rerun: the map is emptied of every actor and rebuilt each time.

Run headless (editor closed):
    UnrealEditor-Cmd.exe <repo>/Game/CombatSimulator.uproject -run=pythonscript
        -script="<repo>/Game/Content/Python/create_capture_stage.py" -unattended -nosplash -nullrhi
or from a running editor through the Unreal MCP server (call_tool, Python execution).
"""

import unreal

MAP_PATH = "/Game/Maps/L_CaptureStage"
PLANE = "/Engine/BasicShapes/Plane.Plane"  # 100 cm x 100 cm
CUBE = "/Engine/BasicShapes/Cube.Cube"  # 100 cm on each side, pivot at the centre
FLOOR_SIZE_M = 100
RULER_LENGTH_M = 10
SKY_INTENSITY = 2.0  # sky fill so shadows read as grey shade (see build)


def log(msg):
    unreal.log("create_capture_stage: " + msg)


def spawn(actor_class, label, location, rotation=None):
    actors = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
    actor = actors.spawn_actor_from_class(actor_class, location, rotation or unreal.Rotator())
    if actor is None:
        raise RuntimeError("Could not spawn " + label)
    actor.set_actor_label(label)
    actor.set_folder_path("CaptureStage")
    return actor


def spawn_mesh(mesh_path, label, location, scale):
    mesh = unreal.load_asset(mesh_path)
    if mesh is None:
        raise RuntimeError("Missing engine mesh " + mesh_path)
    actor = spawn(unreal.StaticMeshActor, label, location)
    comp = actor.static_mesh_component
    comp.set_static_mesh(mesh)
    actor.set_actor_scale3d(scale)
    return actor


def spawn_text(label, text, location, size=20.0):
    actor = spawn(unreal.TextRenderActor, label, location, unreal.Rotator(roll=0, pitch=0, yaw=-90))
    comp = actor.text_render
    comp.set_text(text)
    comp.set_editor_property("world_size", size)
    comp.set_editor_property("horizontal_alignment", unreal.HorizTextAligment.EHTA_CENTER)
    return actor


def build():
    levels = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
    assets = unreal.EditorAssetLibrary

    if assets.does_asset_exist(MAP_PATH):
        # Rebuild from scratch so reruns never stack duplicate actors. Deleting the map in the
        # commandlet leaves it registered and new_level then refuses the path, so the existing map
        # is loaded and emptied of every actor instead: the same result as a new level.
        if not levels.load_level(MAP_PATH):
            raise RuntimeError("Could not load " + MAP_PATH)
        actors = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
        doomed = [a for a in actors.get_all_level_actors() if not isinstance(a, unreal.WorldSettings)]
        if doomed and not actors.destroy_actors(doomed):
            raise RuntimeError("Could not empty " + MAP_PATH)
    elif not levels.new_level(MAP_PATH):
        raise RuntimeError("Could not create " + MAP_PATH)

    # Floor: the plane mesh is 1 m square, so the scale is the size in metres.
    spawn_mesh(PLANE, "Floor", unreal.Vector(0, 0, 0), unreal.Vector(FLOOR_SIZE_M, FLOOR_SIZE_M, 1))

    # Sky: physical atmosphere lit by the sun, a real-time captured sky light, and light fog.
    spawn(unreal.SkyAtmosphere, "SkyAtmosphere", unreal.Vector(0, 0, 0))
    sky = spawn(unreal.SkyLight, "SkyLight", unreal.Vector(0, 0, 300))
    sky.light_component.set_editor_property("real_time_capture", True)
    sky.light_component.set_mobility(unreal.ComponentMobility.MOVABLE)
    # Sky fill at SKY_INTENSITY: at the engine default (1) shadows lit only by the sky read as saturated
    # navy, and the owner's head shadow on the chest in first person looked like a hole.
    sky.light_component.set_editor_property("intensity", SKY_INTENSITY)
    spawn(unreal.ExponentialHeightFog, "HeightFog", unreal.Vector(0, 0, 0))

    sun = spawn(unreal.DirectionalLight, "Sun", unreal.Vector(0, 0, 500), unreal.Rotator(roll=0, pitch=-45, yaw=30))
    sun.light_component.set_editor_property("atmosphere_sun_light", True)
    sun.light_component.set_mobility(unreal.ComponentMobility.MOVABLE)
    sun.light_component.set_editor_property("intensity", 10.0)

    # Reference cube: exactly 1 m, resting on the floor at the origin.
    spawn_mesh(CUBE, "RefCube_1m", unreal.Vector(0, 0, 50), unreal.Vector(1, 1, 1))
    spawn_text("RefCube_Label", "1 m cube", unreal.Vector(0, -60, 120))

    # Scale ruler along +X, 2 m in front of the cube: one 95 cm bar per metre (5 cm gaps keep the
    # metres countable), each 10 cm wide and 2 cm high, plus a label at every metre mark.
    ruler_y = -200.0
    for i in range(RULER_LENGTH_M):
        centre_x = i * 100.0 + 50.0
        spawn_mesh(CUBE, "Ruler_Bar_%02d" % i, unreal.Vector(centre_x, ruler_y, 1.0), unreal.Vector(0.95, 0.10, 0.02))
    for i in range(RULER_LENGTH_M + 1):
        spawn_text("Ruler_Label_%02d" % i, "%d m" % i, unreal.Vector(i * 100.0, ruler_y - 30.0, 5.0))

    # Character placement for the char-* and fp-* cameras. The PlayerStart capsule is 92 cm half
    # height, so at z 92 its bottom (and the spawned character's feet) is on the floor.
    spawn(unreal.PlayerStart, "PlayerStart", unreal.Vector(300, 300, 92), unreal.Rotator(roll=0, pitch=0, yaw=180))

    if not levels.save_current_level():
        raise RuntimeError("Could not save " + MAP_PATH)
    log("saved " + MAP_PATH)


build()
