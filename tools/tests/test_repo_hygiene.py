import subprocess
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]


def git(*args):
    return subprocess.run(["git", *args], cwd=ROOT, capture_output=True, text=True)


def ignored(path):
    return git("check-ignore", "-q", path).returncode == 0


def test_gitignore_covers_secrets():
    for p in [".env", ".env.local", "_release/x/post.bbcode", "_scratch/a",
              "steam_appid.txt", "app_build_1.vdf", ".mcp.json", "Game/Saved/x",
              "docs/superpowers/x.md", "a.mp4"]:
        assert ignored(p), p
    for p in [".env.example", ".mcp.json.example", "steam/templates/app_build.vdf"]:
        assert not ignored(p), p


def attr_filter(path):
    out = git("check-attr", "filter", path).stdout.strip()
    return out.rsplit(": ", 1)[-1]


def test_gitattributes_lfs():
    for p in ["x.uasset", "x.blend", "Art/a.png"]:
        assert attr_filter(p) == "lfs", p
    assert attr_filter("docs/devlog/media/a.webp") != "lfs"
