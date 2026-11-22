import datetime
import json
import re
import subprocess
from pathlib import Path

import pytest
from PIL import Image

from og import gh, session

DAY = datetime.date(2026, 11, 22)


def git(repo: Path, *args: str) -> str:
    r = subprocess.run(["git", "-C", str(repo), *args], capture_output=True, text=True,
                       encoding="utf-8", check=True)
    return r.stdout.strip()


def commit(repo: Path, msg: str, name: str | None = None) -> None:
    (repo / (name or f"f{len(msg)}{abs(hash(msg)) % 9999}.txt")).write_text(msg, "utf-8")
    git(repo, "add", "-A")
    git(repo, "commit", "-q", "-m", msg)


@pytest.fixture
def repo(tmp_path):
    r = tmp_path / "repo"
    r.mkdir()
    git(r, "init", "-q", "-b", "main")
    git(r, "config", "user.name", "Test")
    git(r, "config", "user.email", "test@example.com")
    git(r, "config", "core.hooksPath", "none")
    (r / ".gitignore").write_text("_scratch/\n", "utf-8")
    (r / "docs" / "captures").mkdir(parents=True)
    (r / "docs/captures/INDEX.md").write_text(
        "# Capture index\n\n| date | camera | image path | full 40-char SHA |\n|---|---|---|---|\n",
        "utf-8")
    commit(r, "chore: init")
    return r


def png(path: Path, color=(10, 80, 160)) -> Path:
    path.parent.mkdir(parents=True, exist_ok=True)
    Image.new("RGB", (64, 36), color).save(path)
    return path


def front(entry: Path) -> dict:
    text = entry.read_text("utf-8")
    block = text.split("---")[1]
    return dict(re.findall(r"^(\w+): (.*)$", block, re.M))


def only_entry(repo: Path) -> Path:
    entries = [p for p in (repo / "docs/devlog").glob("*.md") if p.name != "TEMPLATE.md"]
    assert len(entries) == 1
    return entries[0]


def test_start_creates_entry_and_state(repo, capsys):
    sid = session.start(repo, "Build the capture stage", today=DAY)
    assert sid == "2026-11-22-01"
    assert capsys.readouterr().out.splitlines()[0] == sid
    entry = repo / "docs/devlog/2026-11-22-01-build-the-capture-stage.md"
    assert entry.is_file()
    assert session.NARRATIVE_MARKER in entry.read_text("utf-8")
    state = json.loads((repo / "_scratch/session.json").read_text("utf-8"))
    assert state["id"] == sid and state["goal"] == "Build the capture stage"
    assert state["start_sha"] == git(repo, "rev-parse", "HEAD")
    assert state["screencast_session"] is None
    assert session.start(repo, "Second one", today=DAY) == "2026-11-22-02"
    assert session.start(repo, "Next day", today=DAY + datetime.timedelta(days=1)) == "2026-11-23-01"


def test_start_prints_open_setup_items(repo, capsys):
    session.start(repo, "No checklist yet", today=DAY)
    assert "setup" not in capsys.readouterr().out.lower()
    (repo / "docs/steam").mkdir(parents=True)
    (repo / "docs/steam/SETUP-CHECKLIST.md").write_text(
        "# Setup\n\n1. Create the app. *Done 2026-11-01.*\n"
        "2. Add the depot. *Pending.*\n3. Build account. *Blocked on owner.*\n", "utf-8")
    session.start(repo, "With checklist", today=DAY)
    out = capsys.readouterr().out
    assert "Add the depot" in out and "Build account" in out
    assert "Create the app" not in out


def test_end_lists_commits_and_decisions(repo):
    session.start(repo, "Decide things", today=DAY)
    commit(repo, "feat: pick engine settings (OD-003)")
    commit(repo, "docs: record outcome\n\nSupersedes ADR 0002 per review.")
    commit(repo, "fix: unrelated")
    assert session.end(repo, today=DAY) == 0
    entry = only_entry(repo)
    fm = front(entry)
    assert fm["decisions"] == "[OD-003, ADR-0002]"
    assert fm["end_sha"] == git(repo, "rev-parse", "HEAD~1")  # recorded before the devlog commit
    text = entry.read_text("utf-8")
    assert "feat: pick engine settings (OD-003)" in text and "fix: unrelated" in text
    assert "chore: init" not in text
    assert session.NARRATIVE_MARKER in text
    assert git(repo, "log", "-1", "--format=%B").startswith("docs: session 2026-11-22-01 devlog")
    assert "Session: 2026-11-22-01" in git(repo, "log", "-1", "--format=%B")
    assert not (repo / "_scratch/session.json").exists()


def test_end_without_state_uses_trailer_then_head1(repo):
    # Fallback 1: no state file, but a Session-Start trailer commit exists.
    commit(repo, "before")
    session.start(repo, "Crashed session", today=DAY, commit=True)
    commit(repo, "work after start")
    (repo / "_scratch/session.json").unlink()
    assert session.end(repo, today=DAY) == 0
    text = only_entry(repo).read_text("utf-8")
    assert "work after start" in text and "before" not in text
    assert "WARNING" not in text

    # Fallback 2: no state, no trailer: HEAD~1 and a visible warning.
    r2 = repo.parent / "repo2"
    r2.mkdir()
    git(r2, "init", "-q", "-b", "main")
    git(r2, "config", "user.name", "Test")
    git(r2, "config", "user.email", "test@example.com")
    git(r2, "config", "core.hooksPath", "none")
    commit(r2, "one")
    commit(r2, "two")
    one = git(r2, "log", "--oneline", "-1", "HEAD~1")
    assert session.end(r2, today=DAY) == 0
    text = only_entry(r2).read_text("utf-8")
    assert "> WARNING:" in text and "HEAD~1" in text
    assert "- " + one not in text and " two" in text


def test_end_blocks_on_secret_in_image(repo):
    session.start(repo, "Leaky capture", today=DAY)
    caps = repo / "_scratch/capture"
    png(caps / "stage-overview.png")
    (caps / "stage-overview.ocr.txt").write_text("Steamworks  Valve Confidential\n", "utf-8")  # og-secrets: allow
    png(caps / "stage-closeup.png", (200, 10, 10))
    before = git(repo, "rev-parse", "HEAD")
    assert session.end(repo, capture_dir=caps, today=DAY) != 0
    assert git(repo, "rev-parse", "HEAD") == before
    assert not (repo / "docs/devlog/media").exists()
    assert "stage-overview" not in (repo / "docs/captures/INDEX.md").read_text("utf-8")
    assert (repo / "_scratch/session.json").exists()  # session stays open for a retry


def test_end_converts_captures_and_updates_index(repo):
    session.start(repo, "Shots", today=DAY)
    caps = repo / "_scratch/capture"
    png(caps / "stage-overview.png")
    assert session.end(repo, capture_dir=caps, today=DAY) == 0
    assert (repo / "docs/devlog/media/2026-11-22-01/stage-overview.webp").is_file()
    row = [l for l in (repo / "docs/captures/INDEX.md").read_text("utf-8").splitlines()
           if "stage-overview" in l][0]
    assert re.search(r"\| 2026-11-22 \| stage-overview \| docs/devlog/media/.+\.webp \| [0-9a-f]{40} \|", row)
    assert "cameras: [stage-overview]" in only_entry(repo).read_text("utf-8")


def test_end_uploads_clip_to_monthly_release(repo, monkeypatch):
    calls = []

    def fake_gh(args):
        calls.append(list(args))
        if args[:2] == ["release", "view"]:
            raise gh.GhError("release not found")
        if args[:2] == ["repo", "view"]:
            return "owner/name\n"
        return ""

    monkeypatch.setattr(gh, "_gh", fake_gh)
    session.start(repo, "Clip", today=DAY)
    clip = repo / "demo.mp4"
    clip.write_bytes(b"not really a video")
    ocr(clip)
    assert session.end(repo, clip=clip, today=DAY) == 0
    creates = [c for c in calls if c[:2] == ["release", "create"]]
    uploads = [c for c in calls if c[:2] == ["release", "upload"]]
    assert len(creates) == 1 and len(uploads) == 1
    assert creates[0][2] == "media-2026-11"
    assert "Devlog media 2026-11" in creates[0]
    assert "Session recordings for the devlog. Not a game build." in creates[0]
    assert "--prerelease" in creates[0]
    assert uploads[0][2] == "media-2026-11"
    url = "https://github.com/owner/name/releases/download/media-2026-11/2026-11-22-01.mp4"
    assert front(only_entry(repo))["clip"] == url
    assert url in only_entry(repo).read_text("utf-8")
    assert calls.index(creates[0]) < calls.index(uploads[0])


def test_end_skips_release_create_when_present(repo, monkeypatch):
    calls = []
    monkeypatch.setattr(gh, "_gh", lambda a: calls.append(list(a)) or "owner/name\n")
    session.start(repo, "Clip", today=DAY)
    clip = repo / "demo.mp4"
    clip.write_bytes(b"x")
    ocr(clip)
    assert session.end(repo, clip=clip, today=DAY) == 0
    assert not [c for c in calls if c[:2] == ["release", "create"]]


def test_end_adds_co_author_trailer_when_asked(repo):
    session.start(repo, "Trailer", today=DAY)
    line = "Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
    assert session.end(repo, co_author=line, today=DAY) == 0
    assert git(repo, "log", "-1", "--format=%B").rstrip().endswith(line)


def test_milestone_pairs_with_previous(repo):
    idx = repo / "docs/captures/INDEX.md"
    base = idx.read_text("utf-8")
    idx.write_text(base + "| 2026-11-01 | stage-overview | docs/devlog/media/a/stage-overview.webp | "
                   + "a" * 40 + " |\n| 2026-11-01 | stage-closeup | docs/devlog/media/a/stage-closeup.webp | "
                   + "a" * 40 + " |\n", "utf-8")
    commit(repo, "captures for m1", "x1.txt")
    git(repo, "add", "-A")
    assert session.milestone(repo, 1, "first-light", today=DAY) == 0
    assert "m1-first-light" in git(repo, "tag", "--list")

    idx.write_text(idx.read_text("utf-8")
                   + "| 2026-11-20 | stage-overview | docs/devlog/media/b/stage-overview.webp | "
                   + "b" * 40 + " |\n", "utf-8")
    git(repo, "add", "-A")
    git(repo, "commit", "-q", "-m", "captures for m2")
    assert session.milestone(repo, 2, "second", today=DAY) == 0
    page = (repo / "docs/milestones/m2-second.md").read_text("utf-8")
    rows = {l.split("|")[1].strip(): l for l in page.splitlines() if l.startswith("| stage-")}
    assert "media/a/stage-overview.webp" in rows["stage-overview"]
    assert "media/b/stage-overview.webp" in rows["stage-overview"]
    assert rows["stage-overview"].index("media/a/") < rows["stage-overview"].index("media/b/")
    assert "media/a/stage-closeup.webp" in rows["stage-closeup"]
    assert "`m1-first-light`" in page


def test_end_second_scan_block_leaves_no_side_effects(repo, monkeypatch):
    calls = []
    monkeypatch.setattr(gh, "_gh", lambda a: calls.append(list(a)) or "owner/name\n")
    session.start(repo, "Leaky log", today=DAY)
    commit(repo, "fix: leak STEAM_APPID=1234567 in message")  # og-secrets: allow
    caps = repo / "_scratch/capture"
    png(caps / "stage-overview.png")
    clip = repo / "demo.mp4"
    clip.write_bytes(b"x")
    ocr(clip)
    index_before = (repo / "docs/captures/INDEX.md").read_text("utf-8")
    head = git(repo, "rev-parse", "HEAD")
    entry_before = only_entry(repo).read_text("utf-8")
    assert session.end(repo, capture_dir=caps, clip=clip, today=DAY) != 0
    assert git(repo, "rev-parse", "HEAD") == head
    assert not (repo / "docs/devlog/media").exists()
    assert (repo / "docs/captures/INDEX.md").read_text("utf-8") == index_before
    assert only_entry(repo).read_text("utf-8") == entry_before
    assert not [c for c in calls if c[:2] in (["release", "upload"], ["release", "create"])]


def test_end_rerun_does_not_duplicate_index_rows(repo):
    caps = repo / "_scratch/capture"
    png(caps / "stage-overview.png")
    session.start(repo, "Twice", today=DAY)
    assert session.end(repo, capture_dir=caps, commit=False, today=DAY) == 0
    assert session.end(repo, capture_dir=caps, commit=False, today=DAY) == 0
    text = (repo / "docs/captures/INDEX.md").read_text("utf-8")
    assert text.count("stage-overview.webp") == 1


def test_trailer_fallback_skips_ended_sessions(repo):
    session.start(repo, "Old session", today=DAY, commit=True)
    commit(repo, "work in old session")
    assert session.end(repo, today=DAY) == 0  # ends it: Session: trailer commit exists
    commit(repo, "later unrelated work")
    (repo / "_scratch").mkdir(exist_ok=True)
    state, warning = session._resolve_start(repo, DAY)
    assert warning and "HEAD~1" in warning
    assert state["start_sha"] == git(repo, "rev-parse", "HEAD~1")
    assert state["id"] != "2026-11-22-01"


def test_milestone_blocked_scan_leaves_no_tag_and_rerun_works(repo):
    idx = repo / "docs/captures/INDEX.md"
    leaky = "| 2026-11-01 | cam | docs/devlog/media/a/STEAM_APPID=1234567.webp | " + "a" * 40 + " |\n"  # og-secrets: allow
    idx.write_text(idx.read_text("utf-8") + leaky, "utf-8")
    git(repo, "add", "-A")
    git(repo, "commit", "-q", "-m", "captures")
    head = git(repo, "rev-parse", "HEAD")
    assert session.milestone(repo, 1, "first", today=DAY) != 0
    assert git(repo, "tag", "--list") == ""
    assert not (repo / "docs/milestones/m1-first.md").exists()
    assert git(repo, "rev-parse", "HEAD") == head
    idx.write_text(idx.read_text("utf-8").replace(leaky, ""), "utf-8")
    git(repo, "add", "-A")
    git(repo, "commit", "-q", "-m", "fix index")
    assert session.milestone(repo, 1, "first", today=DAY) == 0
    assert git(repo, "tag", "--list") == "m1-first"
    # the tag contains the page
    assert "m1-first.md" in git(repo, "ls-tree", "-r", "--name-only", "m1-first")


def test_end_bad_clip_path_fails_before_any_write(repo, monkeypatch):
    calls = []
    monkeypatch.setattr(gh, "_gh", lambda a: calls.append(list(a)) or "")
    session.start(repo, "Bad clip", today=DAY)
    caps = repo / "_scratch/capture"
    png(caps / "stage-overview.png")
    head = git(repo, "rev-parse", "HEAD")
    assert session.end(repo, capture_dir=caps, clip=repo / "missing.mp4", today=DAY) != 0
    assert not (repo / "docs/devlog/media").exists()
    assert git(repo, "rev-parse", "HEAD") == head
    assert not calls


@pytest.mark.parametrize("exc", [gh.GhError("boom"), FileNotFoundError("gh")])
def test_end_upload_failure_warns_with_manual_command_and_commits(repo, monkeypatch, capsys, exc):
    def fake_gh(args):
        if args[:2] == ["release", "view"]:
            return ""
        if args[:2] == ["release", "upload"]:
            raise exc
        return "owner/name\n"

    monkeypatch.setattr(gh, "_gh", fake_gh)
    session.start(repo, "Upload fails", today=DAY)
    clip = repo / "demo.mp4"
    clip.write_bytes(b"x")
    ocr(clip)
    head = git(repo, "rev-parse", "HEAD")
    assert session.end(repo, clip=clip, today=DAY) == 0
    err = capsys.readouterr().err
    assert "gh release upload media-2026-11" in err and "manually" in err
    assert git(repo, "rev-parse", "HEAD") != head  # still committed
    assert front(only_entry(repo))["clip"] == "null"


def test_end_gh_binary_missing_for_release_view(repo, monkeypatch, capsys):
    def fake_gh(args):
        raise FileNotFoundError("gh")

    monkeypatch.setattr(gh, "_gh", fake_gh)
    monkeypatch.setattr(gh, "repo_slug", lambda r: None)
    session.start(repo, "No gh", today=DAY)
    clip = repo / "demo.mp4"
    clip.write_bytes(b"x")
    ocr(clip)
    assert session.end(repo, clip=clip, today=DAY) == 0
    assert "manually" in capsys.readouterr().err


def ocr(path: Path, text: str = "Unreal Editor  CombatSimulator\n") -> Path:
    side = path.with_name(path.name + ".ocr.txt")
    side.write_text(text, "utf-8")
    return side


def test_end_refuses_contact_sheet_without_ocr_sidecar(repo, capsys):
    session.start(repo, "Sheet", today=DAY)
    sheet = png(repo / "_scratch/sheet.png")
    head = git(repo, "rev-parse", "HEAD")
    assert session.end(repo, contact_sheet=sheet, today=DAY) != 0
    assert "ocr" in capsys.readouterr().err.lower()
    assert git(repo, "rev-parse", "HEAD") == head
    assert not (repo / "docs/devlog/media").exists()
    (repo / "_scratch/sheet.ocr.txt").write_text("Unreal Editor\n", "utf-8")
    assert session.end(repo, contact_sheet=sheet, today=DAY) == 0


def test_end_refuses_clip_without_ocr_text(repo, monkeypatch, capsys):
    calls = []
    monkeypatch.setattr(gh, "_gh", lambda a: calls.append(list(a)) or "owner/name\n")
    session.start(repo, "Clip no OCR", today=DAY)
    clip = repo / "_scratch/demo.mp4"
    clip.parent.mkdir(parents=True, exist_ok=True)
    clip.write_bytes(b"x")
    head = git(repo, "rev-parse", "HEAD")
    assert session.end(repo, clip=clip, today=DAY) != 0
    assert "--no-ocr-ok" in capsys.readouterr().err
    assert git(repo, "rev-parse", "HEAD") == head and not calls


def test_end_no_ocr_ok_publishes_and_says_so(repo, monkeypatch, capsys):
    monkeypatch.setattr(gh, "_gh", lambda a: "owner/name\n")
    session.start(repo, "Owner override", today=DAY)
    clip = repo / "_scratch/demo.mp4"
    clip.parent.mkdir(parents=True, exist_ok=True)
    clip.write_bytes(b"x")
    sheet = png(repo / "_scratch/sheet.png")
    assert session.end(repo, clip=clip, contact_sheet=sheet, no_ocr_ok=True, today=DAY) == 0
    err = capsys.readouterr().err
    assert "--no-ocr-ok" in err and "demo.mp4" in err and "sheet.png" in err


def test_end_blocks_secret_in_clip_ocr_text(repo, monkeypatch):
    calls = []
    monkeypatch.setattr(gh, "_gh", lambda a: calls.append(list(a)) or "owner/name\n")
    session.start(repo, "Leaky clip", today=DAY)
    clip = repo / "_scratch/demo.mp4"
    clip.parent.mkdir(parents=True, exist_ok=True)
    clip.write_bytes(b"x")
    ocr(clip, "Steamworks  " + "Valve " + "Confidential\n")
    head = git(repo, "rev-parse", "HEAD")
    assert session.end(repo, clip=clip, today=DAY) != 0
    assert git(repo, "rev-parse", "HEAD") == head
    assert not [c for c in calls if c[:2] in (["release", "upload"], ["release", "create"])]
    ocr(clip)
    assert session.end(repo, clip=clip, today=DAY) == 0
    assert [c for c in calls if c[:2] == ["release", "upload"]]


@pytest.mark.parametrize("with_capture_flag", [False, True])
def test_end_reads_commit_from_capture_json(repo, monkeypatch, with_capture_flag):
    session.start(repo, "Capture sha", today=DAY)
    caps = repo / "_scratch/capture"
    png(caps / "stage-overview.png")
    sha = "c" * 40
    (caps / "capture.json").write_text(json.dumps({"commit": sha, "cameras": []}), "utf-8")
    monkeypatch.setattr(session, "_run_capture", lambda r, d: None)
    assert session.end(repo, capture=with_capture_flag, capture_dir=caps, today=DAY) == 0
    row = [l for l in (repo / "docs/captures/INDEX.md").read_text("utf-8").splitlines()
           if "stage-overview" in l][0]
    assert row.rstrip().endswith(f"| {sha} |")


def test_end_ignores_malformed_capture_json_commit(repo):
    session.start(repo, "Bad capture sha", today=DAY)
    caps = repo / "_scratch/capture"
    png(caps / "stage-overview.png")
    (caps / "capture.json").write_text(json.dumps({"commit": "not-a-sha"}), "utf-8")
    head = git(repo, "rev-parse", "HEAD")
    assert session.end(repo, capture_dir=caps, today=DAY) == 0
    row = [l for l in (repo / "docs/captures/INDEX.md").read_text("utf-8").splitlines()
           if "stage-overview" in l][0]
    assert row.rstrip().endswith(f"| {head} |")


def test_end_commits_only_session_outputs(repo):
    session.start(repo, "Pre-staged", today=DAY)
    (repo / "unrelated.txt").write_text("work in progress", "utf-8")
    git(repo, "add", "unrelated.txt")
    assert session.end(repo, today=DAY) == 0
    assert "unrelated.txt" not in git(repo, "show", "--name-only", "--format=", "HEAD")
    assert "A  unrelated.txt" in git(repo, "status", "--porcelain")


def test_milestone_commits_only_its_page(repo):
    (repo / "unrelated.txt").write_text("work in progress", "utf-8")
    git(repo, "add", "unrelated.txt")
    assert session.milestone(repo, 1, "first", today=DAY) == 0
    assert git(repo, "show", "--name-only", "--format=", "HEAD") == "docs/milestones/m1-first.md"
    assert "A  unrelated.txt" in git(repo, "status", "--porcelain")
