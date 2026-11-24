# CSCharacter

The playable character for Combat Simulator, as a self-contained Unreal Engine 5.8 plugin.

## What it provides

- `ACSCharacter`: weighty movement with gaits (walk, jog, sprint, crouch), a sprint that builds up, sliding stops, slope and landing costs, Enhanced Input bindings.
- `UCSMovementTuning`: one validated data asset with every feel value; the character runs on an applied copy, and a reload re-applies the asset.
- `UCSCameraRig`: first and third person on one key, with the head hidden from the owner only.
- `UCSPhysicsBodyComponent`: a physics-driven body on top of the animation: sway, stagger, ragdoll and recovery.
- `UCSDevTools`: feel overlay, tuning reload, ragdoll, push and map switch keys, all gated by `bDevTools` in the tuning.
- `UCSAnimInstance`: the values the animation blueprint reads.
- Editor module `CSCharacterEditor`: builders for the ragdoll physics asset, the sockets and the animation graph.

## What it needs from the project

- A skeletal mesh with the standard UE5 humanoid bone names and a physics asset that has a `pelvis` body. Without one the physics body logs a warning and switches itself off; the character still moves.
- Input actions and a mapping context (see `Game/Content/Python/create_input_assets.py` in this repository for the set the game uses).
- The `Ragdoll` collision profile named in `UCSPhysicsBodyComponent::RagdollCollisionProfile`.

## Spawning

Spawn `ACSCharacter` like any actor, at any point in a frame. When it is spawned inside a world tick (from a timer, an AI spawner or a respawn), its physics body starts on the next frame: for that one frame the character is plain animation and ignores hits. This is deliberate. Starting the simulated body in the spawn frame crashes in the engine's physics blend, because the new mesh has not been through a full frame yet.

## Tests

The automation tests live under `Source/CSCharacter/Private/Tests/` (movement, dev tools, camera) and, for the parts that need the game's assets, under `Game/Source/CombatSimulator/Tests/`. Run them with `pwsh tools/ue/test.ps1 -Build` from the repository root.
