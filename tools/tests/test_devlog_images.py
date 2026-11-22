import random
from pathlib import Path

from PIL import Image

from og.devlog import to_webp


def noisy_png(path: Path, width: int, height: int, seed: int = 7) -> Path:
    """A gradient with per-pixel noise: hard to compress, like a busy screenshot."""
    rng = random.Random(seed)
    img = Image.new("RGB", (width, height))
    noise = bytes(rng.getrandbits(8) for _ in range(width * height * 3))
    base = Image.linear_gradient("L").resize((width, height)).convert("RGB")
    img = Image.blend(base, Image.frombytes("RGB", (width, height), noise), 0.3)
    img.save(path)
    return path


def test_to_webp_caps_size_and_dims(tmp_path):
    src = noisy_png(tmp_path / "big.png", 3840, 2160)
    # The fixture must be too big at the starting quality, or the step-down is never exercised.
    first_try = to_webp(src, tmp_path / "q90.webp", target_kb=10_000)
    assert first_try.stat().st_size > 300 * 1024
    out = to_webp(src, tmp_path / "big.webp")
    assert out == tmp_path / "big.webp"
    with Image.open(out) as img:
        assert img.format == "WEBP"
        assert img.size == (1920, 1080)
    assert out.stat().st_size <= 300 * 1024


def test_to_webp_never_upscales(tmp_path):
    src = noisy_png(tmp_path / "small.png", 800, 450)
    out = to_webp(src, tmp_path / "small.webp")
    with Image.open(out) as img:
        assert img.size == (800, 450)
