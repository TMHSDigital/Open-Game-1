"""Creates the Enhanced Input assets for the CS character in /Game/Input/.

Makes or refreshes:
  IA_Move, IA_Look                Axis2D actions
  IA_Jump, IA_Sprint, IA_Crouch, IA_Walk, IA_ToggleCamera,
  IA_DebugOverlay, IA_DebugRagdoll, IA_DebugPush, IA_DebugReload, IA_DebugMapSwitch
                                  Boolean actions (no triggers: pressed is Started, released is
                                  Completed; ACSCharacter treats Sprint and Walk as held and Crouch
                                  as a toggle)
  IMC_Character                   the mapping context, keyboard and mouse plus gamepad (KEYS below)

ACSCharacter reads these through the [/Script/CSCharacter.CSCharacter] section of
Game/Config/DefaultGame.ini. The assets are owned by this script: a rerun rebuilds every mapping,
so change keys here, not in the editor.

Value conventions (ACSCharacter): Move is X right, Y forward. Look is degrees this frame, X yaw
right, Y pitch up; the mouse is scaled by MOUSE_DEG_PER_COUNT and the right stick by
STICK_DEG_PER_SECOND and the frame time, so stick turning speed does not depend on the frame rate.

Run headless (editor closed):
    UnrealEditor-Cmd.exe <repo>/Game/CombatSimulator.uproject -run=pythonscript
        -script="<repo>/Game/Content/Python/create_input_assets.py" -unattended -nosplash -nullrhi
or from a running editor through the Unreal MCP server (call_tool, Python execution).
"""

import unreal

DEST = "/Game/Input"
CONTEXT_NAME = "IMC_Character"

# Degrees of view turn per mouse count: the 0.07 sensitivity times the 2.5 scale the project used
# with legacy input scales.
MOUSE_DEG_PER_COUNT = 0.175
# Right stick at full tilt: yaw and pitch, degrees per second.
STICK_DEG_PER_SECOND = (180.0, 120.0)

AXIS2D = unreal.InputActionValueType.AXIS2D
BOOLEAN = unreal.InputActionValueType.BOOLEAN

# Action name -> value type.
ACTIONS = {
    "IA_Move": AXIS2D,
    "IA_Look": AXIS2D,
    "IA_Jump": BOOLEAN,
    "IA_Sprint": BOOLEAN,
    "IA_Crouch": BOOLEAN,
    "IA_Walk": BOOLEAN,
    "IA_ToggleCamera": BOOLEAN,
    "IA_DebugOverlay": BOOLEAN,
    "IA_DebugRagdoll": BOOLEAN,
    "IA_DebugPush": BOOLEAN,
    "IA_DebugReload": BOOLEAN,
    "IA_DebugMapSwitch": BOOLEAN,
}

# (action, key, modifiers). Modifiers are names handled by make_modifiers. Keyboard and mouse keys
# are the project's fixed key layout; the gamepad uses sticks, L3 sprint and the face buttons.
KEYS = [
    ("IA_Move", "W", ["swizzle"]),
    ("IA_Move", "S", ["swizzle", "negate"]),
    ("IA_Move", "A", ["negate"]),
    ("IA_Move", "D", []),
    ("IA_Move", "Gamepad_Left2D", ["deadzone"]),
    ("IA_Look", "Mouse2D", ["mouse_scale"]),
    ("IA_Look", "Gamepad_Right2D", ["deadzone", "stick_scale", "per_second"]),
    ("IA_Jump", "SpaceBar", []),
    ("IA_Jump", "Gamepad_FaceButton_Bottom", []),
    ("IA_Sprint", "LeftShift", []),
    ("IA_Sprint", "Gamepad_LeftThumbstick", []),
    ("IA_Crouch", "LeftControl", []),
    ("IA_Crouch", "Gamepad_FaceButton_Right", []),
    ("IA_Walk", "LeftAlt", []),
    ("IA_Walk", "Gamepad_LeftShoulder", []),
    ("IA_ToggleCamera", "V", []),
    ("IA_ToggleCamera", "Gamepad_RightThumbstick", []),
    ("IA_DebugOverlay", "F1", []),
    ("IA_DebugOverlay", "Gamepad_Special_Left", []),
    ("IA_DebugRagdoll", "R", []),
    ("IA_DebugRagdoll", "Gamepad_DPad_Down", []),
    ("IA_DebugPush", "P", []),
    ("IA_DebugPush", "Gamepad_DPad_Up", []),
    # Tuning reload: the console (cs.Tuning.Reload) is compiled out of Shipping builds.
    ("IA_DebugReload", "F5", []),
    ("IA_DebugReload", "Gamepad_DPad_Left", []),
    # Map switch (the game mode's DevSwitchMaps: L_Plane and L_MovementGym). Gated by bDevTools.
    ("IA_DebugMapSwitch", "F2", []),
    ("IA_DebugMapSwitch", "Gamepad_DPad_Right", []),
]


def log(msg):
    unreal.log("create_input_assets: " + msg)


def load_or_create(name, asset_class, factory):
    path = DEST + "/" + name
    if unreal.EditorAssetLibrary.does_asset_exist(path):
        asset = unreal.load_asset(path)
        if asset is None or not isinstance(asset, asset_class):
            raise RuntimeError(path + " exists but is not a " + asset_class.__name__)
        return asset
    asset = unreal.AssetToolsHelpers.get_asset_tools().create_asset(name, DEST, asset_class, factory)
    if asset is None:
        raise RuntimeError("Could not create " + path)
    log("created " + path)
    return asset


def make_key(name):
    key = unreal.Key()
    key.import_text(name)
    if str(key.get_editor_property("key_name")) != name:
        raise RuntimeError("Unknown key " + name)
    return key


def make_modifiers(context, names):
    mods = []
    for name in names:
        if name == "swizzle":
            # The key's 1 on X goes to Y (forward).
            mod = unreal.new_object(unreal.InputModifierSwizzleAxis, outer=context)
            mod.set_editor_property("order", unreal.InputAxisSwizzle.YXZ)
        elif name == "negate":
            mod = unreal.new_object(unreal.InputModifierNegate, outer=context)
        elif name == "deadzone":
            mod = unreal.new_object(unreal.InputModifierDeadZone, outer=context)
        elif name == "mouse_scale":
            mod = unreal.new_object(unreal.InputModifierScalar, outer=context)
            mod.set_editor_property("scalar", unreal.Vector(MOUSE_DEG_PER_COUNT, MOUSE_DEG_PER_COUNT, 1.0))
        elif name == "stick_scale":
            mod = unreal.new_object(unreal.InputModifierScalar, outer=context)
            mod.set_editor_property("scalar", unreal.Vector(STICK_DEG_PER_SECOND[0], STICK_DEG_PER_SECOND[1], 1.0))
        elif name == "per_second":
            mod = unreal.new_object(unreal.InputModifierScaleByDeltaTime, outer=context)
        else:
            raise RuntimeError("Unknown modifier " + name)
        mods.append(mod)
    return mods


def main():
    actions = {}
    for name, value_type in ACTIONS.items():
        action = load_or_create(name, unreal.InputAction, unreal.InputAction_Factory())
        action.set_editor_property("value_type", value_type)
        actions[name] = action

    context = load_or_create(CONTEXT_NAME, unreal.InputMappingContext, unreal.InputMappingContext_Factory())
    context.unmap_all()
    for action_name, key_name, modifiers in KEYS:
        context.map_key(actions[action_name], make_key(key_name))

    # map_key returns a copy in Python, so the modifiers are set on the whole list and written back.
    data = context.get_editor_property("default_key_mappings")
    mappings = list(data.get_editor_property("mappings"))
    if len(mappings) != len(KEYS):
        raise RuntimeError("Expected %d mappings, got %d" % (len(KEYS), len(mappings)))
    for i, (action_name, key_name, modifiers) in enumerate(KEYS):
        mapping = mappings[i]
        if str(mapping.get_editor_property("key").get_editor_property("key_name")) != key_name:
            raise RuntimeError("Mapping %d is not %s" % (i, key_name))
        mapping.set_editor_property("modifiers", make_modifiers(context, modifiers))
        mappings[i] = mapping
    data.set_editor_property("mappings", mappings)
    context.set_editor_property("default_key_mappings", data)
    context.set_editor_property("context_description", unreal.Text("CS character: move, look, gaits, camera and dev tools"))

    lib = unreal.EditorAssetLibrary
    for name in list(ACTIONS) + [CONTEXT_NAME]:
        if not lib.save_asset(DEST + "/" + name, only_if_is_dirty=False):
            raise RuntimeError("Could not save " + DEST + "/" + name)
    log("%d actions, %d mappings in %s" % (len(ACTIONS), len(KEYS), DEST + "/" + CONTEXT_NAME))
    log("done")


main()
