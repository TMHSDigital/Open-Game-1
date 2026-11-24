import os
import subprocess
import sys
from pathlib import Path

import pytest

from og.secrets import PRAGMA, scan_image, scan_paths, scan_text

TOOLS = Path(__file__).resolve().parents[1]
# Fixtures are built by concatenation so this file never contains a literal secret.
KEY = "0123456789abcdef" * 2
GHP = "ghp_" + "a" * 36
MAIL = "someone@" + "gmail.com"
VALVE = "Valve " + "Confidential"


def rules(text, path="a.txt"):
    return {f.rule for f in scan_text(text, path)}


def og(*args, cwd):
    env = {**os.environ, "PYTHONPATH": str(TOOLS)}
    return subprocess.run([sys.executable, "-m", "og", *args], cwd=cwd,
                          capture_output=True, text=True, env=env)


def png(path, **info):
    from PIL import Image, PngImagePlugin
    meta = PngImagePlugin.PngInfo()
    for k, v in info.items():
        meta.add_text(k, v)
    Image.new("RGB", (4, 4)).save(path, pnginfo=meta)
    return path


def test_flags_steam_web_api_key():
    assert "steam-api-key" in rules("STEAM_API_KEY=" + KEY)


def test_flags_appid_near_steam_keyword():
    assert "steam-id" in rules('"appid" "' + "1234567" + '"', "app_build.vdf")
    assert "steam-id" in rules("STEAM_APPID=" + "1234567", ".env")  # og-secrets: allow
    assert "steam-id" not in rules("STEAM_APPID=")
    assert "steam-id" not in rules("STEAM_APPID=480")


@pytest.mark.parametrize("line", [
    "STEAM_APPID: " + "1234567",
    '"STEAM_APPID": "' + "1234567" + '"',
    "STEAM_DEPOT_WIN = '" + "1234568" + "'",
])
def test_steam_id_yaml_and_json_forms(line):
    assert "steam-id" in rules(line)


def test_flags_partner_cookie_and_github_token():
    assert "steam-cookie" in rules("steamLoginSecure=abc123")  # og-secrets: allow
    assert "github-token" in rules("token " + GHP)


def test_flags_valve_confidential_text(tmp_path):
    assert "valve-confidential" in rules(VALVE)
    img = png(tmp_path / "shot.png")
    (tmp_path / "shot.ocr.txt").write_text("Steamworks - " + VALVE)
    assert "valve-confidential" in {f.rule for f in scan_image(img)}
    assert "valve-confidential" in {f.rule for f in scan_paths([img])}


def test_flags_private_email_but_not_noreply():
    assert "private-email" in rules("contact " + MAIL)
    assert "private-email" not in rules("noreply@anthropic.com")
    assert "private-email" not in rules("1+name@users.noreply.github.com")


def test_scan_image_reads_exif(tmp_path):
    from PIL import Image
    img = tmp_path / "a.jpg"
    im = Image.new("RGB", (4, 4))
    exif = im.getexif()
    exif[0x010E] = "STEAM_APPID=" + "1234567"  # og-secrets: allow
    im.save(img, exif=exif)
    assert "steam-id" in {f.rule for f in scan_image(img)}


def test_scan_image_reads_png_text_chunk(tmp_path):
    img = png(tmp_path / "a.png", Comment="STEAM_APPID=" + "1234567")  # og-secrets: allow
    assert "steam-id" in {f.rule for f in scan_image(img)}


def test_clean_image_has_no_findings(tmp_path):
    assert scan_image(png(tmp_path / "ok.png")) == []


def test_truncated_image_is_a_finding_not_a_pass(tmp_path):
    from PIL import Image
    good = tmp_path / "good.png"
    Image.effect_noise((64, 64), 80).convert("RGB").save(good)
    bad = tmp_path / "bad.png"
    data = good.read_bytes()
    bad.write_bytes(data[: len(data) // 2])
    assert "unscannable-image" in {f.rule for f in scan_image(bad)}


def test_not_an_image_is_skipped(tmp_path):
    fake = tmp_path / "fake.png"
    fake.write_bytes(b"not really a png")
    assert scan_image(fake) == []


def test_pragma_suppresses_only_its_line():
    text = f"STEAM_APPID={'1234567'}  # {PRAGMA}\nSTEAM_APPID={'7654321'}"  # og-secrets: allow
    found = scan_text(text, "tools/tests/fixture.py")
    assert [f.line for f in found] == [2]


def test_pragma_is_ignored_outside_fixture_files():
    text = f"STEAM_APPID={'1234567'}  <!-- {PRAGMA} -->"  # og-secrets: allow
    assert [f.rule for f in scan_text(text, "docs/devlog/entry.md")] == ["steam-id"]


def test_excerpt_is_redacted():
    text = (f"STEAM_API_KEY={KEY}\n{GHP}\n{MAIL}\n"
            "steamLoginSecure=supersecretvalue")  # og-secrets: allow
    findings = scan_text(text, "x.txt")
    assert {f.rule for f in findings} >= {"steam-api-key", "github-token",
                                          "private-email", "steam-cookie"}
    for f in findings:
        for secret in (KEY, GHP, MAIL, "supersecretvalue"):
            assert secret not in f.excerpt_redacted


def test_utf16_file_is_scanned(tmp_path):
    f = tmp_path / "ps.txt"
    f.write_bytes(("STEAM_APPID=" + "1234567\r\n").encode("utf-16"))  # with BOM  # og-secrets: allow
    assert "steam-id" in {x.rule for x in scan_paths([f])}
    g = tmp_path / "le.txt"
    g.write_bytes(("token " + GHP).encode("utf-16-le"))  # no BOM
    assert "github-token" in {x.rule for x in scan_paths([g])}


def test_binary_with_nuls_is_still_scanned(tmp_path):
    f = tmp_path / "blob.bin"
    f.write_bytes(b"\x00\x01\x02" * 50 + ("token " + GHP).encode() + b"\x00" * 9)
    assert "github-token" in {x.rule for x in scan_paths([f])}


def test_cli_exit_codes(tmp_path):
    clean = tmp_path / "clean.txt"
    clean.write_text("hello")
    dirty = tmp_path / "dirty.txt"
    dirty.write_text("STEAM_APPID=" + "1234567")  # og-secrets: allow
    assert og("secrets", str(clean), cwd=tmp_path).returncode == 0
    r = og("secrets", str(dirty), cwd=tmp_path)
    assert r.returncode == 1
    assert "1234567" not in r.stdout
    assert og("secrets", "--selftest", cwd=tmp_path).returncode == 0


def git(cwd, *args):
    subprocess.run(["git", *args], cwd=cwd, check=True, capture_output=True)


def test_staged_scans_index_not_working_tree(tmp_path):
    git(tmp_path, "init", "-q")
    f = tmp_path / "leak.txt"
    f.write_text("STEAM_APPID=" + "1234567")  # og-secrets: allow
    git(tmp_path, "add", "leak.txt")
    f.write_text("clean now")  # working tree is clean, index is not
    r = og("secrets", "--staged", cwd=tmp_path)
    assert r.returncode == 1
    assert "leak.txt" in r.stdout and "steam-id" in r.stdout
    # and the reverse: clean index, dirty working tree passes
    f.write_text("clean now")
    git(tmp_path, "add", "leak.txt")
    f.write_text("STEAM_APPID=" + "1234567")  # og-secrets: allow
    assert og("secrets", "--staged", cwd=tmp_path).returncode == 0


def test_staged_scans_image_metadata(tmp_path):
    git(tmp_path, "init", "-q")
    img = png(tmp_path / "s.png", Comment="STEAM_APPID=" + "1234567")  # og-secrets: allow
    git(tmp_path, "add", "s.png")
    assert og("secrets", "--staged", cwd=tmp_path).returncode == 1


def test_staged_rename_with_edit_is_scanned(tmp_path):
    git(tmp_path, "init", "-q")
    old = tmp_path / "old.txt"
    old.write_text("line\n" * 20)
    git(tmp_path, "add", "old.txt")
    git(tmp_path, "-c", "user.name=t", "-c", "user.email=t@example.com",
        "commit", "-q", "-m", "x")
    git(tmp_path, "mv", "old.txt", "new.txt")
    (tmp_path / "new.txt").write_text("line\n" * 20 + "STEAM_APPID=" + "1234567\n")  # og-secrets: allow
    git(tmp_path, "add", "new.txt")
    r = og("secrets", "--staged", cwd=tmp_path)
    assert r.returncode == 1 and "new.txt" in r.stdout


def stage_lfs_pointer(repo, name, oid, size):
    pointer = repo / "pointer.tmp"
    pointer.write_text("version https://git-lfs.github.com/spec/v1\noid sha256:" + oid + "\nsize " + str(size) + "\n")
    h = subprocess.run(["git", "hash-object", "-w", str(pointer)], cwd=repo,
                       capture_output=True, text=True, check=True).stdout.strip()
    pointer.unlink()
    git(repo, "update-index", "--add", "--cacheinfo", f"100644,{h},{name}")


def sha(path):
    import hashlib
    return hashlib.sha256(path.read_bytes()).hexdigest()


def test_staged_lfs_image_matching_oid_is_scanned(tmp_path):
    git(tmp_path, "init", "-q")
    (tmp_path / "Art").mkdir()
    img = png(tmp_path / "Art" / "x.png", Comment="STEAM_APPID=" + "1234567")  # og-secrets: allow
    stage_lfs_pointer(tmp_path, "Art/x.png", sha(img), img.stat().st_size)
    r = og("secrets", "--staged", cwd=tmp_path)
    assert r.returncode == 1 and "steam-id" in r.stdout


def test_staged_lfs_clean_image_with_matching_oid_passes(tmp_path):
    git(tmp_path, "init", "-q")
    (tmp_path / "Art").mkdir()
    img = png(tmp_path / "Art" / "x.png")
    stage_lfs_pointer(tmp_path, "Art/x.png", sha(img), img.stat().st_size)
    assert og("secrets", "--staged", cwd=tmp_path).returncode == 0


def test_staged_lfs_oid_mismatch_fails_closed(tmp_path):
    git(tmp_path, "init", "-q")
    (tmp_path / "Art").mkdir()
    img = png(tmp_path / "Art" / "x.png")
    stage_lfs_pointer(tmp_path, "Art/x.png", "0" * 64, img.stat().st_size)
    r = og("secrets", "--staged", cwd=tmp_path)
    assert r.returncode == 1 and "unscannable-lfs" in r.stdout


def test_staged_lfs_size_mismatch_fails_closed(tmp_path):
    git(tmp_path, "init", "-q")
    img = png(tmp_path / "x.png")
    stage_lfs_pointer(tmp_path, "x.png", sha(img), img.stat().st_size + 1)
    assert "unscannable-lfs" in og("secrets", "--staged", cwd=tmp_path).stdout


def test_staged_lfs_missing_working_file_fails_closed(tmp_path):
    git(tmp_path, "init", "-q")
    stage_lfs_pointer(tmp_path, "Art/gone.png", "0" * 64, 10)
    r = og("secrets", "--staged", cwd=tmp_path)
    assert r.returncode == 1 and "unscannable-lfs" in r.stdout


def test_staged_lfs_non_image_is_scanned(tmp_path):
    git(tmp_path, "init", "-q")
    f = tmp_path / "data.wav"
    f.write_bytes(b"RIFF\x00\x00" + ("token " + GHP).encode() + b"\x00\x00")
    stage_lfs_pointer(tmp_path, "data.wav", sha(f), f.stat().st_size)
    r = og("secrets", "--staged", cwd=tmp_path)
    assert r.returncode == 1 and "github-token" in r.stdout


def test_staged_from_subdirectory_resolves_from_repo_root(tmp_path):
    git(tmp_path, "init", "-q")
    (tmp_path / "Art").mkdir()
    img = png(tmp_path / "Art" / "x.png", Comment="STEAM_APPID=" + "1234567")  # og-secrets: allow
    stage_lfs_pointer(tmp_path, "Art/x.png", sha(img), img.stat().st_size)
    r = og("secrets", "--staged", cwd=tmp_path / "Art")
    assert r.returncode == 1 and "steam-id" in r.stdout


def test_staged_image_sidecar_is_scanned(tmp_path):
    git(tmp_path, "init", "-q")
    png(tmp_path / "s.png")
    (tmp_path / "s.ocr.txt").write_text("Steamworks " + "Valve " + "Confidential")
    git(tmp_path, "add", "s.png")  # sidecar only in the working tree
    r = og("secrets", "--staged", cwd=tmp_path)
    assert r.returncode == 1 and "valve-confidential" in r.stdout


def commit_all(repo):
    git(repo, "add", "-A")
    git(repo, "-c", "user.name=t", "-c", "user.email=t@example.com",
        "-c", "core.hooksPath=none", "commit", "-q", "-m", "x")


def test_tracked_scans_every_tracked_file(tmp_path):
    git(tmp_path, "init", "-q")
    (tmp_path / "clean.txt").write_text("hello")
    (tmp_path / "docs").mkdir()
    (tmp_path / "docs" / "notes.md").write_text("token " + GHP)
    commit_all(tmp_path)
    r = og("secrets", "--tracked", cwd=tmp_path)
    assert r.returncode == 1 and "docs/notes.md" in r.stdout and "github-token" in r.stdout


def test_tracked_clean_repo_passes_and_ignores_untracked(tmp_path):
    git(tmp_path, "init", "-q")
    (tmp_path / "clean.txt").write_text("hello")
    commit_all(tmp_path)
    (tmp_path / "untracked.txt").write_text("token " + GHP)
    r = og("secrets", "--tracked", cwd=tmp_path)
    assert r.returncode == 0, r.stdout + r.stderr


def test_tracked_from_subdirectory_scans_whole_repo(tmp_path):
    git(tmp_path, "init", "-q")
    (tmp_path / "sub").mkdir()
    (tmp_path / "sub" / "a.txt").write_text("fine")
    (tmp_path / "leak.txt").write_text("STEAM_APPID=" + "1234567")  # og-secrets: allow
    commit_all(tmp_path)
    r = og("secrets", "--tracked", cwd=tmp_path / "sub")
    assert r.returncode == 1 and "leak.txt" in r.stdout


def test_tracked_lfs_pointer_without_content_fails_closed(tmp_path):
    git(tmp_path, "init", "-q")
    stage_lfs_pointer(tmp_path, "Art/gone.png", "0" * 64, 10)
    r = og("secrets", "--tracked", cwd=tmp_path)
    assert r.returncode == 1 and "unscannable-lfs" in r.stdout


def test_tracked_lfs_content_is_scanned(tmp_path):
    git(tmp_path, "init", "-q")
    (tmp_path / "Art").mkdir()
    img = png(tmp_path / "Art" / "x.png", Comment="STEAM_APPID=" + "1234567")  # og-secrets: allow
    stage_lfs_pointer(tmp_path, "Art/x.png", sha(img), img.stat().st_size)
    r = og("secrets", "--tracked", cwd=tmp_path)
    assert r.returncode == 1 and "steam-id" in r.stdout


def test_index_blob_read_failure_is_a_finding(tmp_path):
    from og.secrets import _index_blob
    git(tmp_path, "init", "-q")
    data, bad = _index_blob("not-in-index.txt", tmp_path)
    assert data is None and bad is not None and bad.rule == "unscannable-blob"


def test_build_account_literal_is_flagged():
    assert "steam-build-account" in rules("STEAM_BUILD_ACCOUNT=" + "builder_name")
    assert "steam-build-account" in rules('"STEAM_BUILD_ACCOUNT": "' + "builder_name" + '"')
    assert "steam-build-account" not in rules("STEAM_BUILD_ACCOUNT=")
    assert "steam-build-account" not in rules("STEAM_BUILD_ACCOUNT=\n")
    assert "steam-build-account" not in rules("+login $env:STEAM_BUILD_ACCOUNT +quit")


def test_tracked_lfs_pointers_ok_accepts_missing_content_but_scans_the_rest(tmp_path):
    git(tmp_path, "init", "-q")
    stage_lfs_pointer(tmp_path, "Art/gone.png", "0" * 64, 10)
    strict = og("secrets", "--tracked", cwd=tmp_path)
    assert strict.returncode == 1 and "unscannable-lfs" in strict.stdout
    ok = og("secrets", "--tracked", "--lfs-pointers-ok", cwd=tmp_path)
    assert ok.returncode == 0, ok.stdout + ok.stderr
    (tmp_path / "leak.txt").write_text("token " + GHP)
    git(tmp_path, "add", "leak.txt")
    r = og("secrets", "--tracked", "--lfs-pointers-ok", cwd=tmp_path)
    assert r.returncode == 1 and "leak.txt" in r.stdout and "unscannable-lfs" not in r.stdout


def test_binary_asset_skips_email_rule_only(tmp_path):
    noise = bytes([0, 0x93]) + b"j@A.C" + bytes([7]) + ("contact " + "someone@" + "gmail.com").encode()  # og-secrets: allow
    asset = tmp_path / "SK_Thing.uasset"
    asset.write_bytes(noise + bytes([0]) + ("token " + GHP).encode())
    rules = {x.rule for x in scan_paths([asset])}
    assert "private-email" not in rules
    assert "github-token" in rules


def test_email_rule_still_applies_to_text_and_unknown_binaries(tmp_path):
    for name in ("notes.txt", "blob.bin"):
        f = tmp_path / name
        f.write_bytes(bytes([0]) + b"x " + ("someone@" + "gmail.com").encode())
        assert "private-email" in {x.rule for x in scan_paths([f])}


def test_cli_prints_non_cp1252_excerpt_without_crashing(tmp_path):
    f = tmp_path / "dirty.txt"
    f.write_text(chr(0x2603) + " token " + GHP, encoding="utf-8")
    env = dict(os.environ, PYTHONIOENCODING="cp1252", PYTHONPATH=str(TOOLS))
    r = subprocess.run([sys.executable, "-m", "og", "secrets", str(f)], cwd=tmp_path,
                       capture_output=True, text=True, encoding="cp1252", errors="replace", env=env)
    assert r.returncode == 1
    assert "Traceback" not in r.stderr


def test_directory_is_scanned_recursively(tmp_path):
    sub = tmp_path / "d" / "deep"
    sub.mkdir(parents=True)
    (sub / "a.txt").write_text("STEAM_API_KEY=" + "0123456789abcdef" * 2)
    assert [f.rule for f in scan_paths([tmp_path / "d"])] == ["steam-api-key"]


def test_missing_path_is_a_finding_not_a_pass(tmp_path):
    assert [f.rule for f in scan_paths([tmp_path / "nope.txt"])] == ["unscannable-path"]


def test_cli_with_nothing_to_scan_exits_2(tmp_path):
    r = og("secrets", cwd=tmp_path)
    assert r.returncode == 2
    assert "nothing to scan" in r.stderr


FAKE_ID = "1234" + "567"


@pytest.mark.parametrize("line", [
    "https://store.steampowered.com/app/" + FAKE_ID + "/Some_Game/",
    "https://steamdb.info/app/" + FAKE_ID + "/",
    "https://partner.steamgames.com/apps/landing/" + FAKE_ID,
    "steam://run/" + FAKE_ID,
    "steam://rungameid/" + FAKE_ID,
    "Successfully finished AppID " + FAKE_ID + " build (BuildID 99)",
    "Building depot " + FAKE_ID + "...",
    "AppID: " + FAKE_ID,
    "appid=" + FAKE_ID,
    '"appid" ' + FAKE_ID,
    "DepotID " + FAKE_ID,
    "Logging in user 'some" + "builder' [U:1:" + "12345678] to Steam Public...OK",  # og-secrets: allow
    "steamcmd +login some" + "builder +run_app_build x.vdf",  # og-secrets: allow
    "github_pat_" + "A1b2" * 8,
    "-----BEGIN OPENSSH " + "PRIVATE KEY-----",
    "AKIA" + "ABCDEFGHIJKLMNOP",
])
def test_flags_leak_shapes_outside_assignments(line):
    assert scan_text(line, "docs/x.md"), line


@pytest.mark.parametrize("line", [
    "https://store.steampowered.com/app/480/Spacewar/",
    "steam://run/480",
    "AppID 480",
    "steamcmd +login anonymous +app_update 480",
    "BuildID 26879552 is live on development",
    "the depot holds 2 files",
    "see steam://open/console",
])
def test_public_and_ordinary_steam_text_is_not_flagged(line):
    assert scan_text(line, "docs/x.md") == [], line


def test_local_values_are_flagged_in_any_form(monkeypatch):
    from og import secrets
    monkeypatch.setenv("STEAM_BUILD_ACCOUNT", "zq" + "localacct")
    monkeypatch.setattr(secrets, "_local_rx", None)
    found = scan_text("uploaded by zq" + "localacct yesterday", "docs/x.md")
    monkeypatch.setattr(secrets, "_local_rx", None)
    assert [f.rule for f in found] == ["steam-local-value"]
    assert "localacct" not in found[0].excerpt_redacted
