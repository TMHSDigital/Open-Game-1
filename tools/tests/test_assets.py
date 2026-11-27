import csv
import json
import os
import subprocess
import sys
from pathlib import Path

import pytest

from og.assets import (HEADER, check, license_verdict, normalize_license,
                       read_rows, render_credits)

TOOLS = Path(__file__).resolve().parents[1]
REPO = TOOLS.parent


def make_project(cwd):
    """A git repository the CLI accepts as this project (og.repo.MARKERS)."""
    cwd = Path(cwd)
    if not (cwd / ".git").exists():
        subprocess.run(["git", "init", "-q"], cwd=cwd, check=True)
    (cwd / "tools" / "og").mkdir(parents=True, exist_ok=True)
    (cwd / "docs").mkdir(exist_ok=True)
    (cwd / "docs" / "OPEN-DECISIONS.md").touch()


def og(*args, cwd):
    make_project(cwd)
    env = {**os.environ, "PYTHONPATH": str(TOOLS)}
    return subprocess.run([sys.executable, "-m", "og", *args], cwd=cwd,
                          capture_output=True, text=True, env=env)


def row(**kw):
    base = dict(id="a", name="Asset A", source_url="https://example.com/a",
                author="Ann", license="CC-BY-4.0",
                license_url="https://example.com/license", verified="2026-11-20",
                credit="Asset A by Ann (CC BY 4.0)", local_paths="", catalog_id="")
    base.update(kw)
    return base


def write_ledger(repo, rows):
    (repo / "assets").mkdir(parents=True, exist_ok=True)
    with open(repo / "assets" / "THIRD_PARTY.csv", "w", newline="", encoding="utf-8") as f:
        w = csv.DictWriter(f, fieldnames=HEADER)
        w.writeheader()
        w.writerows(rows)


def touch(repo, rel):
    p = repo / rel
    p.parent.mkdir(parents=True, exist_ok=True)
    p.write_bytes(b"x")


def test_normalize_variants():
    assert normalize_license("cc-by-sa-4.0") == "CC-BY-SA-4.0"
    assert normalize_license("CC BY-NC 4.0") == "CC-BY-NC-4.0"
    assert normalize_license("GPL-3.0-or-later") == "GPL-3.0-OR-LATER"
    assert normalize_license("CC BY 4.0") == "CC-BY-4.0"


@pytest.mark.parametrize("lic", ["CC0-1.0", "CC-BY-4.0", "MIT", "OFL-1.1",
                                 "cc by 3.0", "Apache-2.0", "BSD-3-Clause", "public domain"])
def test_allowed(lic):
    assert license_verdict(normalize_license(lic)) == "allowed"


@pytest.mark.parametrize("lic", ["CC-BY-SA-4.0", "CC-BY-NC-4.0", "CC-BY-ND-4.0",
                                 "GPL-3.0-OR-LATER", "LGPL-2.1", "AGPL-3.0", "",
                                 "cc-by-sa-4.0", "CC BY-NC 4.0", "CC-BY-NC-SA-4.0", "   "])
def test_refused(lic):
    assert license_verdict(normalize_license(lic)) == "refused"


def test_unknown_license_is_problem(tmp_path):
    assert license_verdict(normalize_license("Custom-Foo")) == "unknown"
    write_ledger(tmp_path, [row(license="Custom-Foo")])
    problems = check(tmp_path)
    assert any("Custom-Foo" in p and "unknown" in p for p in problems)


def test_refused_and_blank_license_are_problems(tmp_path):
    write_ledger(tmp_path, [row(id="a", license="CC BY-NC 4.0"), row(id="b", license="")])
    problems = check(tmp_path)
    assert any(p.startswith("a:") and "refused" in p for p in problems)
    assert any(p.startswith("b:") and "refused" in p for p in problems)


def test_clean_ledger_passes(tmp_path):
    write_ledger(tmp_path, [row(local_paths="Art/ThirdParty/pack/*;Game/Content/ThirdParty/**")])
    touch(tmp_path, "Art/ThirdParty/pack/t.png")
    touch(tmp_path, "Game/Content/ThirdParty/deep/x.uasset")
    assert check(tmp_path) == []


def test_untracked_thirdparty_file_fails(tmp_path):
    write_ledger(tmp_path, [])
    touch(tmp_path, "Game/Content/ThirdParty/x.uasset")
    touch(tmp_path, "Game/Plugins/Foo/Content/ThirdParty/y.uasset")
    touch(tmp_path, "Art/ThirdParty/z.png")
    problems = check(tmp_path)
    for name in ("Game/Content/ThirdParty/x.uasset",
                 "Game/Plugins/Foo/Content/ThirdParty/y.uasset", "Art/ThirdParty/z.png"):
        assert any(name in p for p in problems)


def test_missing_ledger_with_thirdparty_file_fails(tmp_path):
    touch(tmp_path, "Art/ThirdParty/z.png")
    assert check(tmp_path)


def test_missing_ledger_without_files_ok(tmp_path):
    assert check(tmp_path) == []


def test_bad_header_and_duplicate_id(tmp_path):
    (tmp_path / "assets").mkdir()
    (tmp_path / "assets" / "THIRD_PARTY.csv").write_text("id,name\n", encoding="utf-8")
    assert any("header" in p for p in check(tmp_path))
    write_ledger(tmp_path, [row(), row()])
    assert any("duplicate" in p for p in check(tmp_path))


def test_credits_generated_from_rows():
    rows = [row(id="a", credit="Line A", license="CC-BY-4.0"),
            row(id="b", credit="Line B", license="CC0-1.0"),
            row(id="c", credit="Line C", license="cc by 4.0")]
    out = render_credits(rows)
    assert out.index("## CC-BY-4.0") < out.index("Line A") < out.index("Line C")
    assert out.index("## CC0-1.0") < out.index("Line B")
    assert out.count("## CC-BY-4.0") == 1
    assert out.endswith("\n")


def test_credits_zero_rows():
    out = render_credits([])
    assert out.startswith("# Credits")
    assert "No third-party assets yet." in out


def test_credits_md_up_to_date():
    expected = render_credits(read_rows(REPO))
    actual = (REPO / "CREDITS.md").read_text(encoding="utf-8").replace("\r\n", "\n")
    assert actual == expected


def test_real_repo_check_passes():
    assert check(REPO) == []


CATALOG = {"entries": [
    {"id": "good-pack", "name": "Good Pack", "url": "https://example.com/good",
     "license": "CC-BY-4.0", "license_spdx": "CC-BY-4.0",
     "license_url": "https://example.com/good/license", "publisher": "Pat",
     "attribution_string": "Good Pack by Pat, CC BY 4.0", "verified": "2026-11-18"},
    {"id": "font-pack", "name": "Font Pack", "url": "https://example.com/font",
     "license": "SIL OFL", "license_spdx": "OFL-1.1", "verified": "2026-11-01"},
    {"id": "bad-pack", "name": "Bad Pack", "url": "https://example.com/bad",
     "license": "CC-BY-NC-4.0", "license_spdx": "CC-BY-NC-4.0", "verified": "2026-11-18"},
]}


def test_add_from_catalog(tmp_path):
    cat = tmp_path / "data.json"
    cat.write_text(json.dumps(CATALOG), encoding="utf-8")
    repo = tmp_path / "repo"
    repo.mkdir()
    r = og("assets", "add", "--from-catalog", "good-pack", "--catalog", str(cat),
           "--paths", "Art/ThirdParty/good/*", cwd=repo)
    assert r.returncode == 0, r.stderr
    rows = read_rows(repo)
    assert rows[0]["id"] == "good-pack" and rows[0]["catalog_id"] == "good-pack"
    assert rows[0]["license"] == "CC-BY-4.0"
    assert rows[0]["credit"] == "Good Pack by Pat, CC BY 4.0"
    assert rows[0]["local_paths"] == "Art/ThirdParty/good/*"
    # duplicate refused
    r = og("assets", "add", "--from-catalog", "good-pack", "--catalog", str(cat), cwd=repo)
    assert r.returncode != 0
    # fallback credit, OFL allowed
    r = og("assets", "add", "--from-catalog", "font-pack", "--catalog", str(cat), cwd=repo)
    assert r.returncode == 0, r.stderr
    assert "Font Pack" in read_rows(repo)[1]["credit"]
    # refused license never lands in the ledger
    r = og("assets", "add", "--from-catalog", "bad-pack", "--catalog", str(cat), cwd=repo)
    assert r.returncode != 0 and len(read_rows(repo)) == 2
    # unknown id
    r = og("assets", "add", "--from-catalog", "nope", "--catalog", str(cat), cwd=repo)
    assert r.returncode != 0


def test_cli_check_and_credits(tmp_path):
    write_ledger(tmp_path, [row()])
    assert og("assets", "check", cwd=tmp_path).returncode == 0
    assert og("assets", "credits", cwd=tmp_path).returncode == 0
    assert "Asset A by Ann" in (tmp_path / "CREDITS.md").read_text(encoding="utf-8")
    touch(tmp_path, "Art/ThirdParty/z.png")
    r = og("assets", "check", cwd=tmp_path)
    assert r.returncode == 1 and "Art/ThirdParty/z.png" in r.stdout


@pytest.mark.parametrize("lic", ["BSD-2-Clause", "BSD-3-Clause", "BSD-3-Clause-Clear", "0BSD",
                                 "bsd 3-clause"])
def test_bsd_spdx_ids_allowed(lic):
    assert license_verdict(normalize_license(lic)) == "allowed"


@pytest.mark.parametrize("lic", ["BSD-Non-Commercial", "BSD-4-Clause-Shortened", "BSD",
                                 "BSD-Protection", "BSD-anything"])
def test_other_bsd_strings_not_allowed(lic):
    assert license_verdict(normalize_license(lic)) in ("refused", "unknown")
