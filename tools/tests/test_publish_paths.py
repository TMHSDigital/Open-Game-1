"""Nothing is published before it is scanned: Issue text, decision text, clip OCR text and
the names of committed files."""
import os
import subprocess

import pytest

from og import gh, secrets, session
from og.decisions import close_decision, new_open

from test_decisions import SLUG, Stub, make_repo, od
from test_session import DAY, git, png, repo  # noqa: F401  (repo is a fixture)

FAKE_ID = "1234" + "567"


def test_new_open_scans_the_issue_text_before_posting(tmp_path, monkeypatch):
    r = make_repo(tmp_path, [od(1, 11)], slug=SLUG)
    stub = Stub()
    monkeypatch.setattr(gh, "_gh", stub)
    with pytest.raises(RuntimeError, match="steam-id"):
        new_open(r, "Is store.steampowered.com/app/" + FAKE_ID + " the right page?", "w", "b", "a; b")
    assert not stub.with_prefix("issue", "create")
    assert "OD-002" not in (r / "docs" / "OPEN-DECISIONS.md").read_text(encoding="utf-8")


def test_close_scans_the_decision_text_before_writing(tmp_path, monkeypatch):
    r = make_repo(tmp_path, [od(1, 11)], slug=SLUG)
    monkeypatch.setattr(gh, "_gh", Stub())
    with pytest.raises(RuntimeError, match="steam-id"):
        close_decision(r, "OD-001", "Pick the app", "Use AppID " + FAKE_ID + " from now on.")
    assert list((r / "docs" / "decisions").glob("*.md")) == []


def test_end_refuses_an_empty_ocr_sidecar(repo, capsys):  # noqa: F811
    session.start(repo, "Empty sidecar", today=DAY)
    sheet = png(repo / "_scratch/sheet.png")
    (repo / "_scratch/sheet.ocr.txt").write_text("  \n", "utf-8")
    assert session.end(repo, contact_sheet=sheet, today=DAY) != 0
    assert "usable OCR" in capsys.readouterr().err


def test_end_refuses_an_ocr_sidecar_older_than_the_media(repo, capsys):  # noqa: F811
    session.start(repo, "Stale sidecar", today=DAY)
    side = repo / "_scratch/sheet.ocr.txt"
    side.parent.mkdir(parents=True, exist_ok=True)
    side.write_text("Unreal Editor\n", "utf-8")
    sheet = png(repo / "_scratch/sheet.png")
    old = sheet.stat().st_mtime - 3600
    os.utime(side, (old, old))
    assert session.end(repo, contact_sheet=sheet, today=DAY) != 0
    assert "not older" in capsys.readouterr().err
    os.utime(side, None)
    assert session.end(repo, contact_sheet=sheet, today=DAY) == 0


def test_staged_scan_covers_the_file_name(tmp_path, monkeypatch):
    subprocess.run(["git", "init", "-q", "-b", "main"], cwd=tmp_path, check=True)
    name = "shot of steam_appid " + FAKE_ID + ".txt"
    (tmp_path / name).write_text("nothing secret inside\n", "utf-8")
    subprocess.run(["git", "add", "--", name], cwd=tmp_path, check=True)
    monkeypatch.chdir(tmp_path)
    monkeypatch.setattr(secrets, "_local_rx", [])
    found = secrets.scan_staged()
    assert [f.rule for f in found] == ["steam-id"] and FAKE_ID not in found[0].excerpt_redacted
