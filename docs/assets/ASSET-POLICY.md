# Third-party asset policy

Third-party assets keep their own licenses. This file is the rule for what may enter the project and how it is recorded. The decision behind it is ADR 0004.

## Sources

- The Free-Game-Dev-Assets catalog (links only, with SPDX license, verified date and credit line per entry). It is read, never vendored into this repo.
- The asset's original source, when it is not in the catalog.

## Allowed licenses

CC0, public domain equivalents, CC-BY (with credits), MIT, BSD (the SPDX ids BSD-2-Clause, BSD-3-Clause, BSD-3-Clause-Clear and 0BSD; other BSD strings are flagged as unknown), Apache-2.0 and OFL-1.1 (fonts).

An asset-specific license that permits commercial use and modification may be used after review. `og assets check` reports any license it does not recognize as unknown, and an unknown license blocks the commit until the policy is extended deliberately.

## Refused licenses

- Anything NC (non-commercial), ND (no derivatives) or SA (share-alike).
- GPL, LGPL and AGPL.
- A blank license. No license information means no permission.

License strings are normalized before the verdict, so `cc-by-sa-4.0`, `CC BY-NC 4.0` and `GPL-3.0-or-later` are all caught.

## Where third-party files live

- `Game/Content/ThirdParty/`
- `Game/Plugins/<Plugin>/Content/ThirdParty/`
- `Art/ThirdParty/`

Every file under these roots must be covered by a ledger row. Third-party files never go anywhere else in the project.

One exception: a project asset that a script in this repository generates from third-party inputs, and then substantially changes, lives with the project's own assets. The realistic base character is the first case: `Art/Characters/Base/build_human.py` generates it with MPFB from MakeHuman's CC0 assets, then re-rigs and re-weights it (decision 0022). The third-party inputs themselves are not committed. The generated files still get a ledger row, and its `local_paths` names them exactly. This applies only to inputs whose license allows that use without carrying a license file (CC0 or public domain). Anything that must carry its license text goes under a ThirdParty root.

## The ledger

`assets/THIRD_PARTY.csv`, one row per imported asset. The header is exact:

`id,name,source_url,author,license,license_url,verified,credit,local_paths,catalog_id`

- `license` is an SPDX-like identifier such as `CC-BY-4.0`.
- `verified` is the date the license was last read at the source.
- `credit` is the line shown in `CREDITS.md`.
- `local_paths` is a `;`-separated list of globs relative to the repo root. `*` matches within a folder, `**` matches across folders.
- `catalog_id` is the Free-Game-Dev-Assets entry id, or empty for assets from elsewhere.

## Credits

`CREDITS.md` is generated from the ledger with `og assets credits`, grouped by license. Do not edit it by hand. A test fails if it is out of date.

## Check

`og assets check` fails when a file under a third-party root has no ledger row, or when a row has a refused or unknown license, a blank required field or a duplicate id. It runs in the pre-commit hook and in CI.

## Commands

- `og assets add --from-catalog <entry-id> [--paths <globs>] [--catalog <path-or-url>]` fills a row from the catalog. It refuses an entry whose license is not allowed.
- `og assets credits` regenerates `CREDITS.md`.
- `og assets check` validates the ledger and the files.
