---
name: playtest
description: Play Combat Simulator through scripted scenarios on a recording and review it (animation, punches, knockdowns, camera, feedback). Use when the user asks to play the game, play-test a build, review or analyse how something looks or feels in game, or check a change in the running game rather than in tests.
---

# Scripted play-test

One command plays the game through scenarios, records it, and cuts a clip and a contact sheet per segment with the game's own numbers. No keys are sent from outside, so nothing depends on focus or guessed distances.

## Run

```powershell
pwsh tools/ue/playtest.ps1                         # every scenario, editor binaries (about 3 minutes)
pwsh tools/ue/playtest.ps1 -Scenario punch,stance  # some
pwsh tools/ue/playtest.ps1 -Target Packaged        # the last package from tools/ue/package.ps1
pwsh tools/ue/playtest.ps1 -List                   # scenario names
pwsh tools/ue/playtest.ps1 -Scenario gym           # the movement gym (its own map)
```

Run it in the background; it prints the summary path at the end. Exit 1 means the game did not play to the end; exit 2 means it did, but one of the game's checks failed (printed as `CHECK FAILED` and listed in the summary).

Steps state what must happen with `C.Check(name, ok)`: the block keeps punches off, a 6 m drop knocks the character down, stamina comes back, and so on. Add one wherever a step has an outcome that must not vary. Single punches are not checks, because they still miss now and then (#71); they are listed as missed instead. A play-test never writes the player's run setup or run history.

- **Editor target:** the editor binaries must be built from the current source (`tools/ue/build.ps1`), or the script refuses to start.
- **Desktop:** it must be unlocked, because the window is captured from the screen. If the machine is locked the recording is black or shows the lock screen; say so instead of reviewing it. The script makes the game window topmost while it records, so other windows do not cover it; a frame showing anything other than the game still means something went wrong, and it must not be shared.

## Scenarios

The game side is `UCSPlaytestRunner` in `Game/Source/CombatSimulator/Playtest/`. Add a scenario there:

1. Add a `Build...` function returning steps.
2. Add its name to `ScenarioNames`.
3. Add its name to `$Scenarios` in the script.

Each step has a label, which starts a segment, a duration and a camera. Steps note facts with `C.Note(...)`; they land in the summary.

| Scenario | What it shows |
|---|---|
| `locomotion` | Idle, walk, jog, sprint, stop, turn around, crouch, jumps, the player camera and first person. |
| `stance` | Facing a dummy while walking in, backing off and circling; notes how far the body is off the dummy. |
| `punch` | Punches from 70, 100 and 150 cm, from the side, with the body turned, from the player camera and first person, a combo and a punch on the move; notes landed and missed, hit size and bone; then the end of a run and the delayed results. |
| `knockdown` | A dummy knocked down by a heavy ball, and the player knocked down from the side, the front and its own camera; notes when they went down and got up. |
| `moves` | Every strike at a dummy (jab, straight, both hooks from 75 cm, a kick from 100 and 150 cm), then the attack key as a player presses it (a one-two, then a hook moving right); notes landed and the hit, and the moves thrown. |
| `stamina` | Straights as fast as they go until stamina runs out (notes how many were thrown tired and the first and last fist speeds), the guard with no stamina, and a rest (stamina before and after). |
| `fight` | A dummy that fights back (decisions 0019 and 0020), throwing jabs, one-twos, hooks and kicks: the player stands, holds the block, dodges back on the tell, blocks and punches back, blocks from its own camera, blocks with no stamina (the guard must give way), lies down (the opponent must wait), and the run ends (it must stop); notes their punches thrown, landed, blocked and missed, the dodges and the player's punches. |
| `body` | First person looking steeply down (hands in view) and at the floor ahead (the own shadow), idle and walking; the guard from the front and side, and a jump from the front (shoulders and arms). Nothing to measure: review the sheets. |
| `screens` | Ends a short run and waits for its results to open, then shows the pause screen's Run tab (results and run history), Settings, Tuning and Keys tabs, and closes it; checks the screen is open and the game paused on every tab, and closed and running after. |
| `motion` | Walk, jog and sprint three ways each (as shipped, arms at full motor strength, the animation alone with the physics layer hidden), then the idle from the front shipped and animation-only: tells whether a pose comes from the clip or from the physics body. Nothing to measure: compare the sheets. |
| `gym` | On L_MovementGym only (the script opens it; `all` leaves it out): the four ramps (50 degrees must be refused), the three stairs, the gaps sprinting and jumping, jogging through the physics boxes, off the 2 m and 6 m ledges (6 m must knock down); notes the highest the feet got, the fastest fall, where the player ended, boxes moved, down and back up. |

The review cameras are:

- **side** and **front:** frame the whole body, including the legs and a body on the ground, which the player's own camera does not.
- **player:** shows what the player sees.

## Review

The output goes to `_scratch/playtest/<time>-<scenario>/`, which is gitignored.

1. Read `summary.md` first: one row per segment with the game's numbers. Missed punches and failed checks are listed at the end.
2. Look at `overview.png`, then the `NN-*.png` sheet of each segment worth a closer look. The Read tool shows images. Each sheet is 8 frames over the segment.
3. For a single motion (a punch is about half a second), sample frames from the segment's clip with screencast `sample_frames` (mode timestamps, 0.05 s apart).
4. The label at the bottom of every frame names the segment. The line under it is the previous segment's result.

Report what was seen against what the numbers say. Keep what was not visible (for example, motion that stills cannot show) apart from what was.
