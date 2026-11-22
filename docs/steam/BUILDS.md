# Steam build ledger

One row per real upload, appended by `tools/steam/release.ps1` after steamcmd reports a BuildID. Dry runs and preview builds are not recorded. Times are UTC.

- `development_live_utc` is set when `release dev` sets the build live on the `development` branch; the previous development build gets `development_replaced_utc` at the same moment. `upload-only` rows leave both empty.
- `default_live_utc` is set by `release promoted <version>` after the owner promotes the build in Steamworks; the previous default build gets `default_replaced_utc`.
- `release status` compares the open rows (live, not replaced) with Steam's real branch state.

Do not edit rows by hand unless Steam and the ledger disagree and you are fixing the ledger to match Steam.

| version | build_id | git_sha | uploaded_utc | development_live_utc | development_replaced_utc | default_live_utc | default_replaced_utc | notes |
|---|---|---|---|---|---|---|---|---|
| 0.1.1 | 26854724 | 959dba53d7c3038be29f8d6d8646065b378b72cf | 2026-11-22T20:59:17Z |  |  |  |  |  |
