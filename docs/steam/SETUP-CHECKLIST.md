# Steamworks setup checklist

The owner's Steamworks steps, in order. `og session start` and `pwsh tools/steam/release.ps1 status` list every item whose status does not start with Done.

Format rule: one numbered line per step, ending with its status in italics. Update the status in place when a step is done (for example `Done 2026-12-01.`).

Steamworks partner pages carry Valve's confidentiality marking: never commit or publish screenshots of them. IDs never go in this file; they live only in the local `.env`.

1. Publish the pending app changes (Publish tab: Prepare for Publishing, then Publish to Steam). Settings only; the store page stays hidden. Can be done any time. *Done 2026-11-22.*
2. Confirm the depot settings for "Combat Simulator Content": OS Windows, language All, architecture All, and that it is in the developer comp package. *Done (depot exists and is in 3 packages); verify the OS.*
3. Tick Windows (64-bit only) under Application, Supported Operating Systems, and add launch option 0 under Installation, General (executable `CombatSimulator.exe`, Windows, 64-bit only), then publish. *Done 2026-11-22.*
4. Choose the upload account: the owner's own Steam account, no separate build account (decision 0010). *Done 2026-11-22.*
5. Log in to steamcmd once on this PC with your own Steam account (password and Steam Guard are typed into steamcmd, never stored by the repo). *Pending.*
6. Fill in the local `.env` from `.env.example`: STEAM_APPID, STEAM_DEPOT_WIN, STEAM_BUILD_ACCOUNT (and STEAMCMD_PATH if steamcmd is not in C:\steamcmd). *Pending.*
7. Run `pwsh tools/steam/release.ps1 upload-only` in your own terminal, then create the password-protected beta branch named exactly `development` on SteamPipe, Builds, then publish. *Blocked until the first build.*
8. From then on, `pwsh tools/steam/release.ps1 dev` uploads builds and sets them live on `development` automatically. *Pending (after step 7).*
