"""Docs check: the README says what is true now, and the docs' links go somewhere.

`og docs check` fails when README.md does not link the newest decision or name the newest Steam
build in docs/steam/BUILDS.md, or when a relative link in README.md or a top-level doc in docs/
points at a file that does not exist. A link to a devlog entry newer than every entry written is
allowed: it is the session in progress, whose entry `og session end` writes. CI runs it on every
push and pull request; `og session end` runs the same checks before a session can close.
"""
from __future__ import annotations

import re
import sys
from pathlib import Path

_LINK = re.compile(r"\[[^\]]*\]\(([^)\s]+)\)|<img[^>]+src=\"([^\"]+)\"")
_EXTERNAL = re.compile(r"(?:[a-z][a-z0-9+.-]*:|#|/)", re.I)
_BUILD_ROW = re.compile(r"^\|\s*(\d+\.\d+\.\d+)\s*\|")
_DEVLOG = re.compile(r"docs/devlog/(\d{4}-\d{2}-\d{2}-\d{2})-[^/]+\.md$")
DOCS = ("README.md", "docs/WORKFLOWS.md", "docs/TOOLS.md", "docs/systems/README.md")


def newest_decision(repo: Path) -> str | None:
    found = sorted(p.name for p in (repo / "docs" / "decisions").glob("[0-9][0-9][0-9][0-9]-*.md"))
    return found[-1] if found else None


def newest_build(repo: Path) -> str | None:
    ledger = repo / "docs" / "steam" / "BUILDS.md"
    if not ledger.is_file():
        return None
    rows = [m.group(1) for line in ledger.read_text("utf-8").splitlines() if (m := _BUILD_ROW.match(line))]
    return rows[-1] if rows else None


def readme_status(repo: Path) -> list[str]:
    """The README's status against the repository: the newest decision and build."""
    readme = repo / "README.md"
    if not readme.is_file():
        return []
    text = readme.read_text("utf-8")
    problems = []
    decision = newest_decision(repo)
    if decision and decision not in text:
        problems.append(f"README.md does not link the newest decision ({decision}).")
    build = newest_build(repo)
    if build and f"Build {build} " not in text:
        problems.append(f"README.md does not name the newest Steam build ({build}).")
    return problems


def broken_links(repo: Path) -> list[str]:
    """Relative links in the README and the top-level docs whose target is missing."""
    entries = sorted(p.name[:13] for p in (repo / "docs" / "devlog").glob("[0-9]*.md"))
    newest = entries[-1] if entries else ""
    problems = []
    for rel in DOCS:
        doc = repo / rel
        if not doc.is_file():
            continue
        text = re.sub(r"```.*?```", "", doc.read_text("utf-8"), flags=re.S)  # code blocks hold no links
        for m in _LINK.finditer(text):
            link = m.group(1) or m.group(2)
            if not link or _EXTERNAL.match(link):
                continue
            path = link.split("#")[0]
            target = (doc.parent / path).resolve()
            if target.exists():
                continue
            hit = _DEVLOG.search(target.relative_to(repo.resolve()).as_posix()) if target.is_relative_to(repo.resolve()) else None
            if hit and hit.group(1) > newest:
                continue  # the entry of the session in progress
            problems.append(f"{rel}: link to a missing file: {link}")
    return problems


def check(repo: Path) -> list[str]:
    return readme_status(repo) + broken_links(repo)


def _repo_root() -> Path:
    cwd = Path.cwd().resolve()
    for d in (cwd, *cwd.parents):
        if (d / ".git").exists():
            return d
    return cwd


def _run(args) -> int:
    problems = check(_repo_root())
    for p in problems:
        print(f"og docs: {p}", file=sys.stderr)
    if problems:
        return 1
    print("og docs: README status and doc links are up to date.")
    return 0


def add_parser(sub) -> None:
    p = sub.add_parser("docs", help="check the README status and the docs' links")
    ss = p.add_subparsers(dest="docs_command", required=True)
    c = ss.add_parser("check", help="README names the newest decision and build; doc links resolve")
    c.set_defaults(func=_run)
