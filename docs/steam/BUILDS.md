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
| 0.1.5 | 26908258 | bddf9879517ea10ea0737cc50f4de50f9fc4141b | 2026-11-25T00:40:41Z | 2026-11-25T00:40:41Z | 2026-11-25T03:41:52Z | 2026-11-25T02:13:35Z | 2026-11-27T17:46:40Z | A target dummy in both maps; anything that moves into a character staggers it or knocks it down by its mass and speed; T throws a ball, Backspace resets the dummy, props and player; a ball launcher in the gym; the overlay shows the last hit; seven new tuning sliders for contact hits and the ball. |
| 0.1.6 | 26911177 | 3fccf9574ec2019e52a5cbfe8e6be589648ed079 | 2026-11-25T03:41:52Z | 2026-11-25T03:41:52Z | 2026-11-25T11:45:56Z |  |  |  |
| 0.1.7 | 26919197 | 4f72d62c5940314244778043c4750c78e945684d | 2026-11-25T11:45:56Z | 2026-11-25T11:45:56Z | 2026-11-25T14:34:12Z |  |  |  |
| 0.1.8 | 26922995 | b5a10861ddae0ad143ee590c322dfe6183a96d9a | 2026-11-25T14:34:12Z | 2026-11-25T14:34:12Z | 2026-11-25T18:20:33Z |  |  |  |
| 0.1.9 | 26928152 | 49aa875ea93ce810a4cdb0f2708069fe506818bf | 2026-11-25T18:20:33Z | 2026-11-25T18:20:33Z | 2026-11-26T02:56:08Z |  |  | Overhaul after a recorded play-test: punches find and reach their target, combat stance faces the opponent, legs turn and step backward, the camera follows a fall, hit flash, marks and hit-stop; scripted play-test tool. |
| 0.1.10 | 26937611 | 7620e62c54a162ccc22cf63bcf372fa6ef6c1edb | 2026-11-26T02:56:08Z | 2026-11-26T02:56:08Z | 2026-11-26T18:13:41Z |  |  | An opponent that fights back: dummies can be set to close in and punch on a pattern with a readable wind-up; the player can block (right mouse, left trigger) and dodge (Q, X button); run results count blocked hits, their punches and dodges. |
| 0.1.11 | 26954922 | d5c3cc2dc41b755677bc53b8241537c51919e0eb | 2026-11-26T18:13:41Z | 2026-11-26T18:13:41Z | 2026-11-27T15:38:37Z | 2026-11-27T17:46:40Z | 2026-11-27T21:17:30Z | More attacks and stamina: a jab and straight one-two, hooks while moving sideways, a front kick (F, RB); stamina spent by strikes, dodges and blocked hits, with tired strikes and a guard that drops when it runs out; the opponent throws one-twos, hooks and kicks; runs can switch stamina off. |
| 0.1.12 | 26976897 | 20563d9c164c71f35ca9bddb0b1fcffdf860e72d | 2026-11-27T15:38:37Z | 2026-11-27T15:38:37Z | 2026-11-27T20:41:09Z |  |  | Rebind keys on the pause screen's Keys tab; a history of finished runs with the best per setup on the Run tab; saved tuning survives an update; results say strikes; settings save when a slider is let go; reset brings back a dummy that fell out of the world; a leaner Shipping build. |
| 0.1.13 | 26982668 | fda993adea197f140ff32bf4016e31e6dbb51b59 | 2026-11-27T20:41:09Z | 2026-11-27T20:41:09Z |  | 2026-11-27T21:17:30Z |  | A realistic fighter: every character is now an athletic 180 cm human generated with MakeHuman (skin, eyes and hair still to come), with a natural running stride; punches aim at the upper chest. |
