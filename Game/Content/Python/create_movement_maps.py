"""Creates (or rebuilds) the movement maps and their grid material.

- /Game/Maps/L_Plane: the start map (GameDefaultMap). A 1 km square flat plane with a subtle
  neutral grid (1 m minor lines, 10 m major lines), sky, sun and a PlayerStart at the origin.
- /Game/Maps/L_MovementGym: the same plane and sky with a measured course for tuning and captures.
  Every size below is in centimetres; +X is "forward" from the PlayerStart.
- /Game/Maps/Materials/M_Grid: world-aligned grid material (BaseColor and LineColor parameters),
  with instances MI_Grid_Obstacle (lighter, for gym geometry) and MI_Grid_Marker (near white, for
  lane markers).

Gym layout (feet of every feature at x = 0 unless noted, rows side by side along +Y):
  - PlayerStart at (-300, 0), facing +X.
  - Sprint lane: y -150..150, x 0..3000 (30 m), a marker bar across it every 1 m (thicker every
    5 m) with a label beside each metre. Markers have no collision.
  - Ramps (tags Ramp15, Ramp30, Ramp44, Ramp50): 400 wide at y 700, 1200, 1700, 2200, each rising
    to 200 cm onto a 300 cm platform. Walkable slope is 44 degrees (DA_MovementTuning). Ramp44 is
    built at 43.9 degrees and labelled 44: at exactly 44.0000 it passes or fails on float rounding,
    so it keeps 0.1 degree of margin. It is the steepest walkable ramp; 50 is not walkable.
  - Stairs (tags Stairs15, Stairs25, Stairs40): 300 wide at y 2900, 3400, 3900, 35 cm treads,
    step heights 15, 25 and 40 cm (8, 5 and 3 steps, about 120 cm up) onto a 200 cm landing.
    The movement component's MaxStepHeight is 45 cm.
  - Gaps (tag Gaps): at y 4600, a 20 degree ramp up to a row of 100 cm high, 300 cm long blocks
    separated by gaps of 50, 100, 150, 200, 250 and 300 cm.
  - Ledges (tags Ledge100 ... Ledge600): 400 wide at y 5400, 6000, 6600, 7200, 7800, a 30 degree
    ramp up to a 300 cm platform 100, 200, 300, 400 and 600 cm high with open floor beyond. A drop
    from 600 cm lands at about 1080 cm/s, above RagdollLandingSpeedThreshold (1000).
  - Physics boxes (tag PhysicsBox): at x 600..1200, y -775..-475: 50, 75 and 100 cm cubes of 10,
    20 and 40 kg, and a stack of three 50 cm cubes.

Ramps are pitched engine cubes whose top face starts at the floor: the test
CombatSimulator.CS.Maps.GymSlopes reads the ramp angle and ends from that (local (-50, 0, 50) to
(50, 0, 50)). Keep that shape if the ramps change.

Only engine basic content (/Engine/BasicShapes) and assets made here are used.

Safe to rerun: the materials are deleted and made again, and each map is emptied of every actor and
refilled (a map deleted in the commandlet stays registered, so it is emptied in place instead).

Run headless (editor closed):
    UnrealEditor-Cmd.exe <repo>/Game/CombatSimulator.uproject -run=pythonscript
        -script="<repo>/Game/Content/Python/create_movement_maps.py" -unattended -nosplash -nullrhi
"""

import math

import unreal

PLANE_MAP = "/Game/Maps/L_Plane"
GYM_MAP = "/Game/Maps/L_MovementGym"
MATERIAL_DIR = "/Game/Maps/Materials"
GRID = MATERIAL_DIR + "/M_Grid"
GRID_OBSTACLE = MATERIAL_DIR + "/MI_Grid_Obstacle"
GRID_MARKER = MATERIAL_DIR + "/MI_Grid_Marker"
CUBE = "/Engine/BasicShapes/Cube.Cube"  # 100 cm on each side, pivot at the centre
DUMMY_CLASS = "/Script/CombatSimulator.CSTargetDummy"
LAUNCHER_CLASS = "/Script/CSCharacter.CSBallLauncher"
LAUNCHER_AT = (1300.0, -1500.0)

PLANE_SIZE_CM = 100000.0  # 1 km square
FLOOR_COLOR = (0.22, 0.22, 0.22)
LINE_COLOR = (0.12, 0.12, 0.12)
OBSTACLE_COLOR = (0.45, 0.44, 0.42)
MARKER_COLOR = (0.8, 0.8, 0.8)

SKY_INTENSITY = 2.0

RAMP_RISE = 200.0
RAMP_WIDTH = 400.0
PLATFORM_LENGTH = 300.0


def log(msg):
    unreal.log("create_movement_maps: " + msg)


# ----- Material -------------------------------------------------------------------------------

def _expr(material, cls, x, y):
    return unreal.MaterialEditingLibrary.create_material_expression(material, cls, x, y)


def _connect(a, b, b_input, a_output=""):
    if not unreal.MaterialEditingLibrary.connect_material_expressions(a, a_output, b, b_input):
        raise RuntimeError("Could not connect %s to %s.%s" % (a.get_name(), b.get_name(), b_input))


def _grid_lines(material, xy, cell_cm, half_width_cm, y):
    """0..1 mask that is 1 within half_width_cm of a line every cell_cm (world X and Y)."""
    div = _expr(material, unreal.MaterialExpressionDivide, -1200, y)
    div.set_editor_property("const_b", cell_cm)
    _connect(xy, div, "A")
    frac = _expr(material, unreal.MaterialExpressionFrac, -1050, y)
    _connect(div, frac, "")
    centred = _expr(material, unreal.MaterialExpressionSubtract, -900, y)
    centred.set_editor_property("const_b", 0.5)
    _connect(frac, centred, "A")
    dist = _expr(material, unreal.MaterialExpressionAbs, -750, y)
    _connect(centred, dist, "")
    # abs(frac - 0.5) is 0.5 on a line. Per axis: 1 within half_width_cm of a line, else 0.
    edge = _expr(material, unreal.MaterialExpressionSubtract, -600, y)
    edge.set_editor_property("const_b", 0.5 - half_width_cm / cell_cm)
    _connect(dist, edge, "A")
    sharp = _expr(material, unreal.MaterialExpressionMultiply, -450, y)
    sharp.set_editor_property("const_b", 1.0e5)
    _connect(edge, sharp, "A")
    per_axis = _expr(material, unreal.MaterialExpressionSaturate, -300, y)
    _connect(sharp, per_axis, "")
    # Either axis: dot with (1, 1), clamped.
    ones = _expr(material, unreal.MaterialExpressionConstant2Vector, -300, y + 80)
    ones.set_editor_property("r", 1.0)
    ones.set_editor_property("g", 1.0)
    either = _expr(material, unreal.MaterialExpressionDotProduct, -200, y)
    _connect(per_axis, either, "A")
    _connect(ones, either, "B")
    mask = _expr(material, unreal.MaterialExpressionSaturate, -100, y)
    _connect(either, mask, "")
    return mask


def build_grid_material():
    tools = unreal.AssetToolsHelpers.get_asset_tools()
    material = tools.create_asset("M_Grid", MATERIAL_DIR, unreal.Material, unreal.MaterialFactoryNew())
    if material is None:
        raise RuntimeError("Could not create " + GRID)

    world = _expr(material, unreal.MaterialExpressionWorldPosition, -1500, 0)
    xy = _expr(material, unreal.MaterialExpressionComponentMask, -1350, 0)
    xy.set_editor_property("r", True)
    xy.set_editor_property("g", True)
    _connect(world, xy, "")

    minor = _grid_lines(material, xy, 100.0, 1.0, -200)    # 1 m lines, 2 cm wide
    major = _grid_lines(material, xy, 1000.0, 2.5, 200)    # 10 m lines, 5 cm wide
    minor_soft = _expr(material, unreal.MaterialExpressionMultiply, 0, -200)
    minor_soft.set_editor_property("const_b", 0.6)
    _connect(minor, minor_soft, "A")
    lines = _expr(material, unreal.MaterialExpressionMax, 100, 0)
    _connect(minor_soft, lines, "A")
    _connect(major, lines, "B")

    base = _expr(material, unreal.MaterialExpressionVectorParameter, 100, -300)
    base.set_editor_property("parameter_name", "BaseColor")
    base.set_editor_property("default_value", unreal.LinearColor(*FLOOR_COLOR, 1.0))
    line = _expr(material, unreal.MaterialExpressionVectorParameter, 100, -150)
    line.set_editor_property("parameter_name", "LineColor")
    line.set_editor_property("default_value", unreal.LinearColor(*LINE_COLOR, 1.0))
    color = _expr(material, unreal.MaterialExpressionLinearInterpolate, 300, -100)
    _connect(base, color, "A")
    _connect(line, color, "B")
    _connect(lines, color, "Alpha")
    roughness = _expr(material, unreal.MaterialExpressionConstant, 300, 100)
    roughness.set_editor_property("r", 0.85)

    lib = unreal.MaterialEditingLibrary
    lib.connect_material_property(color, "", unreal.MaterialProperty.MP_BASE_COLOR)
    lib.connect_material_property(roughness, "", unreal.MaterialProperty.MP_ROUGHNESS)
    lib.recompile_material(material)
    unreal.EditorAssetLibrary.save_loaded_asset(material)
    log("saved " + GRID)
    return material


def build_instance(name, parent, base_color):
    tools = unreal.AssetToolsHelpers.get_asset_tools()
    factory = unreal.MaterialInstanceConstantFactoryNew()
    instance = tools.create_asset(name, MATERIAL_DIR, unreal.MaterialInstanceConstant, factory)
    if instance is None:
        raise RuntimeError("Could not create " + name)
    lib = unreal.MaterialEditingLibrary
    lib.set_material_instance_parent(instance, parent)
    lib.set_material_instance_vector_parameter_value(instance, "BaseColor", unreal.LinearColor(*base_color, 1.0))
    lib.update_material_instance(instance)
    unreal.EditorAssetLibrary.save_loaded_asset(instance)
    log("saved %s/%s" % (MATERIAL_DIR, name))
    return instance


# ----- Actors ---------------------------------------------------------------------------------

def spawn(actor_class, label, location, rotation=None, folder="Map"):
    actors = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
    actor = actors.spawn_actor_from_class(actor_class, location, rotation or unreal.Rotator())
    if actor is None:
        raise RuntimeError("Could not spawn " + label)
    actor.set_actor_label(label)
    actor.set_folder_path(folder)
    return actor


def spawn_box(label, center, size, material, rotation=None, folder="Gym", tags=(), collision=True):
    """An engine cube scaled to size (x, y, z cm) at center."""
    actor = spawn(unreal.StaticMeshActor, label, unreal.Vector(*center), rotation, folder)
    comp = actor.static_mesh_component
    comp.set_static_mesh(unreal.load_asset(CUBE))
    comp.set_material(0, material)
    actor.set_actor_scale3d(unreal.Vector(size[0] / 100.0, size[1] / 100.0, size[2] / 100.0))
    if not collision:
        comp.set_collision_profile_name("NoCollision")
        comp.set_editor_property("cast_shadow", False)
    if tags:
        actor.set_editor_property("tags", [unreal.Name(t) for t in tags])
    return actor


def spawn_text(label, text, location, yaw=180.0, size=30.0, folder="Gym/Labels"):
    """Upright label; yaw 180 reads from the PlayerStart side (looking +X)."""
    actor = spawn(unreal.TextRenderActor, label, unreal.Vector(*location), unreal.Rotator(roll=0, pitch=0, yaw=yaw), folder)
    comp = actor.text_render
    comp.set_text(text)
    comp.set_editor_property("world_size", size)
    comp.set_editor_property("horizontal_alignment", unreal.HorizTextAligment.EHTA_CENTER)
    comp.set_editor_property("text_render_color", unreal.Color(r=20, g=20, b=20, a=255))
    return actor


def build_plane_and_sky(floor_material):
    """The shared base of both maps: the 1 km floor (top at z = 0, tag Floor), sky and sun."""
    spawn_box("Floor", (0, 0, -50), (PLANE_SIZE_CM, PLANE_SIZE_CM, 100), floor_material, folder="Map", tags=("Floor",))

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


def spawn_player_start(location, yaw):
    # The PlayerStart capsule is 92 cm half height: at z 92 its bottom is on the floor.
    return spawn(unreal.PlayerStart, "PlayerStart", unreal.Vector(location[0], location[1], 92.0),
                 unreal.Rotator(roll=0, pitch=0, yaw=yaw))


def new_map(path):
    """Opens an empty level at path. An existing map is loaded and every actor in it destroyed:
    deleting a map package in the commandlet leaves it registered, and new_level then refuses the
    path, so the map is emptied in place instead. The result is the same as a new level."""
    levels = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
    if unreal.EditorAssetLibrary.does_asset_exist(path):
        if not levels.load_level(path):
            raise RuntimeError("Could not load " + path)
        actors = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
        doomed = [a for a in actors.get_all_level_actors() if not isinstance(a, unreal.WorldSettings)]
        if doomed and not actors.destroy_actors(doomed):
            raise RuntimeError("Could not empty " + path)
        log("emptied %s (%d actors)" % (path, len(doomed)))
    elif not levels.new_level(path):
        raise RuntimeError("Could not create " + path)


def save_map(path):
    levels = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
    if not levels.save_current_level():
        raise RuntimeError("Could not save " + path)
    log("saved " + path)


def ramp_to_platform(label, x0, y, angle_deg, rise, width, material, tags=(), platform_length=PLATFORM_LENGTH):
    """A ramp whose top face starts on the floor at x0 and rises toward +X by rise cm at angle_deg,
    onto a solid platform platform_length long. Returns the x where the platform ends.

    The ramp is a cube pitched by angle_deg, rise thick, so nothing under the slope is hollow; its
    top face runs from local (-50, 0, 50) at (x0, y, 0) to (50, 0, 50) at the top edge."""
    a = math.radians(angle_deg)
    length = rise / math.sin(a)       # along the slope
    thickness = rise
    center = (x0 + 0.5 * length * math.cos(a) + 0.5 * thickness * math.sin(a), y,
              0.5 * length * math.sin(a) - 0.5 * thickness * math.cos(a))
    spawn_box(label, center, (length, width, thickness), material,
              rotation=unreal.Rotator(roll=0, pitch=angle_deg, yaw=0), tags=tags)
    top_x = x0 + rise / math.tan(a)
    spawn_box(label + "_Platform", (top_x + 0.5 * platform_length, y, 0.5 * rise), (platform_length, width, rise),
              material, tags=tags)
    return top_x + platform_length


def build_lane(marker):
    for m in range(0, 31):
        thick = 10.0 if m % 5 == 0 else 4.0
        spawn_box("Lane_Marker_%02d" % m, (m * 100.0, 0, 0.2), (thick, 300, 0.4), marker,
                  folder="Gym/Lane", collision=False)
        spawn_text("Lane_Label_%02d" % m, "%d m" % m, (m * 100.0, -200, 5), yaw=-90, size=25 if m % 5 else 40,
                   folder="Gym/Lane")
    # Side lines along the lane edges.
    for side, y in (("L", -150), ("R", 150)):
        spawn_box("Lane_Edge_" + side, (1500, y, 0.2), (3000, 4, 0.4), marker, folder="Gym/Lane", collision=False)
    spawn_text("Lane_Title", "Sprint lane 30 m", (-60, -200, 60), yaw=-90, size=40, folder="Gym/Lane")


def build_ramps(obstacle):
    # (label angle, built angle, y). The "44" ramp is built at 43.9 deg: the walkable limit is exactly
    # 44 deg, and a surface built at 44.0000 passes or fails on float rounding. 0.1 deg of margin keeps
    # it reliably walkable; the label stays 44, the limit it demonstrates.
    for angle, built, y in ((15, 15.0, 700), (30, 30.0, 1200), (44, 43.9, 1700), (50, 50.0, 2200)):
        name = "Ramp%d" % angle
        ramp_to_platform(name, 0.0, y, built, RAMP_RISE, RAMP_WIDTH, obstacle, tags=(name,))
        spawn_text(name + "_Label", "%d deg%s" % (angle, " (not walkable)" if angle > 44 else ""),
                   (-60, y, 40), size=35)


def build_stairs(obstacle):
    tread = 35.0
    width = 300.0
    for step, count, y in ((15, 8, 2900), (25, 5, 3400), (40, 3, 3900)):
        tag = "Stairs%d" % step
        for i in range(1, count + 1):
            top = i * step
            spawn_box("%s_Step%d" % (tag, i), ((i - 0.5) * tread, y, 0.5 * top), (tread, width, top), obstacle, tags=(tag,))
        top = count * step
        spawn_box(tag + "_Landing", (count * tread + 100.0, y, 0.5 * top), (200.0, width, top), obstacle, tags=(tag,))
        spawn_text(tag + "_Label", "Steps %d cm" % step, (-60, y, 40), size=35)


def build_gaps(obstacle):
    y = 4600.0
    height = 100.0
    width = 300.0
    block = 300.0
    x = ramp_to_platform("Gaps_Ramp", 0.0, y, 20.0, height, width, obstacle, tags=("Gaps",))
    for gap in (50, 100, 150, 200, 250, 300):
        x += gap
        spawn_box("Gaps_Block_%d" % gap, (x + 0.5 * block, y, 0.5 * height), (block, width, height), obstacle, tags=("Gaps",))
        spawn_text("Gaps_Label_%d" % gap, "%.1f m" % (gap / 100.0), (x - 0.5 * gap, y, height + 60), size=30)
        x += block
    spawn_text("Gaps_Title", "Gaps", (-60, y, 40), size=35)


def build_ledges(obstacle):
    for height, y in ((100, 5400), (200, 6000), (300, 6600), (400, 7200), (600, 7800)):
        tag = "Ledge%d" % height
        ramp_to_platform(tag, 0.0, y, 30.0, float(height), RAMP_WIDTH, obstacle, tags=(tag,))
        spawn_text(tag + "_Label", "Ledge %d m%s" % (height // 100, " (ragdoll)" if height >= 600 else ""),
                   (-60, y, 40), size=35)


def build_physics_boxes(obstacle):
    boxes = (
        ("PhysicsBox_50", (600, -500, 25), 50, 10.0),
        ("PhysicsBox_75", (850, -500, 37.5), 75, 20.0),
        ("PhysicsBox_100", (1150, -550, 50), 100, 40.0),
        ("PhysicsBox_Stack_1", (700, -750, 25), 50, 10.0),
        ("PhysicsBox_Stack_2", (700, -750, 75.5), 50, 10.0),
        ("PhysicsBox_Stack_3", (700, -750, 126), 50, 10.0),
    )
    for label, center, size, mass in boxes:
        actor = spawn_box(label, center, (size, size, size), obstacle, tags=("PhysicsBox",))
        comp = actor.static_mesh_component
        comp.set_mobility(unreal.ComponentMobility.MOVABLE)
        comp.set_collision_profile_name("PhysicsActor")
        comp.set_simulate_physics(True)
        comp.set_mass_override_in_kg(unreal.Name("None"), mass, True)
    spawn_text("PhysicsBox_Label", "Physics boxes", (900, -500, 160), size=30)


def spawn_dummy(marker, x, y, yaw, folder="Dummy"):
    """A target dummy (ACSTargetDummy, tag Dummy) standing on a marked square, facing yaw."""
    cls = unreal.load_class(None, DUMMY_CLASS)
    if cls is None:
        raise RuntimeError("Could not load " + DUMMY_CLASS + " (build the game module first)")
    spawn_box("Dummy_Marker", (x, y, 0.2), (140, 140, 0.4), marker, folder=folder, tags=("DummyMarker",), collision=False)
    # The capsule is 90 cm half height; at z 92 it starts just above the floor and settles onto it.
    dummy = spawn(cls, "Dummy", unreal.Vector(x, y, 92.0), unreal.Rotator(roll=0, pitch=0, yaw=yaw), folder)
    dummy.set_editor_property("tags", [unreal.Name("Dummy")])
    # Flat on the floor in front of the marker, toward the player start: the text material is two-sided,
    # so an upright label reads mirrored from behind, and the camera often circles the dummy. Seen from
    # above, text is at worst turned, never mirrored.
    label = spawn_text("Dummy_Label", "Dummy spot", (x - 100, y, 0.5), yaw=180, size=25, folder=folder)
    label.set_actor_rotation(unreal.Rotator(roll=0, pitch=90, yaw=180), False)
    return dummy


def build_launcher(marker):
    """A ball launcher (ACSBallLauncher, tag Launcher) firing along -X at a marked square 8 m away.

    It throws a ball every few seconds while a character stands on the square."""
    cls = unreal.load_class(None, LAUNCHER_CLASS)
    if cls is None:
        raise RuntimeError("Could not load " + LAUNCHER_CLASS + " (build the CSCharacter plugin first)")
    x, y = LAUNCHER_AT
    launcher = spawn(cls, "BallLauncher", unreal.Vector(x, y, 0.0), unreal.Rotator(roll=0, pitch=0, yaw=180.0), "Gym/Launcher")
    launcher.set_editor_property("tags", [unreal.Name("Launcher")])
    distance = launcher.get_editor_property("stand_distance")
    half = launcher.get_editor_property("stand_half_size")
    stand_x = x - distance
    spawn_box("Launcher_Stand", (stand_x, y, 0.2), (2 * half, 2 * half, 0.4), marker, folder="Gym/Launcher",
              tags=("LauncherStand",), collision=False)
    for side, edge_y in (("L", y - half), ("R", y + half)):
        spawn_box("Launcher_Edge_" + side, (x - 0.5 * distance, edge_y, 0.2), (distance, 4, 0.4), marker,
                  folder="Gym/Launcher", collision=False)
    spawn_text("Launcher_Label", "Ball launcher: stand on the square", (stand_x - 60, y, 40), size=30, folder="Gym/Launcher")


def delete_if_exists(path):
    assets = unreal.EditorAssetLibrary
    if assets.does_asset_exist(path) and not assets.delete_asset(path):
        raise RuntimeError("Could not delete existing " + path)


def build():
    # Maps first: they reference the materials.
    # The maps are emptied and refilled (new_map); the materials are deleted and made again.
    for path in (GRID_MARKER, GRID_OBSTACLE, GRID):
        delete_if_exists(path)

    grid = build_grid_material()
    obstacle = build_instance("MI_Grid_Obstacle", grid, OBSTACLE_COLOR)
    marker = build_instance("MI_Grid_Marker", grid, MARKER_COLOR)

    new_map(PLANE_MAP)
    build_plane_and_sky(grid)
    spawn_player_start((0, 0), 0.0)
    # A dummy a few metres ahead of the player, facing back at the start.
    spawn_dummy(marker, 500.0, 200.0, 200.0)
    save_map(PLANE_MAP)

    new_map(GYM_MAP)
    build_plane_and_sky(grid)
    spawn_player_start((-300, 0), 0.0)
    build_lane(marker)
    build_ramps(obstacle)
    build_stairs(obstacle)
    build_gaps(obstacle)
    build_ledges(obstacle)
    build_physics_boxes(obstacle)
    # In open floor between the start and the physics boxes, facing the start.
    spawn_dummy(marker, 300.0, -450.0, 140.0, folder="Gym/Dummy")
    build_launcher(marker)
    save_map(GYM_MAP)


build()
