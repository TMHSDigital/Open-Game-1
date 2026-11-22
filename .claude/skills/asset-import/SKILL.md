---
name: asset-import
description: Import a third-party asset into the project under the license policy. Use when the owner wants to bring in a free asset (model, texture, audio, font, plugin content) from the Free-Game-Dev-Assets catalog or another source, and have it recorded in the ledger and credits.
---

# Import a third-party asset

Policy: `docs/assets/ASSET-POLICY.md`. Ledger: `assets/THIRD_PARTY.csv`. Never commit an asset before it has a ledger row.

1. **Find it in the catalog.** Search the Free-Game-Dev-Assets data: the local checkout (a sibling folder of this repo, file `site/dist/data.json`) or `https://tmhsdigital.github.io/Free-Game-Dev-Assets/data.json`. Read it, never copy it into this repo. Note the entry `id`.
2. **Check the license.** Allowed: CC0, public domain, CC-BY, MIT, BSD (BSD-2-Clause, BSD-3-Clause, BSD-3-Clause-Clear, 0BSD), Apache-2.0, OFL-1.1. Refused: any NC, ND or SA variant, GPL, LGPL, AGPL, or no license. Open the entry's `license_url` and confirm the terms at the source, since the catalog is only a reading on its `verified` date. Aggregator entries (`per-item`, `varies`) need the license of the individual file. If the license is refused or unclear, stop and tell the owner.
3. **Download into `_scratch/`.** `_scratch/` is gitignored. Keep the original archive and its license file there.
4. **Import to a ThirdParty path.** Use `Game/Content/ThirdParty/<Pack>/`, `Game/Plugins/<Plugin>/Content/ThirdParty/<Pack>/` or `Art/ThirdParty/<Pack>/`. Include the license text file in the pack folder when the license requires carrying it.
5. **Record it.** From the repo root:
   - `uv run --project tools python -m og assets add --from-catalog <entry-id> --paths "Art/ThirdParty/<Pack>/**"`
   - For an asset not in the catalog, add a row to `assets/THIRD_PARTY.csv` by hand with every column filled in.
6. **Regenerate credits.** `uv run --project tools python -m og assets credits`
7. **Verify.** `uv run --project tools python -m og assets check` must exit 0.
8. **Commit** the assets, `assets/THIRD_PARTY.csv` and `CREDITS.md` together. The pre-commit hook runs the check again. Never use `--no-verify`.
