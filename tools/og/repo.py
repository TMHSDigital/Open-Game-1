"""The one way every og command finds the repository it works on.

`git rev-parse --show-toplevel` from the working directory, and then a check that the repository
found is this project: a command run from a folder inside some other git repository stops with a
message instead of reading (or writing) that repository. RepoError is a RuntimeError, so the CLI
turns it into one line and exit code 2.
"""
from __future__ import annotations

import subprocess
from pathlib import Path

# Paths that exist at the root of this project and nowhere else by chance.
MARKERS = ("tools/og", "docs/OPEN-DECISIONS.md")


class RepoError(RuntimeError):
    pass


def repo_root(cwd: Path | None = None, *, project: bool = True) -> Path:
    """The top of the git repository holding cwd (default: the working directory).

    project=False accepts any git repository (the secrets scan works on whatever is staged).
    Raises RepoError outside a repository, without git, or (project=True) in another repository.
    """
    try:
        out = subprocess.run(["git", "rev-parse", "--show-toplevel"], capture_output=True,
                             text=True, encoding="utf-8", cwd=cwd)
    except FileNotFoundError as e:
        raise RepoError("git is not installed or not on PATH") from e
    if out.returncode != 0:
        raise RepoError("not inside a git repository; run og from the Combat Simulator repository")
    root = Path(out.stdout.strip())
    if project:
        missing = [m for m in MARKERS if not (root / m).exists()]
        if missing:
            raise RepoError(f"{root} is not the Combat Simulator repository (it has no "
                            f"{missing[0]}); run og from inside the project")
    return root
