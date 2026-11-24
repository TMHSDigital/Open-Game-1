"""Edge cases across og subcommands: decision references, front matter shapes, the site
output directory, repo slug lookup and the CLI's error boundary."""
import subprocess

import pytest

import og.__main__ as cli
from og import decisions, gh, session, site

from test_site import repo  # noqa: F401  (fixture)


def test_decision_wording_counts_as_a_decision_reference():
    found = session._decisions(["feat: Claude runs uploads (decision 0014)", "docs: ADR 0012",
                                "fix: OD-001", "Decision-0003 follow-up"])
    assert found == ["ADR-0014", "ADR-0012", "OD-001", "ADR-0003"]


def test_scalar_front_matter_lists_render_as_one_item(repo, tmp_path):  # noqa: F811
    entry = next((repo / "docs" / "devlog").glob("2026-11-20-a-*.md"))
    text = entry.read_text("utf-8").replace("decisions: [ADR-0007]", "decisions: OD-001")
    entry.write_text(text, "utf-8")
    out = tmp_path / "_site"
    site.build(repo, out)
    index = (out / "index.html").read_text("utf-8")
    assert "OD-001" in index and "O, D" not in index


def test_site_build_refuses_a_directory_it_did_not_write(repo, tmp_path):  # noqa: F811
    precious = tmp_path / "precious"
    precious.mkdir()
    (precious / "keep.txt").write_text("keep", "utf-8")
    with pytest.raises(SystemExit, match="refusing to delete"):
        site.build(repo, precious)
    assert (precious / "keep.txt").is_file()


def test_site_build_replaces_its_own_previous_output(repo, tmp_path):  # noqa: F811
    out = tmp_path / "_site"
    site.build(repo, out)
    (out / "stale.html").write_text("old", "utf-8")
    site.build(repo, out)
    assert not (out / "stale.html").exists() and (out / "index.html").is_file()


def test_site_build_reports_skipped_entries(repo, tmp_path, monkeypatch):  # noqa: F811
    (repo / "docs" / "devlog" / "2026-11-23-z-broken.md").write_text(
        "---\ngoal: no id here\n---\n\n# Broken\n", "utf-8")
    assert site.build(repo, tmp_path / "a") == ["2026-11-23-z-broken.md"]
    monkeypatch.setattr(site, "_repo_root", lambda: repo)
    assert cli.main(["site", "build", "--out", str(tmp_path / "b")]) == 1


def test_repo_slug_works_in_a_git_worktree(tmp_path):
    def git(cwd, *args):
        subprocess.run(["git", *args], cwd=cwd, check=True, capture_output=True)

    main = tmp_path / "main"
    main.mkdir()
    git(main, "init", "-q", "-b", "main")
    git(main, "config", "user.name", "Test")
    git(main, "config", "user.email", "test@example.com")
    git(main, "remote", "add", "origin", "https://github.com/someone/project.git")
    git(main, "commit", "-q", "--allow-empty", "-m", "initial")
    git(main, "worktree", "add", "-q", str(tmp_path / "wt"))
    assert gh.repo_slug(main) == "someone/project"
    assert gh.repo_slug(tmp_path / "wt") == "someone/project"
    assert gh.repo_slug(tmp_path) is None


def test_issue_links_survive_a_rewrite_without_a_repo_slug(tmp_path):
    doc = tmp_path / decisions.OPEN_DOC
    doc.parent.mkdir(parents=True)
    doc.write_text(
        "# Open decisions\n\n## Open\n\n| ID | Question | Why it matters | Blocks | Options known |"
        " GitHub Issue | Status |\n|---|---|---|---|---|---|---|\n"
        "| OD-001 | Core loop? | Matters | proto | a; b |"
        " [#1](https://github.com/x/y/issues/1) | Open |\n\n"
        "## Resolved\n\n| ID | Question | Decision | GitHub Issue |\n|---|---|---|---|\n", "utf-8")
    md = doc.read_text("utf-8")
    decisions._write_doc(tmp_path, decisions.parse_open(md), decisions.parse_resolved(md), None)
    assert "[#1](https://github.com/x/y/issues/1)" in doc.read_text("utf-8")


def test_cli_turns_a_known_error_into_one_line_and_exit_2(tmp_path, monkeypatch, capsys):
    # Never rely on the temp folder being outside a git repository (a home directory can be one):
    # make the lookup fail the way it does on a machine without git.
    def no_repo():
        raise FileNotFoundError("git is not installed")

    monkeypatch.setattr(session, "_repo_root", no_repo)
    assert cli.main(["session", "start", "anything"]) == 2
    err = capsys.readouterr().err
    assert err.startswith("og session: error:") and "Traceback" not in err
