import os
import subprocess
import sys
from pathlib import Path

import pytest

from og import repo, text

TOOLS = Path(__file__).resolve().parents[1]


def git_init(path):
    subprocess.run(["git", "init", "-q"], cwd=path, check=True)


def test_repo_root_finds_this_project_from_a_subfolder(tmp_path):
    git_init(tmp_path)
    (tmp_path / "tools" / "og").mkdir(parents=True)
    (tmp_path / "docs").mkdir()
    (tmp_path / "docs" / "OPEN-DECISIONS.md").touch()
    assert repo.repo_root(tmp_path / "tools").resolve() == tmp_path.resolve()


def test_repo_root_refuses_another_repository(tmp_path):
    git_init(tmp_path)
    with pytest.raises(repo.RepoError, match="not the Combat Simulator repository"):
        repo.repo_root(tmp_path)
    assert repo.repo_root(tmp_path, project=False).resolve() == tmp_path.resolve()


def test_og_assets_check_in_another_repository_stops_with_a_message(tmp_path):
    # Before the shared lookup, assets check walked up to any .git (or used the folder itself)
    # and read an empty ledger as "no problems".
    git_init(tmp_path)
    env = {**os.environ, "PYTHONPATH": str(TOOLS)}
    r = subprocess.run([sys.executable, "-m", "og", "assets", "check"], cwd=tmp_path,
                       capture_output=True, text=True, env=env)
    assert r.returncode == 2
    assert "not the Combat Simulator repository" in r.stderr and "Traceback" not in r.stderr


def test_fill_does_not_expand_a_value_twice():
    out = text.fill("{{goal}} / {{changes}}", {"goal": "write {{changes}}", "changes": "none"})
    assert out == "write {{changes}} / none"


def test_fill_refuses_a_slot_without_a_value():
    with pytest.raises(KeyError, match="links"):
        text.fill("{{title}} {{links}}", {"title": "t"})


def test_slug_caps_length_and_falls_back():
    assert text.slug("Fix the Open Audit issues!", 12, "x") == "fix-the-open"
    assert text.slug("***", 40, "session") == "session"
