# 0013: Ship the movement gym with a dev map switch key

Status: Accepted
Date: 2026-11-23
Issue: [#14](https://github.com/TMHSDigital/Open-Game-1/issues/14)
Supersedes: none

## Context

The first Steam development build (0.1.1) cooked only L_Plane and L_CaptureStage. L_Plane is a flat 1 km plane, so in a packaged build there was nothing to judge slopes, stairs, gaps, ledge drops or physics boxes against: the movement gym (L_MovementGym) existed only in the editor. Movement feel is the point of this milestone, and a packaged build behaves differently from the editor (no console in Shipping, cooked tuning values), so testing the feel only in the editor leaves the shipped build unchecked. The project cooks only listed maps (MapsToCook) so scratch maps do not ship by accident, which means shipping the gym has to be a deliberate choice.

## Decision

L_MovementGym is added to MapsToCook in `Game/Config/DefaultGame.ini` and ships in the Steam build next to L_Plane. A dev key, F2 on keyboard and D-pad right on a gamepad, opens the next map in the game mode's `DevSwitchMaps` list (L_Plane, then L_MovementGym, wrapping), so it switches between the plane and the gym. The key is an Enhanced Input action (`IA_DebugMapSwitch`, made by `create_input_assets.py`) and is gated by `bDevTools` in the applied tuning through `UCSDevTools::AreEnabled()`, like R, P, F1 and F5. The CSCharacter plugin only broadcasts the request; the map list lives in the game's config and the game mode opens the map, so the plugin still names no `/Game` paths.

## Consequences

- The packaged build is a few MB larger (the gym's geometry is engine cubes and shared grid materials, so the cost is small).
- The gym can be played in the shipped build, so slope limits, step heights, landing costs and ragdoll drops are checked in the same build players get.
- F2 is a dev-only key: with `bDevTools` off it does nothing, so a build meant for players turns it off with the other dev keys. Nothing on screen advertises it.
- Every map added to `DevSwitchMaps` must also be in MapsToCook, or F2 fails in a packaged build; the release dry run's manifest shows which maps were cooked.
- The game mode now owns the map list, which is the place later map selection (menus) will grow from.

## Alternatives

- Ship the plane only and judge slopes in the editor: no size cost and no extra key, but the packaged build's movement on slopes, stairs and ledges would never be checked, and a playtester on Steam would have nothing to test beyond flat ground. Rejected for that gap.
- Cook the gym but open it only through the console (`open L_MovementGym`): the console is compiled out of Shipping builds, so this would not work in the build that matters.
- Make the gym the default map: the plane is the agreed start map (decision 0011) and the gym's obstacles are in the way of free movement tests.

## Links

- Open decision: OD-013
- Related: decision 0011 (start with a blank plane and one base character)
