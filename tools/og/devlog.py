"""Devlog helpers. This part: converting captures to the WebP images the devlog publishes.

Devlog images are WebP, at most 1920 px on the long side, and target 300 KB or less, so the repo
stays small (they are plain git, not LFS).
"""
from io import BytesIO
from pathlib import Path

from PIL import Image

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
