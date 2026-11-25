# Steam build ledger

One row per real upload, appended by `tools/steam/release.ps1` after steamcmd reports a BuildID. Dry runs and preview builds are not recorded. Times are UTC.

- `development_live_utc` is set when `release dev` sets the build live on the `development` branch; the previous development build gets `development_replaced_utc` at the same moment. `upload-only` rows leave both empty.
- `default_live_utc` is set by `release promoted <version>` after the owner promotes the build in Steamworks; the previous default build gets `default_replaced_utc`.
- `release status` compares the open rows (live, not replaced) with Steam's real branch state.

Do not edit rows by hand unless Steam and the ledger disagree and you are fixing the ledger to match Steam.

| version | build_id | git_sha | uploaded_utc | development_live_utc | development_replaced_utc | default_live_utc | default_replaced_utc | notes |
|---|---|---|---|---|---|---|---|---|
| 0.1.1 | 26854724 | 959dba53d7c3038be29f8d6d8646065b378b72cf | 2026-11-22T20:59:17Z | 2026-11-22T21:28:44Z | 2026-11-23T22:50:45Z | 2026-11-25T02:07:23Z | 2026-11-25T02:13:35Z | first build; set live by hand in Steamworks (development branch created) |
| 0.1.2 | 26879552 | 7d93ae4d038bba619dd395fc6b9a46713a8f74fd | 2026-11-23T22:50:45Z | 2026-11-23T22:50:45Z | 2026-11-24T02:31:52Z |  |  |  |
| 0.1.3 | 26883076 | 13054d2a02f3a0e0aea7d24085bf7b2ae07259bf | 2026-11-24T02:31:52Z | 2026-11-24T02:31:52Z | 2026-11-24T23:51:29Z |  |  | Fixes: a character spawned mid-frame no longer crashes; overlay shows body state, build version and keys; tuning reload can turn dev tools back on. |
| 0.1.4 | 26907477 | 29ca0f4e913b2081b3965903881b583bf4647d78 | 2026-11-24T23:51:29Z | 2026-11-24T23:51:29Z | 2026-11-25T00:40:41Z |  |  | Pause screen on Escape or Start with Resume and Quit; saved look settings (mouse and stick sensitivity, invert Y); a Tuning tab with a slider for every movement value, kept per machine; a key list; feet stay matched to the ground when gait speeds change. |
| 0.1.5 | 26908258 | bddf9879517ea10ea0737cc50f4de50f9fc4141b | 2026-11-25T00:40:41Z | 2026-11-25T00:40:41Z | 2026-11-25T03:41:52Z | 2026-11-25T02:13:35Z |  | A target dummy in both maps; anything that moves into a character staggers it or knocks it down by its mass and speed; T throws a ball, Backspace resets the dummy, props and player; a ball launcher in the gym; the overlay shows the last hit; seven new tuning sliders for contact hits and the ball. |
| 0.1.6 | 26911177 | 3fccf9574ec2019e52a5cbfe8e6be589648ed079 | 2026-11-25T03:41:52Z | 2026-11-25T03:41:52Z | 2026-11-25T11:45:56Z |  |  |  |
| 0.1.7 | 26919197 | 4f72d62c5940314244778043c4750c78e945684d | 2026-11-25T11:45:56Z | 2026-11-25T11:45:56Z | 2026-11-25T14:34:12Z |  |  |  |
| 0.1.8 | 26922995 | b5a10861ddae0ad143ee590c322dfe6183a96d9a | 2026-11-25T14:34:12Z | 2026-11-25T14:34:12Z |  |  |  |  |
