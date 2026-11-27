"""Devlog formats, in one place: every writer and reader of them goes through this module.

- Entry front matter (docs/devlog/<id>-<slug>.md): `front_value` writes a value, `parse_front` reads
  the block back. og session writes entries; og site and og session read them.
- The capture index (docs/captures/INDEX.md): `format_index_row` writes a row, `parse_index` reads
  the rows, `merge_index` adds new ones. og session writes it; og session (milestones) and og site
  (gallery, home page) read it.
- Images: `to_webp` converts captures to the WebP images the devlog publishes. Devlog images are
  WebP, at most 1920 px on the long side, and target 300 KB or less, so the repo stays small (they
  are plain git, not LFS).
"""
from __future__ import annotations

import re
from dataclasses import dataclass
from io import BytesIO
from pathlib import Path

from PIL import Image

_FRONT = re.compile(r"\A---\r?\n(.*?)\r?\n---\r?\n?", re.S)
_DATE = re.compile(r"\d{4}-\d{2}-\d{2}")
_ESCAPED = re.compile(r"\\(.)")


# ---- entry front matter ----

def front_value(value) -> str:
    """A value as written after `key: ` in an entry's front matter: None is null, a list is
    [a, b], anything else a double-quoted string on one line (runs of whitespace, newlines
    included, become one space) with backslash and double quote escaped (parse_front undoes it)."""
    if value is None:
        return "null"
    if isinstance(value, (list, tuple)):
        return "[" + ", ".join(str(v) for v in value) + "]"
    text = " ".join(str(value).split()).replace("\\", "\\\\").replace('"', '\\"')
    return f'"{text}"'


def _value(raw: str):
    raw = raw.strip()
    if raw in ("", "null", "~"):
        return None
    if raw.startswith("[") and raw.endswith("]"):
        return [_value(p) for p in raw[1:-1].split(",") if p.strip()]
    if len(raw) >= 2 and raw[0] == raw[-1] == '"':
        return _ESCAPED.sub(r"\1", raw[1:-1])
    if len(raw) >= 2 and raw[0] == raw[-1] == "'":
        return raw[1:-1]
    return raw


def parse_front(text: str) -> tuple[dict, str]:
    """Split a devlog entry into (front matter dict, markdown body)."""
    m = _FRONT.match(text)
    if not m:
        return {}, text
    meta = {}
    for line in m.group(1).splitlines():
        key, sep, val = line.partition(":")
        if sep and key.strip():
            meta[key.strip()] = _value(val)
    return meta, text[m.end():]


# ---- capture index ----

@dataclass(frozen=True)
class IndexRow:
    date: str
    camera: str
    path: str
    sha: str


def format_index_row(row: IndexRow) -> str:
    return f"| {row.date} | {row.camera} | {row.path} | {row.sha} |"


def parse_index(text: str) -> list[IndexRow]:
    """The image rows of the index, in file order: four cells with a date first (the header and
    the separator line are not rows)."""
    rows = []
    for line in text.splitlines():
        cells = [c.strip() for c in line.strip().strip("|").split("|")]
        if len(cells) == 4 and _DATE.fullmatch(cells[0]):
            rows.append(IndexRow(*cells))
    return rows


def merge_index(existing: str, rows: list[IndexRow]) -> str:
    """Append the rows whose image path is not already listed (idempotent on rerun)."""
    if existing and not existing.endswith("\n"):
        existing += "\n"
    listed = {r.path for r in parse_index(existing)}
    new = [r for r in rows if r.path not in listed]
    return existing + "".join(format_index_row(r) + "\n" for r in new)


# ---- images ----

START_QUALITY = 90
MIN_QUALITY = 50
QUALITY_STEP = 5


def to_webp(src: Path, dst: Path, max_px: int = 1920, target_kb: int = 300) -> Path:
    """Write src as a WebP at dst and return dst.

    The image is scaled down (never up) so its long side is at most max_px. Quality starts at 90
    and drops in steps of 5 until the file is target_kb or less; if even quality 50 is larger,
    the quality 50 result is written anyway (the target is a goal, not a refusal).
    """
    src, dst = Path(src), Path(dst)
    with Image.open(src) as img:
        img.load()
        if img.mode not in ("RGB", "RGBA"):
            img = img.convert("RGBA" if "A" in img.getbands() else "RGB")
        # thumbnail() only ever shrinks and keeps the aspect ratio.
        img.thumbnail((max_px, max_px), Image.Resampling.LANCZOS)

        data = b""
        for quality in range(START_QUALITY, MIN_QUALITY - 1, -QUALITY_STEP):
            buf = BytesIO()
            img.save(buf, format="WEBP", quality=quality, method=4)
            data = buf.getvalue()
            if len(data) <= target_kb * 1024:
                break

    dst.parent.mkdir(parents=True, exist_ok=True)
    dst.write_bytes(data)
    return dst
