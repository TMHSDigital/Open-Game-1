# Systems map

What this project reuses from, and may contribute to, the owner's private Game-Systems library. Check it before building any system (see `CLAUDE.md`). Game-Systems refs are `<catalogue>#<entry>` or `specs/<name>`.

Status values: `adopted` (used as is), `adapted` (rewritten for this project), `planned` (not built yet), `contributes` (this project is expected to add it to the library).

Reusable code here goes in self-contained plugins under `Game/Plugins/CS<Name>/` so it can be catalogued with `/catalogue-sync` (see [WORKFLOWS.md](../WORKFLOWS.md)).

| Need | Game-Systems ref | Status | Local path |
|---|---|---|---|
| Gated Steam release (numbered gates, fresh staging, development branch only) | `siege-game#steampipe-deploy-gated` | adapted | `tools/steam/release.ps1`, `tools/steam/test-release.ps1`, `steam/templates/` |
| UE build, test and package scripts | `siege-game#ue-powershell-build-scripts` | adapted | `tools/ue/` |
| Unreal MCP editor bootstrap | `siege-game#unreal-mcp-editor-bootstrap` | adapted | `tools/ue/editor.ps1`, `.mcp.json.example`, `Game/Config/` |
| Evolution capture (fixed cameras over time, compare) | `siege-game#siege-capture-subsystem`, `siege-game#evolution-timeline-compare` | adapted | `Game/Source/CSCapture/`, `tools/ue/capture.ps1`, `docs/captures/`, `.claude/skills/capture/` |
| UE automation test harness | `siege-game#ue-automation-test-harness` | adapted | `tools/ue/test.ps1`, `Game/Source/CombatSimulator/Tests/`, `Game/Source/CSCapture/Private/Tests/` |
| Secret scanning (pre-commit, CI, pre-release) | `specs/secret-scanning` | adapted | `tools/og/secrets.py`, `tools/hooks/pre-commit`, `.github/workflows/checks.yml` |
| Media layer (clips, stills, redaction) | `specs/media-layer` | adapted | screencast-mcp for clips and redaction; WebP stills in `tools/og/devlog.py` |
| Tunable weighty character movement (gaits, sprint ramp, slope and landing costs, validated data-asset tuning with live reload) | none yet | contributes | `Game/Plugins/CSCharacter/` (`ACSCharacter`, `UCSMovementTuning`, `UCSDevTools`), `Game/Content/Characters/Base/DA_MovementTuning` |
| First and third person camera rig (one key, blended, head hidden from the owner only) | none yet | contributes | `Game/Plugins/CSCharacter/` (`UCSCameraRig`) |
| Physics-driven body (physical animation, stagger, ragdoll and recovery) | none yet | contributes | `Game/Plugins/CSCharacter/` (`UCSPhysicsBodyComponent`, editor module `CSCharacterEditor` for the ragdoll physics asset builder) |
| Player settings (one record with clamped fields, one update path that saves and applies, a panel generated from the rows) | `specs/settings` | adapted | `Game/Source/CombatSimulator/CSGameSettings.*`, `CSPlayerController.*`, `UI/SCSPauseScreen.*` |
| Feel tuning in a running build (range table, generated slider panel, live copy, changes saved per machine, copy out and bake back) | `specs/feel-tuning` | adapted (no presets, no feel band harness yet) | `Game/Plugins/CSCharacter/` (`CSTuningRanges`, `CSTuningOverrides`, `UCSDevTools`, editor `UCSTuningBaker`), `Game/Source/CombatSimulator/UI/CSTuningPanelModel.*`, `Game/Content/Python/apply_tuning_values.py` |
| Pause screen in Slate (tabs, value rows, gamepad and keyboard use, key list read from the mapping context) | none yet | contributes | `Game/Source/CombatSimulator/UI/` |
| Contact hits on a physics body (mass and approach speed to an impulse, pre-contact velocity tracking, repeat and own-projectile filters), test ball, launcher and target dummy with reset | `siege-game#training-dummy-sandbox` (concept only: that dummy needs a health system) | contributes | `Game/Plugins/CSCharacter/` (`UCSPhysicsBodyComponent::ReceiveContact`, `ACSHitBall`, `ACSBallLauncher`), `Game/Source/CombatSimulator/CSTargetDummy.*`, `CSGameMode` (`ResetPlayground`) |
| UE mod support | none yet | contributes (planned) | not started; shaped by OD-002, OD-003 and OD-004 |
| Physics sandbox toolset | none yet | contributes (planned) | not started; shaped by OD-001 |
