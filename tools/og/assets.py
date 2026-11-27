"""Third-party asset ledger: license policy, ledger check, CREDITS.md, catalog import."""
from __future__ import annotations

import csv
import json
import re
import sys
import urllib.request
from pathlib import Path

from og.repo import repo_root as _repo_root
from typing import Literal

HEADER = ["id", "name", "source_url", "author", "license", "license_url",
          "verified", "credit", "local_paths", "catalog_id"]
LEDGER = Path("assets") / "THIRD_PARTY.csv"
CREDITS = Path("CREDITS.md")
CATALOG_URL = "https://tmhsdigital.github.io/Free-Game-Dev-Assets/data.json"
# Local checkout of Free-Game-Dev-Assets, a sibling of this repo. Read, never vendored.
CATALOG_LOCAL = Path("Free-Game-Dev-Assets") / "site" / "dist" / "data.json"
# Third-party roots, relative to the repo root. "*" matches one path segment.
THIRD_PARTY_ROOTS = ["Game/Content/ThirdParty", "Game/Plugins/*/Content/ThirdParty",
                     "Art/ThirdParty"]

Verdict = Literal["allowed", "refused", "unknown"]

_REFUSED_TOKENS = {"NC", "ND", "SA", "GPL", "LGPL", "AGPL"}
_ALLOWED = [re.compile(p) for p in (
    r"CC0(-\d+(\.\d+)?)?", r"PUBLIC-DOMAIN", r"PD", r"UNLICENSE",
    r"CC-BY(-\d+(\.\d+)?)?", r"MIT",
    # BSD: the SPDX ids only (not BSD-4-Clause, not free-form "BSD-..." strings).
    r"BSD-2-CLAUSE", r"BSD-3-CLAUSE", r"BSD-3-CLAUSE-CLEAR", r"0BSD",
    r"APACHE-2(\.0)?",
    r"OFL-1\.1", r"SIL-OFL(-1\.1)?")]


def normalize_license(raw: str) -> str:
    """SPDX-like form: trimmed, upper-case, whitespace runs become hyphens."""
    return re.sub(r"\s+", "-", (raw or "").strip()).upper()


def license_verdict(spdx: str) -> Verdict:
    spdx = normalize_license(spdx)
    if not spdx:
        return "refused"
    if _REFUSED_TOKENS & set(spdx.split("-")):
        return "refused"
    if any(rx.fullmatch(spdx) for rx in _ALLOWED):
        return "allowed"
    return "unknown"


def read_rows(repo: Path) -> list[dict]:
    path = repo / LEDGER
    if not path.exists():
        return []
    with open(path, newline="", encoding="utf-8") as f:
        return list(csv.DictReader(f))


def write_rows(repo: Path, rows: list[dict]) -> None:
    path = repo / LEDGER
    path.parent.mkdir(parents=True, exist_ok=True)
    with open(path, "w", newline="", encoding="utf-8") as f:
        w = csv.DictWriter(f, fieldnames=HEADER, lineterminator="\n")
        w.writeheader()
        w.writerows(rows)


def _glob_regex(pattern: str) -> re.Pattern:
    if pattern.endswith("/"):
        pattern += "**"
    out, i = [], 0
    while i < len(pattern):
        c = pattern[i]
        if pattern.startswith("**/", i):
            out.append("(?:.*/)?")
            i += 3
            continue
        if pattern.startswith("**", i):
            out.append(".*")
            i += 2
            continue
        out.append("[^/]*" if c == "*" else "[^/]" if c == "?" else re.escape(c))
        i += 1
    return re.compile("".join(out))


def _third_party_files(repo: Path) -> list[str]:
    found = set()
    for root in THIRD_PARTY_ROOTS:
        for base in repo.glob(root):
            for p in base.rglob("*"):
                if p.is_file() and p.name != ".gitkeep":
                    found.add(p.relative_to(repo).as_posix())
    return sorted(found)


def check(repo: Path) -> list[str]:
    """Return a list of problems with the ledger and the third-party files."""
    problems: list[str] = []
    path = repo / LEDGER
    rows: list[dict] = []
    if path.exists():
        with open(path, newline="", encoding="utf-8") as f:
            reader = csv.DictReader(f)
            if reader.fieldnames != HEADER:
                problems.append(f"{LEDGER.as_posix()}: header must be exactly {','.join(HEADER)}")
                return problems
            rows = list(reader)
    seen: set[str] = set()
    patterns: list[re.Pattern] = []
    for n, r in enumerate(rows, start=2):
        rid = (r["id"] or "").strip()
        label = rid or f"line {n}"
        if not rid:
            problems.append(f"line {n}: missing id")
        elif rid in seen:
            problems.append(f"{label}: duplicate id")
        seen.add(rid)
        for field in ("name", "source_url", "license_url", "credit"):
            if not (r[field] or "").strip():
                problems.append(f"{label}: missing {field}")
        verdict = license_verdict(r["license"])
        if verdict == "refused":
            problems.append(f"{label}: license {r['license']!r} is refused")
        elif verdict == "unknown":
            problems.append(f"{label}: license {r['license']!r} is unknown; "
                            "review it and extend the policy before use")
        patterns += [_glob_regex(g.strip()) for g in (r["local_paths"] or "").split(";")
                     if g.strip()]
    for rel in _third_party_files(repo):
        if not any(p.fullmatch(rel) for p in patterns):
            problems.append(f"{rel}: third-party file has no matching ledger row")
    return problems


def render_credits(rows: list[dict]) -> str:
    out = ["# Credits", "",
           "Third-party assets used in Combat Simulator. Generated from "
           "`assets/THIRD_PARTY.csv` by `og assets credits`; do not edit by hand.", ""]
    if not rows:
        out += ["No third-party assets yet.", ""]
        return "\n".join(out)
    groups: dict[str, list[dict]] = {}
    for r in rows:
        groups.setdefault(normalize_license(r["license"]) or "UNSPECIFIED", []).append(r)
    for lic in sorted(groups):
        out += [f"## {lic}", ""]
        for r in sorted(groups[lic], key=lambda r: r["id"]):
            line = (r["credit"] or "").strip() or r["name"]
            out.append(f"- {line} ({r['source_url']})" if r["source_url"] else f"- {line}")
        out.append("")
    return "\n".join(out)


def _load_catalog(repo: Path, source: str | None) -> dict:
    if source is None:
        local = repo.parent / CATALOG_LOCAL
        source = str(local) if local.exists() else CATALOG_URL
    if re.match(r"https?://", source):
        with urllib.request.urlopen(source, timeout=30) as resp:  # noqa: S310
            return json.loads(resp.read().decode("utf-8"))
    return json.loads(Path(source).read_text(encoding="utf-8"))


def row_from_entry(e: dict, paths: str = "") -> dict:
    lic = e.get("license_spdx") or e.get("license") or ""
    author = e.get("publisher") or ""
    credit = (e.get("attribution_string") or "").strip()
    if not credit:
        credit = e["name"] + (f" by {author}" if author else "") + f", {lic}"
    return {"id": e["id"], "name": e["name"], "source_url": e.get("url", ""),
            "author": author, "license": lic,
            "license_url": e.get("license_url") or e.get("url", ""),
            "verified": e.get("verified", ""), "credit": credit,
            "local_paths": paths, "catalog_id": e["id"]}


def add_parser(sub) -> None:
    p = sub.add_parser("assets", help="third-party asset ledger and license check")
    asub = p.add_subparsers(dest="assets_command", required=True)
    c = asub.add_parser("check", help="validate the ledger and third-party files")
    c.set_defaults(func=run_check)
    cr = asub.add_parser("credits", help="regenerate CREDITS.md from the ledger")
    cr.set_defaults(func=run_credits)
    a = asub.add_parser("add", help="add a ledger row")
    a.add_argument("--from-catalog", required=True, metavar="ENTRY_ID")
    a.add_argument("--catalog", help="catalog data.json path or URL")
    a.add_argument("--paths", default="", help="';'-separated globs relative to the repo root")
    a.set_defaults(func=run_add)


def run_check(args) -> int:
    problems = check(_repo_root())
    for p in problems:
        print(p)
    if problems:
        print(f"{len(problems)} asset problem(s).", file=sys.stderr)
        return 1
    return 0


def run_credits(args) -> int:
    repo = _repo_root()
    (repo / CREDITS).write_text(render_credits(read_rows(repo)), encoding="utf-8", newline="\n")
    return 0


def run_add(args) -> int:
    repo = _repo_root()
    try:
        data = _load_catalog(repo, args.catalog)
    except (OSError, ValueError) as exc:
        print(f"cannot load catalog: {exc}", file=sys.stderr)
        return 1
    entry = next((e for e in data.get("entries", []) if e.get("id") == args.from_catalog), None)
    if entry is None:
        print(f"catalog entry {args.from_catalog!r} not found", file=sys.stderr)
        return 1
    new = row_from_entry(entry, args.paths)
    verdict = license_verdict(new["license"])
    if verdict != "allowed":
        print(f"{new['id']}: license {new['license']!r} is {verdict}; not added", file=sys.stderr)
        return 1
    rows = read_rows(repo)
    if any(r["id"] == new["id"] for r in rows):
        print(f"{new['id']}: already in the ledger", file=sys.stderr)
        return 1
    write_rows(repo, rows + [new])
    print(f"added {new['id']} ({new['license']}); run `og assets credits`")
    return 0
