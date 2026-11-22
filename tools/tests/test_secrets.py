from og.secrets import scan_text, scan_image, scan_paths


def rules(text, path="a.txt"):
    return {f.rule for f in scan_text(text, path)}


def test_flags_steam_web_api_key():
    key = "0123456789abcdef0123456789ABCDEF"
    assert "steam-api-key" in rules(f"STEAM_API_KEY={key}")


def test_flags_appid_near_steam_keyword():
    assert "steam-id" in rules('"appid" "1234567"', "app_build.vdf")
    assert "steam-id" in rules("STEAM_APPID=1234567", ".env")
    assert "steam-id" not in rules("STEAM_APPID=")
    assert "steam-id" not in rules("STEAM_APPID=480")


def test_flags_partner_cookie_and_github_token():
    assert "steam-cookie" in rules("steamLoginSecure=abc123")
    assert "github-token" in rules("token ghp_" + "a" * 36)


def test_flags_valve_confidential_text(tmp_path):
    assert "valve-confidential" in rules("Valve Confidential")
    img = tmp_path / "shot.png"
    img.write_bytes(b"not really a png")
    (tmp_path / "shot.ocr.txt").write_text("Steamworks - Valve Confidential")
    assert "valve-confidential" in {f.rule for f in scan_image(img)}
    assert "valve-confidential" in {f.rule for f in scan_paths([img])}


def test_flags_private_email_but_not_noreply():
    assert "private-email" in rules("contact someone@gmail.com")
    assert "private-email" not in rules("noreply@anthropic.com")
    assert "private-email" not in rules("1+name@users.noreply.github.com")


def test_scan_image_reads_exif(tmp_path):
    from PIL import Image
    img = tmp_path / "a.jpg"
    im = Image.new("RGB", (4, 4))
    exif = im.getexif()
    exif[0x010E] = "STEAM_APPID=1234567"
    im.save(img, exif=exif)
    assert "steam-id" in {f.rule for f in scan_image(img)}


def test_excerpt_is_redacted():
    key = "0123456789abcdef0123456789ABCDEF"
    token = "ghp_" + "b" * 36
    mail = "someone@gmail.com"
    text = f"STEAM_API_KEY={key}\n{token}\n{mail}\nsteamLoginSecure=supersecretvalue"
    findings = scan_text(text, "x.txt")
    assert findings
    for f in findings:
        for secret in (key, token, mail, "supersecretvalue"):
            assert secret not in f.excerpt_redacted
