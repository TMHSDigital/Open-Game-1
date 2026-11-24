# 0015: Next goal: pause and settings, tuning in the build, then a dummy and contact hits

Status: Accepted
Date: 2026-11-24
Issue: [#58](https://github.com/TMHSDigital/Open-Game-1/issues/58)
Supersedes: none

## Context

Decision 0011 only covered a blank plane and one moving character, so the next block of work needed a goal. The owner played build 0.1.3 from the Steam development branch and found the character clunky and barebones, both in how it responds to input and in how it looks while moving.

Three things stood between that verdict and fixing it. The Steam build, where the feel is judged, could not be paused or quit and had a fixed look sensitivity (#17). None of the roughly 40 movement values could be changed in that build; each try needed an editor edit, a package and an upload (#18). And nothing in the world could hit the character, so the stagger, ragdoll and recovery states could only be seen by pressing a dev key on yourself (#16).

## Decision

After the base character (decision 0011) the next block is, in order: a pause screen with quit and look settings; sliders for every movement value in the Steam build, saved to a file, with the locomotion play rate worked out at run time so the feet stay matched to the ground; then a dummy, props, a ball launcher and contact-driven stagger and ragdoll. It ships as two builds on the development branch. Own animations to replace the stand-in set are the block after this one.

## Consequences

- The owner can change movement values in the Steam build and hand the result back, so the response side of the feel can be fixed without a build per try. The values need a range table in code, because the ranges shown in the editor do not exist in a Shipping build.
- The game gets its first screen and its first saved settings. Both are written in C++ so they stay as text in the repository; restyling waits for art direction (OD-006).
- Moving a gait speed slider must not make the feet slide, so the first half of #37 (play rate at run time) is pulled into this block. Stride warping and new clips are not.
- The dummy and the contact path become the test bed that all later combat moves are built against. Hits on a character go through one entry point, which attacks will reuse.
- The look side of the verdict is not addressed here. The stand-in walk, jog and sprint cycles and the get-up motion (#37 second half, #38) stay as they are until the next block, so the character will still look barebones after these two builds.
- Two packages and uploads instead of one, so the sliders reach the owner before the dummy work is done.

## Alternatives

- Own animations first. Rejected for now: it answers the look but not the response, and without tuning in the build the owner still could not adjust how the character moves. It is the next block.
- First combat moves. Rejected: there is nothing to hit and no way to judge a hit reaction yet; the dummy and contact path come first.
- Only the dummy and props (the first physical interaction), leaving pause and tuning for later. Rejected: the build would still be judged with fixed values and no way to quit.
- One build with everything. Rejected: the sliders would arrive last, and they are what lets the owner start on the movement.
- Tuning by a hand-edited file reloaded with a key, as #18 suggested. Rejected as the main path: each try means leaving the game. The saved file is still plain JSON and can be edited by hand.

## Links

- Open decision: OD-015
- Issues: #16, #17, #18, #37, #56
- Builds on: decision 0011, decision 0013 (the gym ships), decision 0014 (development uploads)
