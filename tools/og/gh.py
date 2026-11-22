"""The one place `gh` is invoked. Tests monkeypatch `og.gh._gh`; callers use `gh._gh(...)`."""
from __future__ import annotations

import re
import subprocess
from pathlib import Path


class GhError(RuntimeError):
    pass


def _gh(args: list[str]) -> str:
    """Run `gh` with args and return stdout; raise GhError with stderr on a non-zero exit."""
    r = subprocess.run(["gh", *args], capture_output=True, text=True, encoding="utf-8")
    if r.returncode != 0:
        raise GhError(f"gh {' '.join(args[:2])} failed ({r.returncode}): {r.stderr.strip()}")
    return r.stdout


def repo_slug(repo: Path) -> str | None:
    """`owner/name` from the repo's own origin URL (read from .git/config), else None."""
    cfg = Path(repo) / ".git" / "config"
    if not cfg.is_file():
        return None
    try:
        r = subprocess.run(["git", "config", "--file", str(cfg), "--get", "remote.origin.url"],
                           capture_output=True, text=True, encoding="utf-8")
    except OSError:
        return None
    m = re.search(r"github\.com[:/]([^/\s]+/[^/\s]+?)(?:\.git)?\s*$", r.stdout.strip())
    return m.group(1) if m else None
