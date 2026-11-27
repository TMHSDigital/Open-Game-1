# CSCharacter

The playable character for Combat Simulator, as a self-contained Unreal Engine 5.8 plugin.

## What it provides

- `ACSCharacter`: weighty movement with gaits (walk, jog, sprint, crouch), a sprint that builds up, sliding stops, slope and landing costs, Enhanced Input bindings.
- `UCSMovementTuning`: one validated data asset with every feel value; the character runs on an applied copy, and a reload re-applies the asset.
- `UCSCameraRig`: first and third person on one key, with the head hidden from the owner only.
- `UCSPhysicsBodyComponent`: a physics-driven body on top of the animation: sway, stagger, ragdoll and recovery. Every hit, from a punch, a ball or a body, enters through `ReceiveContact`, which reports Stagger, Ragdoll, Blocked (taken on a guard) or Ignored.
- `UCSStrikeComponent`: the strikes: a jab, a straight (cross), a hook from either side and a kick, each a row of `Moves` (`FCSStrikeMoveData`) that scales the punch tuning and names its authored clip and active window (decision 0025). The clip plays in `ClipSlot` as a dynamic montage while `UCSPhysicsBodyComponent::SetAnimationOverride` shows it over the simulated upper body, easing back over the recovery; the fist (or foot) is swept along the animated path and hits at the move's tuned speed, and a hit to the head or neck snaps it back (`HeadSnapScale`). It finds the nearest opponent in front of the aim, aims at the point of its body the player looks at, and steps in while out of reach, drawing out the wind-up for the step (`MaxStepWindupSeconds`). A new hit breaks a strike off. An opponent's slower wind-up is set through `WindupSecondsOverride` (the clip's draw back plays slower). With `bUseClips` off, or no clip, the fist is steered through the physics body and a kick simulates the leg.
- `UCSAnimInstance` loops the guard clip (`GuardClip`, `A_Stance_Idle`) in the upper-body slot while the character is in its combat stance, and the high guard (`BlockClip`, `A_Block_High`) while the block is up (`UCSGuardComponent` then shows it by `BlockClipWeight` over the simulation instead of steering the fists). In the stance the legs take the Stance pose: stance idle, the shuffle step and the jog by speed (`StanceStride`). `UCSAnimGraphBuilder` builds the upper-body slot as a layered blend from `spine_01` under the full-body slot. A punch thrown walking plays in that slot (`MovingClipSlot`), so the legs keep walking.
- `UCSStaminaComponent`: stamina spent by strikes, dodges and blocked hits; out of it, strikes are tired and the guard drops.
- `UCSGuardComponent`: the block (fists steered in front of the head; a share of a hit from in front is absorbed) and the dodge (a quick step with a cooldown, no immunity).
- Combat stance on `ACSCharacter`: a controlled character near another faces its aim, backs off slower, and `FCSAnimNode_LegYaw` turns the legs toward the way they move or plays the steps backward.
- `ACSHitBall` and `ACSBallLauncher`: a thrown ball that hits through `ReceiveContact`, and a launcher that throws one at a marked square.
- `UCSDevTools`: feel overlay, tuning reload, ragdoll, push and map switch keys, all gated by `bDevTools` in the tuning.
- `UCSAnimInstance`: the values the animation blueprint reads.
- Editor module `CSCharacterEditor` (UncookedOnly): builders for the ragdoll physics asset, the sockets and the animation graph, and the graph nodes for the stride and leg-yaw anim nodes.

## What it needs from the project

- A skeletal mesh with the standard UE5 humanoid bone names and a physics asset that has a `pelvis` body. Without one the physics body logs a warning and switches itself off; the character still moves.
- Input actions and a mapping context (see `Game/Content/Python/create_input_assets.py` in this repository for the set the game uses).
- The `Ragdoll` collision profile named in `UCSPhysicsBodyComponent::RagdollCollisionProfile`.

## Tuning changes

`ACSCharacter::ApplyTuning` (and `TryApplyTuning`, and a reload) replaces the active tuning copy, then broadcasts `OnTuningApplied`. A component that keeps state worked out from the tuning rewrites it there, in the same frame: `UCSPhysicsBodyComponent` rewrites its motors (Normal) or its ragdoll damping and friction (Ragdoll). Do not compare tuning pointers each tick.

## Adding an input action

1. A `TSoftObjectPtr<UInputAction>` config property on `ACSCharacter`, a handler, and a row in the binding table in `SetupPlayerInputComponent` (one row per trigger event).
2. The action and its keys in `Game/Content/Python/create_input_assets.py` (run it again), and the property in `Game/Config/DefaultGame.ini`.
3. The action in the expected list of `Game/Source/CombatSimulator/Tests/CSInputAssetTests.cpp`.

The test rigs find the actions by reflection (`ACSCharacter::InputActionProperties`), so they need no change. `CombatSimulator.CS.DevTools.EveryInputActionIsBound` fails when a property has no table row, and `InputContextHasAllActions` fails when it is missing from the expected list.

## Spawning

Spawn `ACSCharacter` like any actor, at any point in a frame. When it is spawned inside a world tick (from a timer, an AI spawner or a respawn), its physics body starts on the next frame: for that one frame the character is plain animation and ignores hits. This is deliberate. Starting the simulated body in the spawn frame crashes in the engine's physics blend, because the new mesh has not been through a full frame yet.

## Tests

The automation tests live under `Source/CSCharacter/Private/Tests/` (movement, dev tools, camera) and, for the parts that need the game's assets, under `Game/Source/CombatSimulator/Tests/` (the body, the punch, the overhaul of the play-test findings, the block, the dodge and the opponent). Run them with `pwsh tools/ue/test.ps1 -Build` from the repository root.

Both sets build their character on the same harness, in `Source/CSCharacter/Public/Tests/` (test-only, compiled out without `WITH_DEV_AUTOMATION_TESTS`):
- `CSTestWorld.h` (`FCSTestWorld`): a test world with a floor and one deferred-spawned `ACSCharacter`, ticked at a fixed step.
- `CSInputRig.h` (`FCSInputRig`): the character's real input bindings, fired as key presses.

The per-file sims (`FMoveSim`, `FBodySim`, `FAnimSim`, `FCameraSim`, `FCSBareCharacterWorld`) only wrap it. A change to how a test character is spawned goes in `FCSTestWorld`.
