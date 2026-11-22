import json
import re
from html.parser import HTMLParser
from pathlib import Path

import pytest
from PIL import Image

from og import secrets, site

SHA = "a" * 40
INDEX_HEAD = "# Capture index\n\n| date | camera | image path | full 40-char SHA |\n|---|---|---|---|\n"
BUILDS_HEAD = ("| version | build_id | git_sha | uploaded_utc | development_live_utc | "
               "development_replaced_utc | default_live_utc | default_replaced_utc | notes |\n"
               "|---|---|---|---|---|---|---|---|---|\n")


def entry(sid, goal, cams, clip="null"):
    caps = "\n".join(f"![{c}](media/{sid}/{c}.webp)" for c in cams)
    return (f"---\nid: {sid}\ndate: {sid[:10]}\ngoal: \"{goal}\"\nstart_sha: {SHA}\n"
            f"end_sha: {SHA}\ndecisions: [ADR-0007]\ncameras: [{', '.join(cams)}]\n"
            f"clip: {clip}\n---\n\n# {sid}: {goal}\n\n## Narrative\n\nDid *things*.\n\n"
            f"## Captures\n\n{caps}\n")


def webp(path: Path):
    path.parent.mkdir(parents=True, exist_ok=True)
    Image.new("RGB", (8, 8), "gray").save(path, "WEBP")


@pytest.fixture
def repo(tmp_path):
    r = tmp_path / "repo"
    d = r / "docs"
    (d / "devlog").mkdir(parents=True)
    ids = [("2026-11-20-a", "first goal", ["cam-one"]),
           ("2026-11-22-b", "third <goal>", ["cam-one", "cam-two"]),
           ("2026-11-21-c", "second goal", ["cam-one"])]
    rows = ""
    for sid, goal, cams in ids:
        (d / "devlog" / f"{sid}-x.md").write_text(entry(sid, goal, cams, "https://example.com/c.mp4"
                                                        if sid.endswith("b") else "null"), "utf-8")
        for c in cams:
            webp(d / "devlog" / "media" / sid / f"{c}.webp")
            rows += f"| {sid[:10]} | {c} | docs/devlog/media/{sid}/{c}.webp | {SHA} |\n"
    (d / "devlog" / "TEMPLATE.md").write_text("---\nid: {{id}}\n---\n", "utf-8")
    (d / "captures").mkdir()
    (d / "captures" / "INDEX.md").write_text(INDEX_HEAD + rows, "utf-8")
    (d / "steam").mkdir()
    (d / "steam" / "BUILDS.md").write_text(
        "# Steam build ledger\n\n" + BUILDS_HEAD +
        "| 0.1.0 | 12345 | " + SHA + " | 2026-11-22T10:00:00Z | 2026-11-22T10:05:00Z | | | | first |\n",
        "utf-8")
    (d / "decisions").mkdir()
    (d / "decisions" / "0001-use-files.md").write_text(
        "# 0001: Use files\n\nStatus: Accepted\nDate: 2026-11-01\n\n## Context\n\nBecause.\n", "utf-8")
    (d / "OPEN-DECISIONS.md").write_text(
        "# Open decisions\n\n## Open\n\n| ID | Question | Why it matters | Blocks | Options known |"
        " GitHub Issue | Status |\n|---|---|---|---|---|---|---|\n"
        "| OD-001 | Core loop? | Matters | proto | a; b | [#1](https://github.com/x/y/issues/1)"
        " | Open |\n\n## Resolved\n\n| ID | Question | Decision record | GitHub Issue |\n"
        "|---|---|---|---|\n| OD-000 | Old? | [0001](decisions/0001-use-files.md) | - |\n", "utf-8")
    (d / "milestones").mkdir()
    (d / "milestones" / "m1-start.md").write_text(
        "# Milestone 1: start\n\n![](../../docs/devlog/media/2026-11-20-a/cam-one.webp)\n", "utf-8")
    return r


class Links(HTMLParser):
    def __init__(self):
        super().__init__()
        self.links = []

    def handle_starttag(self, tag, attrs):
        for k, v in attrs:
            if k in ("href", "src") and v:
                self.links.append(v)


def links_of(page: Path):
    p = Links()
    p.feed(page.read_text("utf-8"))
    return p.links


def test_builds_all_pages_from_fixture_repo(repo, tmp_path):
    out = tmp_path / "_site"
    site.build(repo, out)
    for rel in ["index.html", "gallery.html", "decisions.html", "builds.html",
                "devlog/2026-11-20-a.html", "devlog/2026-11-21-c.html",
                "devlog/2026-11-22-b.html", "static/site.css"]:
        assert (out / rel).is_file(), rel
    index = (out / "index.html").read_text("utf-8")
    assert index.index("2026-11-22-b") < index.index("2026-11-21-c") < index.index("2026-11-20-a")
    assert "third &lt;goal&gt;" in index and "<goal>" not in index
    assert "Did <em>things</em>" in (out / "devlog/2026-11-22-b.html").read_text("utf-8")
    assert "OD-001" in (out / "decisions.html").read_text("utf-8")
    assert "Use files" in (out / "decisions.html").read_text("utf-8")
    assert "12345" in (out / "builds.html").read_text("utf-8")


def test_empty_repo_builds_with_empty_state(tmp_path):
    r = tmp_path / "empty"
    (r / "docs" / "devlog").mkdir(parents=True)
    out = tmp_path / "_site"
    site.build(r, out)
    assert "No sessions yet" in (out / "index.html").read_text("utf-8")
    assert "No captures yet" in (out / "gallery.html").read_text("utf-8")
    assert "No builds yet" in (out / "builds.html").read_text("utf-8")
    assert (out / "decisions.html").is_file()


def test_gallery_orders_by_date_per_camera(repo, tmp_path):
    out = tmp_path / "_site"
    site.build(repo, out)
    html = (out / "gallery.html").read_text("utf-8")
    cams = {}
    for m in re.finditer(r'data-camera="([^"]+)" data-shots=\'([^\']*)\'', html):
        cams[m.group(1)] = json.loads(m.group(2).replace("&quot;", '"'))
    assert set(cams) == {"cam-one", "cam-two"}
    assert [s["date"] for s in cams["cam-one"]] == ["2026-11-20", "2026-11-21", "2026-11-22"]
    assert [s["date"] for s in cams["cam-two"]] == ["2026-11-22"]
    assert cams["cam-one"][0]["src"] == "media/2026-11-20-a/cam-one.webp"


def test_no_absolute_or_broken_internal_links(repo, tmp_path):
    out = tmp_path / "_site"
    site.build(repo, out)
    pages = list(out.rglob("*.html"))
    assert len(pages) >= 8
    for page in pages:
        for link in links_of(page):
            if re.match(r"(https?:|mailto:|#)", link):
                continue
            assert not link.startswith("/"), (page, link)
            target = (page.parent / link.split("#")[0]).resolve()
            assert target.exists(), (page.relative_to(out), link)


def test_site_contains_no_secret_findings(repo, tmp_path):
    out = tmp_path / "_site"
    site.build(repo, out)
    assert secrets.scan_paths([p for p in out.rglob("*") if p.is_file()]) == []


def test_no_external_scripts_or_fonts(repo, tmp_path):
    out = tmp_path / "_site"
    site.build(repo, out)
    for page in out.rglob("*.html"):
        text = page.read_text("utf-8")
        assert not re.search(r"<(script|link)[^>]+(src|href)=\"https?:", text)


def test_unsafe_entry_id_is_skipped(repo, tmp_path):
    text = entry("2026-11-23-e", "evil", []).replace("id: 2026-11-23-e", "id: ../../escape")
    (repo / "docs/devlog/evil.md").write_text(text, "utf-8")
    out = tmp_path / "site"
    site.build(repo, out)
    assert not (tmp_path / "escape.html").exists()
    assert not any("escape" in p.name for p in out.rglob("*.html"))


def test_non_http_clip_is_not_linked(repo, tmp_path):
    (repo / "docs/devlog/2026-11-23-d-x.md").write_text(
        entry("2026-11-23-d", "js clip", [], "javascript:alert(1)"), "utf-8")
    out = tmp_path / "site"
    site.build(repo, out)
    html = (out / "index.html").read_text("utf-8") + (out / "devlog/2026-11-23-d.html").read_text("utf-8")
    assert "javascript:" not in html
