import json
from pathlib import Path

import pytest

from og import gh
from og.decisions import (DEFAULT_TEMPLATE, OpenDecision, close_decision,
                          consistency, list_adrs, new_open, next_adr_number,
                          parse_open, render_open_table, sync_issues)

REPO = Path(__file__).resolve().parents[2]
SLUG = "owner/repo"

SAMPLE = """\
| ID | Question | Why it matters | Blocks | Options known | GitHub Issue | Status |
|---|---|---|---|---|---|---|
| OD-001 | Core loop? | Everything hangs off it | first gameplay prototype | toy box; scenarios | [#11](https://github.com/owner/repo/issues/11) | Open |
| OD-002 | Mod terms? | Modders need clarity | public mod docs | permissive; EULA | - | Open |
"""

DOC_HEAD = "# Open decisions\n\n## Open\n\n"
DOC_TAIL = ("\n## Resolved\n\n"
            "| ID | Question | Decision | GitHub Issue |\n|---|---|---|---|\n")


def make_repo(tmp_path, rows, adrs=(), slug=None):
    (tmp_path / ".git").mkdir()
    if slug:
        (tmp_path / ".git" / "config").write_text(
            f'[remote "origin"]\n\turl = https://github.com/{slug}.git\n', encoding="utf-8")
    (tmp_path / "docs" / "decisions").mkdir(parents=True)
    (tmp_path / "docs" / "OPEN-DECISIONS.md").write_text(
        DOC_HEAD + render_open_table(rows, slug) + DOC_TAIL, encoding="utf-8", newline="\n")
    for n in adrs:
        (tmp_path / "docs" / "decisions" / f"{n:04d}-seed-{n}.md").write_text(
            f"# {n:04d}: Seed {n}\n\nStatus: Accepted\nDate: 2026-11-22\nIssue: none\n"
            "Supersedes: none\n\n## Context\n\nx\n", encoding="utf-8")
    return tmp_path


def od(n, issue=None, **kw):
    base = dict(id=f"OD-{n:03d}", question=f"Question {n}?", why=f"why {n}",
                blocks=f"blocks {n}", options=f"opt a; opt b {n}", issue=issue, status="Open")
    return OpenDecision(**{**base, **kw})


class Stub:
    """Records gh calls; answers label/issue listing and creation."""

    def __init__(self, issues=None, labels=("decision",)):
        self.calls = []
        self.issues = list(issues or [])
        self.labels = list(labels)
        self.next = 100

    def __call__(self, args):
        self.calls.append(list(args))
        if args[:2] == ["label", "list"]:
            return json.dumps([{"name": n} for n in self.labels])
        if args[:2] == ["label", "create"]:
            self.labels.append(args[2])
            return ""
        if args[:2] == ["issue", "list"]:
            return json.dumps(self.issues)
        if args[:2] == ["issue", "create"]:
            self.next += 1
            return f"https://github.com/{SLUG}/issues/{self.next}\n"
        return ""

    def with_prefix(self, *prefix):
        return [c for c in self.calls if c[:len(prefix)] == list(prefix)]


def test_parse_open_table_roundtrip():
    rows = parse_open(SAMPLE)
    assert [r.id for r in rows] == ["OD-001", "OD-002"]
    assert rows[0].issue == 11 and rows[1].issue is None
    assert rows[0].blocks == "first gameplay prototype"
    assert render_open_table(rows, SLUG) == SAMPLE
    assert parse_open(render_open_table(rows, SLUG)) == rows


def test_close_moves_row_and_writes_adr(tmp_path, monkeypatch):
    repo = make_repo(tmp_path, [od(1, 11), od(2, 12)], adrs=range(1, 10), slug=SLUG)
    stub = Stub()
    monkeypatch.setattr(gh, "_gh", stub)
    path = close_decision(repo, "OD-002", "Mod license", "Permissive grant with attribution")
    assert path.name == "0010-mod-license.md"
    text = path.read_text(encoding="utf-8")
    assert "Status: Accepted" in text
    assert "Issue: [#12](https://github.com/owner/repo/issues/12)" in text
    assert "Permissive grant with attribution" in text
    doc = (repo / "docs" / "OPEN-DECISIONS.md").read_text(encoding="utf-8")
    open_part, resolved_part = doc.split("## Resolved")
    assert "OD-002" not in open_part and "OD-001" in open_part
    assert "OD-002" in resolved_part and "decisions/0010-mod-license.md" in resolved_part
    close = stub.with_prefix("issue", "close")
    assert len(close) == 1 and "12" in close[0]
    assert "docs/decisions/0010-mod-license.md" in close[0][close[0].index("--comment") + 1]
    assert consistency(repo, None) == []


def test_close_unknown_id_raises(tmp_path, monkeypatch):
    repo = make_repo(tmp_path, [od(1, 11)])
    monkeypatch.setattr(gh, "_gh", Stub())
    with pytest.raises(KeyError):
        close_decision(repo, "OD-009", "t", "d")


def test_numbers_never_reused(tmp_path):
    repo = make_repo(tmp_path, [], adrs=[1, 2, 3])
    (repo / "docs" / "decisions" / "0002-seed-2.md").unlink()
    assert next_adr_number(repo) == 4
    assert [a.number for a in list_adrs(repo / "docs" / "decisions")] == [1, 3]


def test_check_detects_drift(tmp_path):
    repo = make_repo(tmp_path, [od(1, 11), od(2, 12)], adrs=[1])
    doc = repo / "docs" / "OPEN-DECISIONS.md"
    doc.write_text(doc.read_text(encoding="utf-8")
                   + "| OD-005 | Gone? | [0005-missing](decisions/0005-missing.md) | #15 |\n",
                   encoding="utf-8")
    (repo / "docs" / "decisions" / "0001-seed-1.md").write_text(
        "# 0001: Seed\n\nStatus: Superseded by 0099\nDate: 2026-11-22\nIssue: none\n"
        "Supersedes: none\n", encoding="utf-8")
    issues = [{"number": 11, "state": "CLOSED", "title": "OD-001: Question 1?"},
              {"number": 12, "state": "OPEN", "title": "OD-002: Question 2?"},
              {"number": 77, "state": "OPEN", "title": "OD-077: orphan"}]
    problems = "\n".join(consistency(repo, issues))
    assert "OD-001" in problems and "closed" in problems.lower()
    assert "OD-005" in problems and "0005-missing" in problems
    assert "0099" in problems
    assert "#77" in problems
    assert "OD-002" not in problems  # healthy row stays quiet


def test_check_offline_skips_github(tmp_path):
    repo = make_repo(tmp_path, [od(1, 11), od(2, None)], adrs=[1])
    assert consistency(repo, None) == []  # no GitHub data: file checks only
    (repo / "docs" / "decisions" / "0001-seed-1.md").write_text(
        "# 0001: Seed\n\nStatus: Superseded by 0099\nDate: x\nIssue: none\nSupersedes: none\n",
        encoding="utf-8")
    assert any("0099" in p for p in consistency(repo, None))


def test_sync_issues_creates_only_missing(tmp_path, monkeypatch):
    repo = make_repo(tmp_path, [od(1, 11), od(2, None), od(3, None)], slug=SLUG)
    existing = [{"number": 31, "state": "OPEN", "title": "OD-003: Question 3?"}]
    stub = Stub(issues=existing, labels=[])
    monkeypatch.setattr(gh, "_gh", stub)
    sync_issues(repo)
    creates = stub.with_prefix("issue", "create")
    assert len(creates) == 1 and "OD-002: Question 2?" in creates[0]
    body = creates[0][creates[0].index("--body") + 1]
    for part in ("Why it matters", "Blocks", "Known options",
                 "https://github.com/owner/repo/blob/main/docs/OPEN-DECISIONS.md"):
        assert part in body
    assert "decision" in creates[0]
    assert stub.with_prefix("label", "create")  # label was missing
    rows = {r.id: r.issue for r in parse_open(
        (repo / "docs" / "OPEN-DECISIONS.md").read_text(encoding="utf-8"))}
    assert rows == {"OD-001": 11, "OD-002": 101, "OD-003": 31}
    stub.calls.clear()
    stub.issues += [{"number": 101, "state": "OPEN", "title": "OD-002: Question 2?"}]
    sync_issues(repo)  # idempotent
    assert not stub.with_prefix("issue", "create")


def test_sync_issues_dry_run_changes_nothing(tmp_path, monkeypatch, capsys):
    repo = make_repo(tmp_path, [od(1, None)], slug=SLUG)
    before = (repo / "docs" / "OPEN-DECISIONS.md").read_text(encoding="utf-8")
    stub = Stub(labels=[])
    monkeypatch.setattr(gh, "_gh", stub)
    sync_issues(repo, dry_run=True)
    assert "OD-001" in capsys.readouterr().out
    assert not stub.with_prefix("issue", "create") and not stub.with_prefix("label", "create")
    assert (repo / "docs" / "OPEN-DECISIONS.md").read_text(encoding="utf-8") == before


def test_new_open_appends_row_and_issue(tmp_path, monkeypatch):
    repo = make_repo(tmp_path, [od(1, 11)], slug=SLUG)
    stub = Stub()
    monkeypatch.setattr(gh, "_gh", stub)
    row = new_open(repo, "Pick a name?", "branding", "store page", "a; b")
    assert row.id == "OD-002" and row.issue == 101
    assert stub.with_prefix("issue", "create")
    assert [r.id for r in parse_open(
        (repo / "docs" / "OPEN-DECISIONS.md").read_text(encoding="utf-8"))] == ["OD-001", "OD-002"]


def test_real_repo_template_matches_default_and_checks_offline():
    assert (REPO / "docs" / "decisions" / "TEMPLATE.md").read_text(encoding="utf-8") == DEFAULT_TEMPLATE
    assert consistency(REPO, None) == []
    adrs = list_adrs(REPO / "docs" / "decisions")
    assert [a.number for a in adrs] == list(range(1, 10))
    assert all(a.status == "Accepted" for a in adrs)
    rows = parse_open((REPO / "docs" / "OPEN-DECISIONS.md").read_text(encoding="utf-8"))
    assert [r.id for r in rows] == [f"OD-{n:03d}" for n in range(1, 13)]
    assert rows[0].blocks == "first gameplay prototype"
