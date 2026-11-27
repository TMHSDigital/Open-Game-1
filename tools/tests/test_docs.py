from pathlib import Path

from og import docs


def make_repo(tmp_path: Path) -> Path:
    r = tmp_path / "repo"
    (r / "docs" / "decisions").mkdir(parents=True)
    (r / "docs" / "devlog").mkdir(parents=True)
    (r / "docs" / "steam").mkdir(parents=True)
    (r / "docs" / "decisions" / "0001-first.md").write_text("# 0001\n", "utf-8")
    (r / "docs" / "devlog" / "2026-11-20-01-a.md").write_text("x\n", "utf-8")
    (r / "docs" / "steam" / "BUILDS.md").write_text("| version | id |\n|---|---|\n| 0.1.1 | 1 |\n| 0.1.2 | 2 |\n", "utf-8")
    (r / "README.md").write_text(
        "[d](docs/decisions/0001-first.md) Build 0.1.2 is live on the private development branch.\n[log](docs/devlog/2026-11-20-01-a.md)\n", "utf-8")
    return r


def test_current_readme_passes(tmp_path):
    assert docs.check(make_repo(tmp_path)) == []


def test_a_new_decision_or_build_the_readme_does_not_name_fails(tmp_path):
    r = make_repo(tmp_path)
    (r / "docs" / "decisions" / "0002-second.md").write_text("# 0002\n", "utf-8")
    with (r / "docs" / "steam" / "BUILDS.md").open("a", encoding="utf-8") as f:
        f.write("| 0.1.3 | 3 |\n")
    problems = docs.check(r)
    assert any("0002-second.md" in p for p in problems)
    assert any("0.1.3" in p for p in problems)


def test_a_broken_link_fails_but_the_session_in_progress_does_not(tmp_path):
    r = make_repo(tmp_path)
    readme = r / "README.md"
    # A newer entry than any written: the session in progress, whose entry end writes.
    readme.write_text(readme.read_text("utf-8") + "[now](docs/devlog/2026-11-21-01-now.md)\n", "utf-8")
    assert docs.check(r) == []
    # An older entry that is missing is a broken link.
    readme.write_text(readme.read_text("utf-8") + "[old](docs/devlog/2026-11-19-01-gone.md)\n[x](docs/NOPE.md)\n", "utf-8")
    problems = docs.check(r)
    assert any("2026-11-19-01-gone.md" in p for p in problems)
    assert any("docs/NOPE.md" in p for p in problems)


def test_external_links_anchors_and_code_blocks_are_not_checked(tmp_path):
    r = make_repo(tmp_path)
    readme = r / "README.md"
    readme.write_text(readme.read_text("utf-8")
                      + "[web](https://example.org/x.md) [top](#status) [abs](/x)\n```\n[code](missing.md)\n```\n", "utf-8")
    assert docs.check(r) == []


def test_no_readme_nothing_to_check(tmp_path):
    r = make_repo(tmp_path)
    (r / "README.md").unlink()
    assert docs.check(r) == []


def test_a_readme_worded_so_the_release_cannot_update_it_fails(tmp_path):
    r = make_repo(tmp_path)
    (r / "README.md").write_text(
        "[d](docs/decisions/0001-first.md) Build 0.1.1 is live on default; build 0.1.2 is on the private "
        "development branch.\n[log](docs/devlog/2026-11-20-01-a.md)\n", "utf-8")
    problems = docs.check(r)
    assert any("Build 0.1.2 is live on the private development branch" in p for p in problems)
