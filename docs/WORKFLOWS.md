# Workflows

Each routine below is a numbered list. Every step names the skill, script and MCP tool it uses. Skills live in `.claude/skills/`; `og` means `uv run --project tools python -m og`. Tool setup is in [TOOLS.md](TOOLS.md). Every routine works without the Unreal editor and without MCP servers unless a step says otherwise.

## Start a session

Skill: `session`.

1. Ask for the goal in one line, and whether to record. Recording is opt-in every time.
2. If recording: screencast MCP `list_targets`, then `start_recording` (target `window:exe=UnrealEditor.exe` or `full`).
3. `og session start "<goal>"` (script `tools/og/session.py`). Add `--commit` to leave a `Session-Start:` marker commit that survives a lost state file. It saves state to `_scratch/session.json`, prints the path the devlog entry will have (`docs/devlog/<id>-<slug>.md`; `end` writes it from `docs/devlog/TEMPLATE.md`), and lists open items from `docs/steam/SETUP-CHECKLIST.md`. Nothing is written under `docs/` until `end`, so a session can be open during a release.
4. If recording: write the screencast session id into `_scratch/session.json`.
5. Work. Decisions go through the `decide` skill, assets through `asset-import`, builds through `ue-build-test`.
6. To end: if recording, screencast MCP `stop_recording`, `find_text` with `redact: true`, `redact_region`, `pipeline` (recipe `pr_demo`), `contact_sheet`.
7. `og session end --capture [--clip ...] [--contact-sheet ...] --co-author "..."`. It runs `tools/ue/capture.ps1` for the fixed cameras, converts stills to WebP under `docs/devlog/media/<id>/`, updates `docs/captures/INDEX.md`, runs `og secrets` on every new file and on the OCR text of the clip and contact sheet, uploads any clip to the `media-YYYY-MM` prerelease with `gh`, and commits only those files. A `--clip` or `--contact-sheet` without its `<file>.ocr.txt` (from screencast `find_text`) is refused unless the owner passes `--no-ocr-ok`.
8. Before step 7, bring the docs up to date and commit them (the owner's rule: every session). `og session end` refuses, writing nothing, when `README.md` does not name the newest decision, the newest Steam build and this session's devlog entry, or when code or tools changed this session and no doc did (README, WORKFLOWS, TOOLS, the systems map, a plugin README or a skill). The owner can waive it with `--docs-ok`.
9. Write the narrative paragraph at the `<!-- narrative -->` marker and commit it as `docs: session <id> narrative`.

## Import a third-party asset

Skill: `asset-import`. Policy: `docs/assets/ASSET-POLICY.md`.

1. Find the entry in the Free-Game-Dev-Assets data (the catalog JSON, or its MCP server). Note the `id`.
2. Confirm the license at the source. Refuse NC, ND, SA, GPL, LGPL, AGPL and blank licenses.
3. Download into `_scratch/` (gitignored).
4. Copy into a `ThirdParty/` path, with the license text if the license needs it carried.
5. `og assets add --from-catalog <id> --paths "<glob>"` writes the row in `assets/THIRD_PARTY.csv`.
6. `og assets credits` regenerates `CREDITS.md`.
7. `og assets check` must exit 0.
8. Commit assets, ledger and credits together; the `tools/hooks/pre-commit` hook runs the checks again.

## Make a Blender asset

Skills: `blender-to-unreal-export`, and `asset-import` for anything not made from scratch. Tools: Blender (4.4 or 5.1 installed), optional Blender MCP or Blender-Developer-Tools skills.

1. Model or clean up in Blender under `Art/` (Metric, unit scale 1.0; skeletal meshes are exported from a centimetre scene, unit scale 0.01, see the export skill). Blender-Developer-Tools skills help with cleanup, LODs and collision.
2. Apply transforms and use the project names: `SM_`, `SK_`, `A_`, `UCX_`, `SOCKET_`.
3. Check without writing: `blender -b <file>.blend --python-exit-code 1 --python .claude/skills/blender-to-unreal-export/export_unreal_fbx.py -- --out Art/Export/<name>.fbx --kind static --all --check-only`.
4. Export with the same command minus `--check-only`.
5. Import into the Content Browser by hand following the skill's steps (a Blender or Unreal MCP import is not wired up yet), then File, Save All.
6. Verify the size against the mannequin in a level; add a capture camera with the `capture` skill if the asset should show in the devlog.
7. Commit `.blend`, `.fbx` and `.uasset` files (tracked through Git LFS) and let the pre-commit hook scan them (or run `og secrets --staged` yourself before committing).

## Tune character movement

Plugin: `Game/Plugins/CSCharacter/`. Tuning asset: `Game/Content/Characters/Base/DA_MovementTuning` (a `UCSMovementTuning` data asset). Tools: the Unreal editor; no MCP server is needed.

1. Open `L_MovementGym` in the editor and press Play. The gym is cooked into the Steam build too (decision 0013): in any build, F2 (gamepad D-pad right) switches between `L_Plane` and the gym when `bDevTools` is on. It has a 30 m sprint lane with 1 m markers, ramps at 15, 30, 44 and 50 degrees, stairs with 15, 25 and 40 cm steps, gaps from 50 to 300 cm, ledges from 1 m to 6 m, and movable physics boxes. `L_Plane` is the default map: a blank grid floor and one character.
2. Change values in the running game, in any build: press Escape (Tab in the editor, Start on a gamepad) for the pause screen and open the Tuning tab. It has a slider and a number box for every movement value, grouped as in the asset. A change applies at once and is kept on that machine in `<Saved>/Tuning/MovementTuning.json` (Steam build: `%LOCALAPPDATA%\CombatSimulator\Saved\Tuning`), so it is still there after a restart and in the next build. The game is paused while the screen is open: resume to feel the change. A value that breaks a rule between fields (walk above jog, for example) is refused and the status line says why. Each row has a Reset; Reset all goes back to the packaged values and removes the file. The tab exists only while `bDevTools` is on in the packaged asset, and so does the file: with it off the build runs exactly as packaged.
3. The feel overlay (F1) and the reload line say which values are in play: `tuning: packaged`, or `tuning: packaged + 3 changes`, with `(file from 0.1.3)` when the changes were saved by another build. Check this line before judging a screenshot or clip.
4. To make changed values the packaged ones: press Copy values on the Tuning tab, paste the text into `_scratch/tuning-values.json`, and run `Game/Content/Python/apply_tuning_values.py` headless (`UnrealEditor-Cmd` with `-run=pythonscript -script=...`). It writes the values into `DA_MovementTuning` after the same checks the game runs, or writes nothing and says which value it refused. Commit the asset. The saved changes that now equal the packaged values stop counting as changes.
5. In the editor you can also edit `DA_MovementTuning` directly and save it; each field has a tooltip. Press F5 (gamepad D-pad left) in the running game, or type `cs.Tuning.Reload` in the console, to re-read the asset and the saved changes. Both need `bDevTools` on. The result shows on screen: green when accepted, red when a value was rejected. A rejected asset or file leaves the last good values in place; the message names the field. The console is compiled out of Shipping builds.
6. The ranges of the sliders, of the editor fields and of the validity check are one table (`CSTuningRanges.cpp` in the plugin). A new tuning field needs a row there; a test fails until it has one.
7. Use the dev keys to check the result. They work only while `bDevTools` is on in the applied tuning (it is on by default; turn it off for a build you do not want them in). Turning it off by reload stays off until the next play session.

| Key | Gamepad | Does |
|---|---|---|
| Escape, Tab | Start | Pause screen: Resume, Quit to desktop, and the tabs Settings (mouse and stick sensitivity, invert Y), Tuning (dev tools only) and Keys. Works with `bDevTools` off |
| F1 | View/Back | Feel overlay: horizontal speed, horizontal acceleration, vertical speed, grounded, floor slope angle, gait, camera view, body state, build and which tuning values are in play |
| F2 | D-pad right | Switch map: `L_Plane` to `L_MovementGym` and back (the game mode's `DevSwitchMaps` in `DefaultGame.ini`) |
| F5 | D-pad left | Reload `DA_MovementTuning` and the saved changes |
| R | D-pad down | Ragdoll the character, or recover it |
| P | D-pad up | Push the upper body (`DevPushSpeed`; raise it above the ragdoll threshold to knock the character down) |
| T | Tuning tab button | Throw a ball from the chest along the view (`DevBallMass` at `DevBallSpeed`). It strikes the body part it touches and never hits whoever threw it |
| Backspace | Tuning tab button | Reset: physics props back where the map placed them, a fresh dummy on its spot, balls removed, player back at the PlayerStart |
| V | R3 | Switch first and third person (works with `bDevTools` off) |

On the pause screen: the mouse works everywhere; with a keyboard or gamepad, the arrow keys, D-pad or left stick move between controls, Enter or the bottom face button presses one, Left and Right change a slider, Page Up and Page Down or the shoulder buttons change tab, and Escape, Tab, Start or the right face button resume. Settings are kept per machine in the user's `GameUserSettings.ini` and apply at once.

Play keys: WASD and mouse, Space jump (from a crouch it stands up and jumps), Left Shift sprint, Left Alt walk, Left Ctrl crouch toggle, left mouse jab and straight (right trigger; a hook while moving sideways near an opponent), F kick (right shoulder), right mouse held to block (left trigger), Q dodge (left face button). The pause screen's Keys tab lists every binding from the mapping context. Falling off the plane (below 50 m under it, `RespawnKillZ`) respawns the character at the PlayerStart.

8. Things that hit the character. Both maps have a target dummy on a marked square: the same character with no controller, tinted, running on the same tuning and tuning changes as the player. A dummy set to fight back (a run's "Dummies do: Fight back", or `ACSTargetDummy::SetFightsBack`) gets an `ACSOpponentController` instead, see step 10. Anything that moves into a character hits it: the other body's mass times its own speed toward the character, over the character's mass, times `ContactImpulseScale`, is the size of the hit as a velocity change (at most `ContactImpulseMax`). Below `RagdollImpulseThreshold` it is a stagger, at or above it a ragdoll, exactly as for the P key. A body slower than `MinContactSpeed` does nothing, and only the other body's own motion counts, so walking into a resting box, standing on it or pushing it is not a hit. The same object cannot hit again within `ContactRepeatSeconds`. To be hit yourself, stand on the square in front of the ball launcher in the gym (at x 500, y -1500): it throws a ball every 2.5 s. The overlay (F1) shows the last hit on you and on the nearest dummy: body part, size, result. With the default ball (8 kg at 20 m/s) a hit is a stagger of about 160 cm/s; raise `DevBallMass` or `DevBallSpeed` on the Tuning tab to knock a character down. Combat moves should deliver their hits through `UCSPhysicsBodyComponent::ReceiveContact`, the same entry.
9. Gait speeds and the feet: the walk, jog, sprint and crouch clips get their blend position and play rate while the game runs, from the speed the character actually moves and each clip's stride (`UCSAnimInstance::ComputeLocomotion`), so changing a gait speed does not make the feet slide and needs no script rerun. The play rate is held between `LocomotionRateMin` and `LocomotionRateMax`; outside them the feet slide. `setup_animation.py` only needs a rerun when the clips themselves change. The other editor scripts in `Game/Content/Python/` are rerunnable: `import_base_character.py` (mesh, eyes socket, physics asset; never overwrites `DA_MovementTuning`), `create_input_assets.py` (input actions and `IMC_Character`), `create_movement_maps.py` (`L_Plane`, `L_MovementGym`, grid materials, the dummies and the ball launcher) and `create_capture_stage.py`.
10. The fight. The punch, the combat stance, the block, the dodge and stamina are tuned on the Tuning tab like movement (groups Punch, Combat, Defence and Stamina). The strikes (decision 0020) are rows of `UCSStrikeComponent::Moves` (`FCSStrikeMoveData`: jab, straight, left and right hook, kick), each scaling the Punch values (times, reach, speed, weight) and with its stamina cost; edit them on the component or in `FCSStrikeMoveData::Defaults`. A kick simulates the right leg for its length (`UCSPhysicsBodyComponent::SetLimbSimulated`) and counts only on the body from the upper thigh up. Stamina (`UCSStaminaComponent`) is spent by strikes, dodges (`DodgeStaminaCost`) and blocked hits (`BlockStaminaCost` per cm/s the guard absorbed), comes back at `StaminaRegen` after `StaminaRegenDelay`; a strike without enough is tired (`TiredStrikeScale`), and at zero the guard drops until `GuardReturnShare` is back. A run can switch stamina off and scale its recovery. The punch goes for the nearest standing opponent in front of the aim (`PunchAssistRange`, `PunchAssistAngle`) and steps in toward where it stood when the punch began, leashed to 40 cm, so a dodge escapes it. The guard (`UCSGuardComponent`) takes `BlockAbsorb` of a hit from within `BlockAngle` of the facing and reports it as Blocked, not landed; a punch thrown from the guard opens it. The dodge moves at `DodgeSpeed` for `DodgeSeconds`, with `DodgeCooldownSeconds` between, ends at an edge, and gives no immunity. A dummy that fights back runs a fixed pattern (`ACSOpponentController::Pattern`: a jab, a one-two, a right hook, a kick, a jab and a left hook): it closes in to `AttackDistance` (farther for a kick), winds up for the run's wind-up time times the move's (its tell), strikes, recovers, backs off and circles; it holds off while its stamina is short of the next attack, waits while the target is down, reels while staggered and stops when the run ends. To see it: pause screen, Run tab, set "Dummies do" to Fight back, start the run. To check a change to any of this, play the `fight`, `punch`, `moves` and `stamina` scenarios (see Play-test a build).
10. Run `pwsh tools/ue/test.ps1 -Filter CombatSimulator.CS` (movement, animation, camera, body, dev tools, maps). Add capture cameras for the gym with the `capture` skill; owner-view shots use the `view`, `commands` and `settleSeconds` fields described there.

## Play-test a build

The game plays itself through scripted scenarios on a recording. Nothing is sent from outside, so the result does not depend on window focus or guessed distances. Skill: `playtest`.

1. Build the editor (`pwsh tools/ue/build.ps1`), or package one (`tools/ue/package.ps1`) for `-Target Packaged`.
2. `pwsh tools/ue/playtest.ps1 [-Scenario punch,stance] [-Target Packaged]`. Keep the desktop unlocked: the window's area of the screen is recorded. The script keeps the game window on top while it records, so a notification or another window does not end up in the recording.
3. The game (`UCSPlaytestRunner`, started with `-CSPlaytest=`) drives the player, frames the whole body with a review camera, writes the segment label on screen and logs timed events, then quits.
4. The script cuts a clip and an 8-frame contact sheet per segment into `_scratch/playtest/<time>-<scenario>/`, which is gitignored. It also writes `summary.md` with the game's numbers: punches landed, facing error, top speed, when a body went down and got up.
5. Review `summary.md`, `overview.png` and the sheets. Sample the clips frame by frame for a single motion.

New scenarios go in `Game/Source/CombatSimulator/Playtest/CSPlaytestRunner.cpp`, with their name added to the script.

## Open a decision

Skill: `decide`. Tool: `gh`.

1. State the question so it stands alone for a public reader.
2. `og decisions new-open "<question>" --why "..." --blocks "..." --options "a; b; c"`. It appends the next `OD-NNN` row to `docs/OPEN-DECISIONS.md` and creates the `decision`-labelled Issue with `gh`.
3. Commit the table change, mentioning the OD number in the message.
4. Discuss options with the owner; record useful points as Issue comments. Do not decide for the owner.

## Close a decision

Skill: `decide`. Tool: `gh`.

1. Only after the owner states the decision.
2. `og decisions close OD-NNN --title "..." --decision "..."`. It writes `docs/decisions/NNNN-<slug>.md`, moves the row to Resolved and closes the Issue.
3. Open the new file and write Context, Consequences and Alternatives properly (the generated Consequences line is a placeholder).
4. `og decisions check` must exit 0 (`--offline` skips the Issue comparison).
5. Commit, naming the OD and ADR numbers so session devlogs list them.

## Release to dev

Skill: `release` (reference: `docs/steam/BUILD-WORKFLOW.md`). Script: `tools/steam/release.ps1`. Tools: steamcmd (only through the script, with the owner's cached login), `gh`, optional steam-mcp for checks.

1. `pwsh tools/steam/release.ps1 status` to see open setup items and the ledger.
2. `pwsh tools/steam/release.ps1 dry-run` runs gates 4 to 10 for real: UE 5.8.x, version not in the ledger, tests (`tools/ue/test.ps1`), the repo checks through `og check` (secrets scan of every tracked file plus the `-Notes` text, asset ledger, decisions), Steam env var names, fresh staging. It also packages a Shipping build (`tools/ue/package.ps1`) and renders the VDFs from `steam/templates/`. Gates 1 to 3 (on main, clean tree, HEAD pushed) only warn in a dry run.
3. Fix whichever gate failed (exit code `10 + n` is gate n); the tree must be committed and pushed before a real run.
4. Play-test the packaged build the dry run made: `pwsh tools/ue/playtest.ps1 -Target Packaged` (skill `playtest`), with the desktop unlocked, and look at `summary.md` and the sheets. A recording of the lock screen has valid numbers but nothing to look at; say so rather than call it reviewed.
5. Claude runs `pwsh tools/steam/release.ps1 dev` (`upload-only` for a first build). Since decision 0014 development uploads are Claude's job; the owner can still run the same command. Setting `default` live and `promoted` stay with the owner.
6. On success the script commits a row to `docs/steam/BUILDS.md`, creates a `v<ver>` prerelease and a `steam-development` deployment with `gh`, and pushes. If steamcmd printed an error or no BuildID, nothing is recorded.

## Promote

Skill: `release`. Script: `tools/steam/release.ps1 promote`. Tools: `gh`, owner in Steamworks.

1. `pwsh tools/steam/release.ps1 promote <ver>` writes `_release/<ver>/` (gitignored): `post.bbcode`, `patch-notes.md`, `github-release.md`, `checklist.md`.
2. Claude fills the `[narrative]` marker in `post.bbcode` and `github-release.md` from the devlogs listed in `patch-notes.md`: player-facing, no IDs, no internal paths.
3. The owner reads the drafts and `checklist.md`, sets the build live on `default` in Steamworks and publishes the post. No script sets `default` live.
4. The owner runs `pwsh tools/steam/release.ps1 promoted <ver>`: it records `default_live_utc`, makes the GitHub Release the Latest full release, and records a `steam-default` deployment.

## Cut a milestone

Skill: `milestone`. Tools: `git`, optionally screencast MCP.

1. Commit the tree and make sure the latest shots are in `docs/captures/INDEX.md` (run the `session` routine or the `capture` skill first).
2. `og milestone <N> <slug>` creates the annotated tag `m<N>-<slug>`, writes `docs/milestones/m<N>-<slug>.md` (each camera now versus the previous milestone), scans it with `og secrets`, and commits.
3. Add a two or three sentence summary at the top of the page and commit it separately. Never move the tag.
4. Push the tag only when the owner asks.

## Contribute a system to Game-Systems

Skill: Game-Systems `catalogue-sync` (run from `E:\Game-Systems`). Tools: `git`, `python scripts/catalogue.py`.

1. Build the system as a self-contained plugin under `Game/Plugins/CS<Name>/`: no game-actor assumptions, its own tests, a short README.
2. Commit it here. Uncommitted work is never catalogued.
3. In Game-Systems run `/catalogue-sync open-game-1` (the first time was `new E:/Open-Game-1`). It adds or updates entries in `catalogues/open-game-1.md` and never copies code.
4. `python scripts/catalogue.py build`, then `python scripts/catalogue.py check` must report 0 errors.
5. Commit in Game-Systems (no emojis, no em dashes, no Steam IDs). Push only when the owner says so.
6. Update the row in `docs/systems/README.md` here (Status `contributes` once catalogued).
