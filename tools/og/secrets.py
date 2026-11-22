"""Public-repo secrets scanner: regex rules over text, plus image metadata and OCR sidecars."""
from __future__ import annotations

import hashlib
import io
import re
import subprocess
import sys
import warnings
from dataclasses import dataclass
from pathlib import Path

IMAGE_SUFFIXES = {".png", ".jpg", ".jpeg", ".webp", ".gif", ".bmp", ".tif", ".tiff"}
# A line containing this marker is never reported (for deliberate fake fixtures).
PRAGMA = "og-secrets: allow"
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
     re.compile(r"STEAM_(?:APPID|DEPOT\w*)[\"']?\s*[=:]\s*[\"']?(?P<s>(?!480\b)\d{4,})")),
    ("steam-id",
     re.compile(r"[\"'](?:appid|depotid)[\"']\s+[\"'](?P<s>(?!480[\"'])\d{4,})[\"']", re.I)),
    ("steam-id",
     re.compile(r"steam.{0,40}?app\s*id\D{0,6}(?P<s>\d{5,})", re.I)),
    ("steam-build-account",
     re.compile(r"STEAM_BUILD_ACCOUNT[\"']?\s*[=:][ \t]*[\"']?(?P<s>[A-Za-z0-9_.@-]+)")),
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
        spans = [] if PRAGMA in line else list(_spans(line))
        if not spans:
            continue
        excerpt = _redact(line, spans)
        for rule in dict.fromkeys(r for r, _, _ in spans):
            findings.append(Finding(path, lineno, rule, excerpt))
    return findings


def _image_metadata_findings(data: bytes, path: str) -> list[Finding]:
    """Scan EXIF/XMP/text-chunk metadata. Fails closed on real errors."""
    try:
        from PIL import Image
    except ImportError:
        return [Finding(path, 0, "unscannable-image", "Pillow is not installed")]
    pieces: list[str] = []
    try:
        with warnings.catch_warnings():
            warnings.simplefilter("error", Image.DecompressionBombWarning)
            with Image.open(io.BytesIO(data)) as im:
                im.load()  # fully decode: detects truncated/corrupt files
            with Image.open(io.BytesIO(data)) as im:
                for value in list(im.info.values()) + list(im.getexif().values()):
                    if isinstance(value, bytes):
                        value = value.decode("utf-8", "replace")
                    if isinstance(value, str):
                        pieces.append(value)
    except Image.UnidentifiedImageError:
        return []  # genuinely not an image; nothing to read
    except Exception as exc:
        return [Finding(path, 0, "unscannable-image", type(exc).__name__)]
    return scan_text("\n".join(pieces), path) if pieces else []


def scan_image(path: Path) -> list[Finding]:
    """Check image metadata and a sidecar OCR text file (x.ocr.txt or x.png.ocr.txt)."""
    path = Path(path)
    findings = _image_metadata_findings(path.read_bytes(), str(path))
    for sidecar in (path.with_suffix(".ocr.txt"), path.with_name(path.name + ".ocr.txt")):
        if sidecar.is_file():
            findings += scan_text(sidecar.read_text("utf-8", "replace"), str(path))
    return findings


def _decode(data: bytes) -> str:
    """Decode text, handling UTF-16 (BOM or NUL pattern); strip stray NULs from binaries."""
    if data.startswith((b"\xff\xfe", b"\xfe\xff")):
        return data.decode("utf-16", "replace")
    if b"\0" in data:
        nulls = [i for i, b in enumerate(data) if b == 0]
        odd = sum(1 for i in nulls if i % 2)
        even = len(nulls) - odd
        if len(nulls) > len(data) // 4:
            if odd >= 0.9 * len(nulls):
                return data.decode("utf-16-le", "replace")
            if even >= 0.9 * len(nulls):
                return data.decode("utf-16-be", "replace")
        data = data.replace(b"\0", b"")
    return data.decode("utf-8", "replace")


def _scan_blob(path: str, data: bytes) -> list[Finding]:
    if Path(path).suffix.lower() in IMAGE_SUFFIXES:
        return _image_metadata_findings(data, path)
    return scan_text(_decode(data), path)


def scan_paths(paths: list[Path]) -> list[Finding]:
    findings: list[Finding] = []
    for p in map(Path, paths):
        if not p.is_file():
            continue
        if p.suffix.lower() in IMAGE_SUFFIXES:
            findings += scan_image(p)
        else:
            findings += _scan_blob(str(p), p.read_bytes())
    return findings


LFS_POINTER = b"version https://git-lfs.github.com/spec"


def _resolve_lfs(name: str, pointer: bytes, root: Path):
    """Return (data, None) for the verified real content, or (None, Finding) on failure."""
    def fail(why: str):
        return None, Finding(name, 0, "unscannable-lfs", why)

    fields = dict(line.split(" ", 1) for line in
                  pointer.decode("utf-8", "replace").splitlines() if " " in line)
    oid = fields.get("oid", "")
    size = fields.get("size", "")
    if not oid.startswith("sha256:") or not size.isdigit():
        return fail("malformed LFS pointer")
    real = root / name
    if not real.is_file():
        return fail("LFS content missing from working tree")
    data = real.read_bytes()
    if len(data) != int(size) or hashlib.sha256(data).hexdigest() != oid[7:]:
        return fail("working tree file does not match LFS pointer")
    return data, None


def _repo_root() -> Path:
    return Path(subprocess.run(["git", "rev-parse", "--show-toplevel"], capture_output=True,
                               check=True, text=True).stdout.strip())


def _index_blob(name: str, root: Path):
    """Return (data, None) for the index content of name, or (None, Finding) when unreadable."""
    r = subprocess.run(["git", "show", ":" + name], capture_output=True, cwd=root)
    if r.returncode != 0:
        why = r.stderr.decode("utf-8", "replace").strip().splitlines()
        return None, Finding(name, 0, "unscannable-blob",
                             (why[0] if why else f"git show exit {r.returncode}")[:EXCERPT_MAX])
    return r.stdout, None


def _scan_index_entries(names: list[str], root: Path,
                        lfs_pointers_ok: bool = False) -> list[Finding]:
    """Scan index content for each path: LFS pointers resolve to verified working-tree
    content, images also get their OCR sidecars. Anything unreadable is a finding.

    With lfs_pointers_ok, an LFS pointer is scanned as the pointer text itself (for CI checkouts
    without LFS content; the pre-commit hook already scanned the verified content)."""
    findings: list[Finding] = []
    for name in names:
        blob, bad = _index_blob(name, root)
        if bad is None and blob.startswith(LFS_POINTER):
            if lfs_pointers_ok:
                findings += scan_text(_decode(blob), name)
                continue
            blob, bad = _resolve_lfs(name, blob, root)
        if bad:
            findings.append(bad)
            continue
        findings += _scan_blob(name, blob)
        if Path(name).suffix.lower() in IMAGE_SUFFIXES:
            for sc in (Path(name).with_suffix(".ocr.txt").as_posix(), name + ".ocr.txt"):
                r = subprocess.run(["git", "show", ":" + sc], capture_output=True, cwd=root)
                if r.returncode == 0:
                    sidecar = r.stdout
                elif (root / sc).is_file():
                    sidecar = (root / sc).read_bytes()
                else:
                    continue
                findings += scan_text(_decode(sidecar), name)
    return findings


def scan_staged() -> list[Finding]:
    """Scan the staged (index) content, not the working tree. Paths resolve from the repo root."""
    root = _repo_root()
    out = subprocess.run(
        ["git", "diff", "--cached", "--name-only", "-z", "--no-renames",
         "--diff-filter=ACMRT"],
        capture_output=True, check=True, cwd=root).stdout.decode("utf-8")
    return _scan_index_entries(list(filter(None, out.split("\0"))), root)


def scan_tracked(lfs_pointers_ok: bool = False) -> list[Finding]:
    """Scan the index content of every tracked file (git ls-files), skipping submodules.
    Fails closed on LFS content that is missing, unless lfs_pointers_ok."""
    root = _repo_root()
    out = subprocess.run(["git", "ls-files", "-s", "-z"], capture_output=True, check=True,
                         cwd=root).stdout.decode("utf-8")
    names = []
    for entry in filter(None, out.split("\0")):
        meta, _, name = entry.partition("\t")
        if meta.split(" ")[0] == "160000":
            continue  # gitlink (submodule): no blob here
        if name not in names:
            names.append(name)  # unmerged paths list several stages; scan once
    return _scan_index_entries(names, root, lfs_pointers_ok)


_POSITIVE = {
    "steam-api-key": "STEAM_API_KEY=" + "0123456789abcdef" * 2,
    "steam-id": "STEAM_APPID=" + "1234567",  # og-secrets: allow
    "steam-build-account": "STEAM_BUILD_ACCOUNT=" + "builder",  # og-secrets: allow
    "steam-cookie": "steamLoginSecure=" + "abc123",  # og-secrets: allow
    "github-token": "ghp_" + "a" * 36,
    "valve-confidential": "Valve " + "Confidential",
    "private-email": "someone@" + "gmail.com",
}
_NEGATIVE = ["STEAM_APPID=", "STEAM_BUILD_ACCOUNT=", "STEAM_APPID=480", "noreply@anthropic.com",
             "1+n@users.noreply.github.com", "just some ordinary text",
             "STEAM_APPID=" + "1234567  # " + PRAGMA]  # og-secrets: allow


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
    p.add_argument("--tracked", action="store_true",
                   help="scan every tracked file (git ls-files), as committed to the index")
    p.add_argument("--lfs-pointers-ok", action="store_true",
                   help="with --tracked: accept LFS pointers without their content (CI "
                        "checkouts with lfs: false); the pointer text is still scanned")
    p.add_argument("--selftest", action="store_true", help="run built-in samples")
    p.set_defaults(func=run)


def run(args) -> int:
    if args.selftest:
        if not selftest():
            return 1
        print("secrets selftest ok")
        return 0
    findings = scan_paths(args.paths)
    if args.staged:
        findings += scan_staged()
    if args.lfs_pointers_ok and not args.tracked:
        print("--lfs-pointers-ok only applies with --tracked.", file=sys.stderr)
        return 2
    if args.tracked:
        findings += scan_tracked(lfs_pointers_ok=args.lfs_pointers_ok)
    for f in findings:
        print(f"{f.path}:{f.line}: [{f.rule}] {f.excerpt_redacted}")
    if findings:
        print(f"{len(findings)} secret finding(s); commit blocked.", file=sys.stderr)
        return 1
    return 0
