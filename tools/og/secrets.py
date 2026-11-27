"""Public-repo secrets scanner: regex rules over text, plus image metadata and OCR sidecars."""
from __future__ import annotations

import hashlib
import io
import os
import re
import subprocess
import sys
import warnings
from dataclasses import dataclass
from pathlib import Path

from og.repo import RepoError, repo_root


def _repo_root() -> Path:
    return repo_root(project=False)  # the scan works on any repository's staged files


IMAGE_SUFFIXES = {".png", ".jpg", ".jpeg", ".webp", ".gif", ".bmp", ".tif", ".tiff"}
# Binary asset formats (LFS-tracked, see .gitattributes). Their compressed bytes match the loose
# email pattern by chance, so the private-email rule is skipped for them; every other rule still
# runs on their decoded bytes.
BINARY_ASSET_SUFFIXES = {".uasset", ".umap", ".blend", ".fbx", ".glb", ".wav", ".ogg", ".psd",
                         ".exr", ".tga"}
# A line containing this marker is not reported, but only in the files that hold deliberate fake
# fixtures (PRAGMA_PATHS). Anywhere else the marker is ignored, so a real finding in a doc, a
# devlog or a release note cannot be cleared by appending a comment.
PRAGMA = "og-secrets: allow"
PRAGMA_PATHS = ("tools/tests/", "tools/og/secrets.py", "tools/steam/test-release.ps1",
                "tools/steam/fake-steamcmd.ps1")
# Keys whose local values (process environment or the gitignored .env) are searched for verbatim.
LOCAL_VALUE_KEYS = ("STEAM_APPID", "STEAM_DEPOT_WIN", "STEAM_BUILD_ACCOUNT")
EXCERPT_MAX = 120

# The local part is bounded (64 is the limit in the mail standard): without the bound a long
# unbroken token with no @ makes the search quadratic.
EMAIL_RE = re.compile(r"(?P<s>[A-Za-z0-9._%+-]{1,64}@[A-Za-z0-9-]+(?:\.[A-Za-z0-9-]+)*\.[A-Za-z]{2,})")
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
    # Forms the IDs take outside an assignment: store and partner URLs, steam:// links and
    # lines copied from steamcmd output. 480 (Spacewar, the public test app) is never a secret.
    ("steam-id",
     re.compile(r"(?:store\.steampowered\.com|steamcommunity\.com|steamdb\.info)/app/"
                r"(?P<s>(?!480\b)\d{3,})", re.I)),
    ("steam-id",
     re.compile(r"partner\.steamgames\.com/\S*?(?P<s>\d{4,})", re.I)),
    ("steam-id",
     re.compile(r"steam://\w+/(?P<s>(?!480\b)\d{3,})", re.I)),
    ("steam-id",
     re.compile(r"\b(?:app|depot)\s*_?\s*id\b\W{0,6}(?P<s>(?!480\b)\d{4,})", re.I)),
    ("steam-id",
     re.compile(r"\bdepot\s+(?P<s>\d{4,})\b", re.I)),
    ("steam-id",
     re.compile(r"\[U:1:(?P<s>\d+)\]")),
    ("steam-build-account",
     re.compile(r"STEAM_BUILD_ACCOUNT[\"']?\s*[=:][ \t]*[\"']?(?P<s>[A-Za-z0-9_.@-]+)")),
    ("steam-build-account",
     re.compile(r"Logging in user '(?P<s>[^'<$\[][^']*)'")),  # og-secrets: allow
    ("steam-build-account",
     re.compile(r"\+login\s+(?P<s>(?!anonymous\b)[A-Za-z0-9_.@-]+)")),
    ("steam-cookie",
     re.compile(r"(?:steamLoginSecure|steamMachineAuth\w*)\s*[=:]\s*(?P<s>\S+)")),
    ("github-token",
     re.compile(r"\b(?P<s>gh[pousr]_[A-Za-z0-9]{36,})")),
    ("github-token",
     re.compile(r"\b(?P<s>github_pat_[A-Za-z0-9_]{20,})")),
    ("private-key",
     re.compile(r"(?P<s>-----BEGIN [A-Z ]*PRIVATE KEY-----)")),
    ("aws-access-key",
     re.compile(r"\b(?P<s>AKIA[0-9A-Z]{16})\b")),
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


_local_rx: list | None = None


def _local_value_rules() -> list:
    """Regexes for this machine's own Steam values (LOCAL_VALUE_KEYS), read from the process
    environment and from the repo's gitignored .env. They catch the real values in any form,
    where the pattern rules can only guess at shapes. Values are never printed. Empty on a
    machine without them (CI)."""
    global _local_rx
    if _local_rx is None:
        values = {k: os.environ.get(k, "") for k in LOCAL_VALUE_KEYS}
        try:
            for raw in (_repo_root() / ".env").read_text("utf-8", "replace").splitlines():
                key, sep, val = raw.partition("=")
                key = key.strip()
                if sep and key in LOCAL_VALUE_KEYS and not values[key]:
                    values[key] = val.strip().strip("\"'")
        except (OSError, subprocess.SubprocessError, RepoError):
            pass  # no .env, or not inside a repository
        _local_rx = [re.compile(r"(?<![A-Za-z0-9])(?P<s>" + re.escape(v) + r")(?![A-Za-z0-9])")
                     for v in values.values() if len(v) >= 4 and v != "480"]
    return _local_rx


def _spans(line: str, emails: bool = True):
    """Yield (rule, start, end) for every secret span on a line."""
    for rule, rx in RULES:
        for m in rx.finditer(line):
            yield rule, m.start("s"), m.end("s")
    for rx in _local_value_rules():
        for m in rx.finditer(line):
            yield "steam-local-value", m.start("s"), m.end("s")
    if not emails:
        return
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


suppressed_lines = 0  # lines the pragma silenced in this process; `run` reports the count


def _pragma_applies(path: str) -> bool:
    posix = str(path).replace("\\", "/")
    return posix == "selftest" or any(p in posix for p in PRAGMA_PATHS)


def scan_text(text: str, path: str, emails: bool = True) -> list[Finding]:
    global suppressed_lines
    findings = []
    pragma_ok = _pragma_applies(path)
    for lineno, line in enumerate(text.splitlines(), 1):
        spans = list(_spans(line, emails))
        if not spans:
            continue
        if pragma_ok and PRAGMA in line:
            suppressed_lines += 1
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


def ocr_sidecars(media: Path) -> list[Path]:
    """The OCR text files a media file may have next to it: <stem>.ocr.txt and <name>.ocr.txt
    (x.ocr.txt and x.png.ocr.txt). Not checked for existence."""
    media = Path(media)
    return list(dict.fromkeys((media.with_suffix(".ocr.txt"), media.with_name(media.name + ".ocr.txt"))))


def scan_image(path: Path) -> list[Finding]:
    """Check image metadata and its sidecar OCR text files (ocr_sidecars)."""
    path = Path(path)
    findings = _image_metadata_findings(path.read_bytes(), str(path))
    for sidecar in ocr_sidecars(path):
        if sidecar.is_file():
            findings += scan_text(sidecar.read_text("utf-8", "replace"), str(path))
    return findings


def _decode(data: bytes) -> str:
    """Decode text, handling UTF-16 (BOM or NUL pattern); strip stray NULs from binaries."""
    if data.startswith((b"\xff\xfe", b"\xfe\xff")):
        return data.decode("utf-16", "replace")
    if b"\0" in data:
        # Counted with bytes.count on slices (C speed), not a Python loop over every byte:
        # large binary assets go through here on every commit.
        even = data[0::2].count(0)
        odd = data[1::2].count(0)
        nulls = even + odd
        if nulls > len(data) // 4:
            if odd >= 0.9 * nulls:
                return data.decode("utf-16-le", "replace")
            if even >= 0.9 * nulls:
                return data.decode("utf-16-be", "replace")
        data = data.replace(b"\0", b"")
    return data.decode("utf-8", "replace")


def _scan_blob(path: str, data: bytes) -> list[Finding]:
    if Path(path).suffix.lower() in IMAGE_SUFFIXES:
        return _image_metadata_findings(data, path)
    binary_asset = Path(path).suffix.lower() in BINARY_ASSET_SUFFIXES
    return scan_text(_decode(data), path, emails=not binary_asset)


def scan_paths(paths: list[Path]) -> list[Finding]:
    """Scan files; a directory is walked recursively (skipping .git). A path that does not
    exist is a finding: a scan that read nothing must not look like a clean scan."""
    findings: list[Finding] = []
    for p in map(Path, paths):
        if p.is_dir():
            files = sorted(f for f in p.rglob("*") if f.is_file() and ".git" not in f.parts)
        elif p.is_file():
            files = [p]
        else:
            findings.append(Finding(str(p), 0, "unscannable-path", "path does not exist"))
            continue
        for f in files:
            if f.suffix.lower() in IMAGE_SUFFIXES:
                findings += scan_image(f)
            else:
                findings += _scan_blob(str(f), f.read_bytes())
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


def _index_blob(name: str, root: Path):
    """Return (data, None) for the index content of name, or (None, Finding) when unreadable."""
    r = subprocess.run(["git", "show", ":" + name], capture_output=True, cwd=root)
    if r.returncode != 0:
        why = r.stderr.decode("utf-8", "replace").strip().splitlines()
        return None, Finding(name, 0, "unscannable-blob",
                             (why[0] if why else f"git show exit {r.returncode}")[:EXCERPT_MAX])
    return r.stdout, None


def _index_blobs(names: list[str], root: Path) -> dict[str, bytes | None]:
    """Index content of many paths from one `git cat-file --batch` process, instead of one
    `git show` per file. A path maps to None when the index has no such blob. Paths that
    cannot be sent on one line are left out, and so is everything if git fails: the caller
    then asks for those one at a time, which also produces the error finding."""
    ok = [n for n in dict.fromkeys(names) if "\n" not in n]
    if not ok:
        return {}
    try:
        r = subprocess.run(["git", "cat-file", "--batch"], cwd=root, capture_output=True,
                           input="".join(f":{n}\n" for n in ok).encode("utf-8"))
    except OSError:
        return {}
    if r.returncode != 0:
        return {}
    out, pos, blobs = r.stdout, 0, {}
    for name in ok:
        eol = out.find(b"\n", pos)
        if eol < 0:
            break
        head = out[pos:eol].split(b" ")
        pos = eol + 1
        if len(head) != 3 or not head[2].isdigit():
            blobs[name] = None  # "<name> missing" and the like
            continue
        size = int(head[2])
        blobs[name] = out[pos:pos + size]
        pos += size + 1
    return blobs


def _sidecar_names(name: str) -> list[str]:
    return [p.as_posix() for p in ocr_sidecars(Path(name))]


def _scan_index_entries(names: list[str], root: Path,
                        lfs_pointers_ok: bool = False) -> list[Finding]:
    """Scan index content for each path: LFS pointers resolve to verified working-tree
    content, images also get their OCR sidecars. Anything unreadable is a finding.

    With lfs_pointers_ok, an LFS pointer is scanned as the pointer text itself (for CI checkouts
    without LFS content; the pre-commit hook already scanned the verified content)."""
    findings: list[Finding] = []
    images = [n for n in names if Path(n).suffix.lower() in IMAGE_SUFFIXES]
    blobs = _index_blobs(names + [sc for n in images for sc in _sidecar_names(n)], root)
    for name in names:
        if blobs.get(name) is not None:
            blob, bad = blobs[name], None
        else:
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
        findings += scan_text(name, name)  # the path itself is published too
        if Path(name).suffix.lower() in IMAGE_SUFFIXES:
            for sc in _sidecar_names(name):
                if sc in blobs:
                    staged = blobs[sc]
                else:  # not answered by the batch: ask git for this one path
                    r = subprocess.run(["git", "show", ":" + sc], capture_output=True, cwd=root)
                    staged = r.stdout if r.returncode == 0 else None
                if staged is not None:
                    sidecar = staged
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
    # Excerpts from binaries can hold characters a cp1252 console cannot print.
    for stream in (sys.stdout, sys.stderr):
        if hasattr(stream, "reconfigure"):
            stream.reconfigure(errors="backslashreplace")
    if args.selftest:
        if not selftest():
            return 1
        print("secrets selftest ok")
        return 0
    if not (args.paths or args.staged or args.tracked):
        print("og secrets: nothing to scan. Give paths, --staged or --tracked.", file=sys.stderr)
        return 2
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
    if suppressed_lines:
        print(f"{suppressed_lines} line(s) suppressed by '{PRAGMA}' in fixture files.",
              file=sys.stderr)
    if findings:
        print(f"{len(findings)} secret finding(s); commit blocked.", file=sys.stderr)
        return 1
    return 0
