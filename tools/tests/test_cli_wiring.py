"""Issue #52: the session and decisions CLI layers, odd goals, and the gh and decisions error paths."""
import json
import subprocess

import pytest

import og.__main__ as cli
from og import decisions, gh, session
from og.devlog import parse_front

from test_decisions import Stub, make_repo, od
from test_session import DAY, commit, git, only_entry
from test_session import repo  # noqa: F401  (fixture)


# ---- session: odd goals ---------------------------------------------------------------------

def test_a_goal_with_a_newline_stays_on_one_line(repo):  # noqa: F811
    session.start(repo, "Line one\nstart_sha: injected\tand a tab ", today=DAY)
    commit(repo, "feat: work")
    assert session.end(repo, today=DAY) == 0
    text = only_entry(repo).read_text("utf-8")
    meta, _ = parse_front(text)
    assert meta["goal"] == "Line one start_sha: injected and a tab"
    lines = text.split("---")[1].splitlines()
    assert sum(line.startswith("start_sha:") for line in lines) == 1


def test_a_goal_naming_a_template_slot_is_not_expanded(repo):  # noqa: F811
    session.start(repo, "Document {{changes}} and {{id}}", today=DAY)
    commit(repo, "feat: work")
    assert session.end(repo, today=DAY) == 0
    meta, _ = parse_front(only_entry(repo).read_text("utf-8"))
    assert meta["goal"] == "Document {{changes}} and {{id}}"


def test_a_long_goal_gives_a_slug_of_at_most_40_characters(repo):  # noqa: F811
    sid = session.start(repo, "word " * 30, today=DAY)
    state = json.loads((repo / "_scratch/session.json").read_text("utf-8"))
    assert state["id"] == sid and 0 < len(state["slug"]) <= 40


# ---- session and milestone: CLI flags -------------------------------------------------------

@pytest.fixture
def cli_repo(repo, monkeypatch):  # noqa: F811
    monkeypatch.setattr(session, "_repo_root", lambda: repo)
    return repo


def test_cli_session_end_no_commit_writes_the_entry_and_commits_nothing(cli_repo):
    assert cli.main(["session", "start", "CLI wiring"]) == 0
    commit(cli_repo, "feat: work")
    head = git(cli_repo, "rev-parse", "HEAD")
    assert cli.main(["session", "end", "--no-commit"]) == 0
    assert only_entry(cli_repo).is_file()
    assert git(cli_repo, "rev-parse", "HEAD") == head
    assert (cli_repo / "_scratch/session.json").is_file()


def test_cli_session_end_with_a_missing_clip_fails(cli_repo, capsys):
    assert cli.main(["session", "start", "CLI clip"]) == 0
    assert cli.main(["session", "end", "--clip", str(cli_repo / "missing.mp4")]) == 1
    assert "missing.mp4" in capsys.readouterr().err


def test_cli_milestone_no_commit_writes_the_page_and_no_tag(cli_repo):
    assert cli.main(["milestone", "1", "first", "--no-commit"]) == 0
    assert (cli_repo / "docs/milestones/m1-first.md").is_file()
    assert git(cli_repo, "tag", "--list") == ""


# ---- gh ---------------------------------------------------------------------------------------

@pytest.mark.parametrize("url,slug", [
    ("https://github.com/owner/name.git", "owner/name"),
    ("https://github.com/owner/name", "owner/name"),
    ("git" + "@github.com:owner/name.git", "owner/name"),  # split: not an email address
    ("https://gitlab.com/owner/name.git", None),
])
def test_repo_slug_reads_each_url_form(tmp_path, url, slug):
    (tmp_path / ".git").mkdir()
    (tmp_path / ".git" / "config").write_text(f'[remote "origin"]\n\turl = {url}\n', "utf-8")
    assert gh.repo_slug(tmp_path) == slug


def test_repo_slug_without_git_is_none(tmp_path):
    assert gh.repo_slug(tmp_path) is None


def test_gh_failure_raises_with_stderr(monkeypatch):
    def fail(args, **kw):
        return subprocess.CompletedProcess(args, 1, "", "HTTP 404: Not Found")

    monkeypatch.setattr(gh.subprocess, "run", fail)
    with pytest.raises(gh.GhError, match="HTTP 404"):
        gh._gh(["issue", "view", "1"])


# ---- decisions: consistency branches ----------------------------------------------------------

def _adr(root, n, status="Accepted", supersedes="none"):
    (root / "docs" / "decisions" / f"{n:04d}-x-{n}.md").write_text(
        f"# {n:04d}: X {n}\n\nStatus: {status}\nDate: 2026-11-22\nIssue: none\n"
        f"Supersedes: {supersedes}\n\n## Context\n\nx\n", encoding="utf-8")


def test_consistency_names_a_bad_status(tmp_path):
    root = make_repo(tmp_path, [od(1, issue=11)])
    _adr(root, 1, status="Maybe")
    assert any("0001-x-1.md: bad Status 'Maybe'" in p for p in decisions.consistency(root, None))


def test_consistency_names_a_missing_superseded_adr(tmp_path):
    root = make_repo(tmp_path, [od(1, issue=11)])
    _adr(root, 1, supersedes="0009")
    assert any("supersedes 0009, which does not exist" in p
               for p in decisions.consistency(root, None))


def test_consistency_names_a_duplicate_id(tmp_path):
    root = make_repo(tmp_path, [od(1, issue=11), od(1, issue=12)])
    assert any("OD-001: appears more than once" in p for p in decisions.consistency(root, None))


def test_consistency_names_an_open_row_without_an_issue(tmp_path):
    root = make_repo(tmp_path, [od(2)])
    assert any("OD-002: open row has no GitHub Issue" in p for p in decisions.consistency(root, []))


def test_consistency_names_a_resolved_row_whose_issue_is_open(tmp_path, monkeypatch):
    root = make_repo(tmp_path, [od(1, issue=11)], slug="owner/repo")
    monkeypatch.setattr(gh, "_gh", Stub())  # close_decision closes the Issue through it
    close_decision_offline(root, "OD-001")
    problems = decisions.consistency(root, [{"number": 11, "state": "OPEN", "title": "q"}])
    assert any("OD-001: resolved but Issue #11 is still open" in p for p in problems)


def close_decision_offline(root, od_id):
    return decisions.close_decision(root, od_id, "A title", "We chose a.")


def test_a_pipe_inside_a_cell_survives_a_rewrite(tmp_path):
    rows = [od(1, issue=11, question="Either a | b?")]
    table = decisions.render_open_table(rows, None)
    assert decisions.parse_open(table)[0].question == "Either a | b?"


# ---- decisions: a close whose Issue cannot be closed ------------------------------------------

def test_close_with_a_failing_issue_close_leaves_a_consistent_record(tmp_path, monkeypatch):
    root = make_repo(tmp_path, [od(1, issue=11)], slug="owner/repo")

    def fail(args):
        raise gh.GhError("gh issue close failed (1): network")

    monkeypatch.setattr(gh, "_gh", fail)
    with pytest.raises(gh.GhError):
        close_decision_offline(root, "OD-001")
    assert list((root / "docs" / "decisions").glob("0001-*.md"))
    assert decisions.parse_resolved((root / decisions.OPEN_DOC).read_text("utf-8"))[0].id == "OD-001"
    assert decisions.consistency(root, None) == []
    with pytest.raises(KeyError):
        close_decision_offline(root, "OD-001")


def test_cli_close_of_an_unknown_decision_exits_2(tmp_path, monkeypatch, capsys):
    root = make_repo(tmp_path, [od(1, issue=11)])
    monkeypatch.setattr(decisions, "_repo_root", lambda: root)
    assert cli.main(["decisions", "close", "OD-999", "--title", "t", "--decision", "d"]) == 2
    assert "OD-999 is not an open decision" in capsys.readouterr().err
