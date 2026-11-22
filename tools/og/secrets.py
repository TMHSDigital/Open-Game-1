"""Public-repo secrets scanner: regex rules over text, plus image metadata and OCR sidecars."""
from __future__ import annotations

import re
import subprocess
import sys
from dataclasses import dataclass
from pathlib import Path

IMAGE_SUFFIXES = {".png", ".jpg", ".jpeg", ".webp", ".gif", ".bmp", ".tif", ".tiff"}
# Files that must contain fake secrets as test data.
SELF_PATHS = {"tools/og/secrets.py", "tools/tests/test_secrets.py"}
EXCERPT_MAX = 120

EMAIL_RE = re.compile(r"(?P<s>[A-Za-z0-9._%+-]+@[A-Za-z0-9-]+(?:\.[A-Za-z0-9-]+)+)")
ALLOWED_EMAILS = (
    re.compile(r"@users\.noreply\.github\.com$", re.I),
    re.compile(r"^noreply@anthropic\.com$", re.I),
    re.compile(r"@example\.(com|org|net)$", re.I),
)

# (rule, regex). The named group "s" is the secret span that gets redacted.
RULES = [
    ("steam-api-key",
     re.compile(r"STEAM_API_KEY\s*[=:]\s*[\"']?(?P<s>[0-9A-Fa-f]{32})\b")),
    ("steam-id",
     re.compile(r"STEAM_(?:APPID|DEPOT\w*)\s*=\s*[\"']?(?P<s>(?!480\b)\d{4,})")),
    ("steam-id",
     re.compile(r"[\"'](?:appid|depotid)[\"']\s+[\"'](?P<s>(?!480[\"'])\d{4,})[\"']", re.I)),
    ("steam-id",
     re.compile(r"steam.{0,40}?app\s*id\D{0,6}(?P<s>\d{5,})", re.I)),
    ("steam-cookie",
     re.compile(r"(?:steamLoginSecure|steamMachineAuth\w*)\s*[=:]\s*(?P<s>\S+)")),
    ("github-token",
     re.compile(r"\b(?P<s>gh[pousr]_[A-Za-z0-9]{36,})")),
    ("valve-confidential",
     re.compile(r"(?P<s>Valve\s+Confidential)", re.I)),
]


@dataclass(frozen=True)
class Finding:
    path: str
    line: int
    rule: str
    excerpt_redacted: str


def _mask(secret: str) -> str:
    return secret[:2] + "***" if len(secret) > 4 else "***"


def _spans(line: str):
    """Yield (rule, start, end) for every secret span on a line."""
    for rule, rx in RULES:
        for m in rx.finditer(line):
            yield rule, m.start("s"), m.end("s")
    for m in EMAIL_RE.finditer(line):
        if not any(a.search(m.group("s")) for a in ALLOWED_EMAILS):
            yield "private-email", m.start("s"), m.end("s")


def _redact(line: str, spans) -> str:
    out, pos = [], 0
    for start, end in sorted({(s, e) for _, s, e in spans}):
        if start < pos:
            continue
        out.append(line[pos:start])
        out.append(_mask(line[start:end]))
        pos = end
    out.append(line[pos:])
    text = "".join(out).strip()
    return text if len(text) <= EXCERPT_MAX else text[:EXCERPT_MAX] + "..."


def scan_text(text: str, path: str) -> list[Finding]:
    findings = []
    for lineno, line in enumerate(text.splitlines(), 1):
        spans = list(_spans(line))
        if not spans:
            continue
        excerpt = _redact(line, spans)
        for rule in dict.fromkeys(r for r, _, _ in spans):
            findings.append(Finding(path, lineno, rule, excerpt))
    return findings


def scan_image(path: Path) -> list[Finding]:
    """Check EXIF/XMP/text-chunk metadata and a sidecar OCR text file, if present."""
    path = Path(path)
    findings: list[Finding] = []
    pieces: list[str] = []
    try:
        from PIL import Image
        with Image.open(path) as im:
            for value in list(im.info.values()) + list(im.getexif().values()):
                if isinstance(value, bytes):
                    value = value.decode("utf-8", "replace")
                if isinstance(value, str):
                    pieces.append(value)
    except Exception:
        pass  # not a decodable image; the sidecar is still checked
    if pieces:
        findings += scan_text("\n".join(pieces), str(path))
    for sidecar in (path.with_suffix(".ocr.txt"), path.with_name(path.name + ".ocr.txt")):
        if sidecar.is_file():
            findings += scan_text(sidecar.read_text("utf-8", "replace"), str(path))
    return findings


def scan_paths(paths: list[Path]) -> list[Finding]:
    findings: list[Finding] = []
    for p in map(Path, paths):
        if not p.is_file() or p.as_posix() in SELF_PATHS:
            continue
        if p.suffix.lower() in IMAGE_SUFFIXES:
            findings += scan_image(p)
            continue
        data = p.read_bytes()
        if b"\0" in data:
            continue
        findings += scan_text(data.decode("utf-8", "replace"), str(p))
    return findings


def _staged_paths() -> list[Path]:
    out = subprocess.run(
        ["git", "diff", "--cached", "--name-only", "--diff-filter=ACM"],
        capture_output=True, text=True, check=True).stdout
    return [Path(line) for line in out.splitlines() if line.strip()]


_POSITIVE = {
    "steam-api-key": "STEAM_API_KEY=0123456789abcdef0123456789abcdef",
    "steam-id": "STEAM_APPID=1234567",
    "steam-cookie": "steamLoginSecure=abc123",
    "github-token": "ghp_" + "a" * 36,
    "valve-confidential": "Valve Confidential",
    "private-email": "someone@gmail.com",
}
_NEGATIVE = ["STEAM_APPID=", "STEAM_APPID=480", "noreply@anthropic.com",
             "1+n@users.noreply.github.com", "just some ordinary text"]


def selftest() -> bool:
    ok = True
    for rule, sample in _POSITIVE.items():
        if rule not in {f.rule for f in scan_text(sample, "selftest")}:
            print(f"selftest FAIL: {rule} not detected", file=sys.stderr)
            ok = False
    for sample in _NEGATIVE:
        if scan_text(sample, "selftest"):
            print(f"selftest FAIL: false positive on {sample!r}", file=sys.stderr)
            ok = False
    return ok


def add_parser(sub) -> None:
    p = sub.add_parser("secrets", help="scan for secrets before they reach a public repo")
    p.add_argument("paths", nargs="*", type=Path)
    p.add_argument("--staged", action="store_true", help="scan files staged in git")
    p.add_argument("--selftest", action="store_true", help="run built-in samples")
    p.set_defaults(func=run)


def run(args) -> int:
    if args.selftest:
        if not selftest():
            return 1
        print("secrets selftest ok")
        return 0
    paths = list(args.paths)
    if args.staged:
        paths += _staged_paths()
    findings = scan_paths(paths)
    for f in findings:
        print(f"{f.path}:{f.line}: [{f.rule}] {f.excerpt_redacted}")
    if findings:
        print(f"{len(findings)} secret finding(s); commit blocked.", file=sys.stderr)
        return 1
    return 0
