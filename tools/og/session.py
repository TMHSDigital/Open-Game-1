"""Session routine: `og session start|end` and `og milestone`.

State lives in `_scratch/session.json` (gitignored). `end` writes the devlog entry, converts
captures to WebP, scans everything with og.secrets (a finding blocks the commit), uploads the
clip to the monthly media release and commits only its own outputs.

Desktop-derived media (the contact sheet and the clip) must come with OCR text next to them
(`<file>.ocr.txt`, made with screencast find_text), which is scanned like any other text. Without
it `end` refuses, unless the owner passes --no-ocr-ok. Unreal capture PNGs are game renders and
need no OCR text.
"""
from __future__ import annotations

import datetime
import json
import re
import shutil
import subprocess
import sys
from pathlib import Path

from og import gh, secrets
from og.devlog import to_webp

PKG_ROOT = Path(__file__).resolve().parents[2]
NARRATIVE_MARKER = "<!-- narrative -->"
CHECKLIST = Path("docs/steam/SETUP-CHECKLIST.md")
INDEX = Path("docs/captures/INDEX.md")
RELEASE_NOTES = "Session recordings for the devlog. Not a game build."
EMPTY_TREE = "4b825dc642cb6eb9a060e54bf8d69288fbee4904"


class SessionError(RuntimeError):
    pass


def _git(repo: Path, *args: str, check: bool = True) -> str:
    r = subprocess.run(["git", "-C", str(repo), *args], capture_output=True, text=True,
                       encoding="utf-8")
    if check and r.returncode != 0:
        raise SessionError(f"git {' '.join(args[:2])} failed: {r.stderr.strip()}")
    return r.stdout.strip() if r.returncode == 0 else ""


def _template(repo: Path, rel: str) -> str:
    for base in (repo, PKG_ROOT):
        p = base / rel
        if p.is_file():
            return p.read_text("utf-8")
    raise SessionError(f"missing template {rel}")


def _render(text: str, values: dict[str, str]) -> str:
    for k, v in values.items():
        text = text.replace("{{" + k + "}}", v)
    return text


def _slugify(text: str) -> str:
    s = re.sub(r"[^a-z0-9]+", "-", text.lower()).strip("-")
    return s[:40].strip("-") or "session"


def _state_path(repo: Path) -> Path:
    return repo / "_scratch" / "session.json"


def _next_id(repo: Path, day: datetime.date) -> str:
    existing = list((repo / "docs" / "devlog").glob(f"{day.isoformat()}-[0-9][0-9]-*.md"))
    nums = [int(p.name[11:13]) for p in existing]
    return f"{day.isoformat()}-{max(nums, default=0) + 1:02d}"


def _find_entry(repo: Path, sid: str) -> Path | None:
    found = sorted((repo / "docs" / "devlog").glob(f"{sid}-*.md"))
    return found[0] if found else None


def _open_setup_items(repo: Path) -> list[str]:
    """Numbered checklist lines whose italic status does not start with Done."""
    f = repo / CHECKLIST
    if not f.is_file():
        return []
    items = []
    for line in f.read_text("utf-8").splitlines():
        if not re.match(r"\s*\d+\.\s", line):
            continue
        m = re.search(r"\*([A-Za-z][^*]*)\*", line)
        if m and m.group(1).strip().lower().startswith("done"):
            continue
        items.append(line.strip())
    return items


def _render_entry(repo: Path, d: dict) -> str:
    changes = "\n".join(f"- {c}" for c in d["commits"]) or d.get("empty_changes", "_No commits._")
    decisions = d["decisions"]
    caps = "\n".join(f"![{c}]({p})" for c, p in d["captures"]) or "_No captures._"
    clip = d["clip"]
    return _render(_template(repo, "docs/devlog/TEMPLATE.md"), {
        "id": d["id"], "date": d["id"][:10], "goal": d["goal"].replace('"', "'"),
        "start_sha": d["start_sha"] or "null", "end_sha": d["end_sha"] or "null",
        "decisions": "[" + ", ".join(decisions) + "]",
        "cameras": "[" + ", ".join(c for c, _ in d["captures"] if c != "contact-sheet") + "]",
        "clip": clip or "null",
        "warning": f"> WARNING: {d['warning']}\n\n" if d.get("warning") else "",
        "changes": changes,
        "decisions_list": "\n".join(f"- {x}" for x in decisions) or "_None._",
        "captures": caps,
        "clip_line": f"[Session clip]({clip})" if clip else "_No clip._",
    })


def start(repo: Path, goal: str, today: datetime.date | None = None, commit: bool = False) -> str:
    repo = Path(repo)
    day = today or datetime.date.today()
    sid = _next_id(repo, day)
    slug = _slugify(goal)
    if commit:
        _git(repo, "commit", "--allow-empty", "-m",
             f"chore: session {sid} start\n\nSession-Start: {sid}")
    head = _git(repo, "rev-parse", "HEAD", check=False)
    entry = repo / "docs" / "devlog" / f"{sid}-{slug}.md"
    entry.parent.mkdir(parents=True, exist_ok=True)
    entry.write_text(_render_entry(repo, {
        "id": sid, "goal": goal, "start_sha": head, "end_sha": None, "commits": [],
        "decisions": [], "captures": [], "clip": None,
        "empty_changes": "_Filled in by `og session end`._"}), "utf-8")
    state = {"id": sid, "slug": slug, "goal": goal, "start_sha": head,
             "started_at": datetime.datetime.now().astimezone().isoformat(timespec="seconds"),
             "screencast_session": None}
    sp = _state_path(repo)
    sp.parent.mkdir(parents=True, exist_ok=True)
    sp.write_text(json.dumps(state, indent=2) + "\n", "utf-8")
    print(sid)
    for item in _open_setup_items(repo):
        print(f"Open setup item: {item}")
    hooks = _git(repo, "config", "--get", "core.hooksPath", check=False)
    if hooks.replace(chr(92), "/").rstrip("/") != "tools/hooks":
        print("WARNING: the commit hook is not enabled on this clone, so commits are not scanned "
              "before they are made. Run: git config core.hooksPath tools/hooks", file=sys.stderr)
    return sid


def _resolve_start(repo: Path, today: datetime.date) -> tuple[dict, str | None]:
    """Return (state, warning). State comes from the file, else the trailer, else HEAD~1."""
    sp = _state_path(repo)
    if sp.is_file():
        return json.loads(sp.read_text("utf-8")), None
    for sha in _git(repo, "log", "--format=%H", "--grep=^Session-Start: ", check=False).split():
        body = _git(repo, "log", "-1", "--format=%B", sha)
        m = re.search(r"^Session-Start: (\S+)", body, re.M)
        if not m:
            continue
        sid = m.group(1)
        if _git(repo, "log", "-1", "--format=%H", f"--grep=^Session: {re.escape(sid)}$",
                check=False):
            continue  # that session already ended; do not reuse its start
        entry = _find_entry(repo, sid)
        goal = "(unknown goal)"
        if entry:
            g = re.search(r'^goal: "(.*)"$', entry.read_text("utf-8"), re.M)
            goal = g.group(1) if g else goal
        return {"id": sid, "slug": _slugify(goal), "goal": goal, "start_sha": sha}, None
    sid = _next_id(repo, today)
    parent = _git(repo, "rev-parse", "--verify", "-q", "HEAD~1", check=False)
    warn = ("No open session and no Session-Start trailer found; "
            "start SHA fell back to HEAD~1, so Changes may be incomplete.")
    return {"id": sid, "slug": "session", "goal": "(unknown goal)",
            "start_sha": parent or EMPTY_TREE}, warn


def _commits(repo: Path, start_sha: str) -> list[tuple[str, str]]:
    """(oneline, full message) for commits in start..HEAD."""
    rng = "HEAD" if start_sha == EMPTY_TREE else f"{start_sha}..HEAD"
    shas = _git(repo, "log", "--reverse", "--format=%H", rng, check=False).split()
    return [(_git(repo, "log", "-1", "--oneline", s), _git(repo, "log", "-1", "--format=%B", s))
            for s in shas]


def _decisions(messages: list[str]) -> list[str]:
    found: list[str] = []
    for msg in messages:
        for m in re.finditer(r"\bOD-(\d{3})\b|\bADR[ -]?(\d{4})\b", msg):
            tag = f"OD-{m.group(1)}" if m.group(1) else f"ADR-{m.group(2)}"
            if tag not in found:
                found.append(tag)
    return found


def _run_capture(repo: Path, outdir: Path) -> None:
    r = subprocess.run(["pwsh", "-NoProfile", "-File", str(repo / "tools/ue/capture.ps1"),
                        "-Cameras", "all", "-OutDir", str(outdir)])
    if r.returncode != 0:
        raise SessionError(f"capture.ps1 failed ({r.returncode})")


def _print_findings(findings) -> None:
    for f in findings:
        print(f"SECRET {f.rule}: {f.path}:{f.line}: {f.excerpt_redacted}", file=sys.stderr)


def _clip_url(repo: Path, sid: str) -> str:
    slug = gh.repo_slug(repo) or gh._gh(
        ["repo", "view", "--json", "nameWithOwner", "-q", ".nameWithOwner"]).strip()
    return f"https://github.com/{slug}/releases/download/media-{sid[:7]}/{sid}.mp4"


def _upload_clip(repo: Path, sid: str, clip: Path) -> None:
    """Upload clip as <id>.mp4 to the monthly media release, creating it if missing."""
    tag = f"media-{sid[:7]}"
    try:
        gh._gh(["release", "view", tag])
    except gh.GhError:
        gh._gh(["release", "create", tag, "--title", f"Devlog media {sid[:7]}",
                "--notes", RELEASE_NOTES, "--prerelease"])
    scratch = repo / "_scratch"
    scratch.mkdir(exist_ok=True)
    asset = scratch / f"{sid}.mp4"
    shutil.copyfile(clip, asset)
    gh._gh(["release", "upload", tag, str(asset), "--clobber"])


def _ocr_sidecars(media: Path) -> list[Path]:
    """Existing OCR text files for a media file: <name>.ocr.txt or <stem>.ocr.txt."""
    media = Path(media)
    cands = (media.with_name(media.name + ".ocr.txt"), media.with_suffix(".ocr.txt"))
    return [c for c in dict.fromkeys(cands) if c.is_file()]


def _clip_text_findings(clip: Path) -> list:
    """Scan the clip's OCR text and, if present, its transcript (<clip>.srt / <stem>.srt)."""
    clip = Path(clip)
    texts = _ocr_sidecars(clip) + [t for t in dict.fromkeys(
        (clip.with_name(clip.name + ".srt"), clip.with_suffix(".srt"))) if t.is_file()]
    found = []
    for t in texts:
        found += secrets.scan_text(t.read_text("utf-8", "replace"), str(clip))
    return found


def _missing_ocr(clip: Path | None, contact_sheet: Path | None) -> list[Path]:
    return [Path(m) for m in (contact_sheet, clip) if m and not _ocr_sidecars(Path(m))]


def _merge_index(existing: str, rows: list[str]) -> str:
    """Append rows whose image path is not already listed (idempotent on rerun)."""
    if existing and not existing.endswith("\n"):
        existing += "\n"
    listed = {line.split("|")[3].strip() for line in existing.splitlines()
              if line.startswith("|") and line.count("|") >= 5}
    new = [r for r in rows if r.split("|")[3].strip() not in listed]
    return existing + "".join(r + "\n" for r in new)


def end(repo: Path, capture: bool = False, clip: Path | None = None,
        contact_sheet: Path | None = None, capture_dir: Path | None = None,
        commit: bool = True, co_author: str | None = None,
        today: datetime.date | None = None, no_ocr_ok: bool = False) -> int:
    repo = Path(repo)
    if clip and not Path(clip).is_file():
        print(f"og session: clip not found: {clip}", file=sys.stderr)
        return 1
    missing = _missing_ocr(clip, contact_sheet)
    if missing and not no_ocr_ok:
        for m in missing:
            print(f"og session: no OCR text for {m} (expected {m.name}.ocr.txt next to it, "
                  "from screencast find_text).", file=sys.stderr)
        print("Refusing to publish desktop media without an OCR secret check; nothing written. "
              "Add the .ocr.txt file, or pass --no-ocr-ok if you have checked it yourself.",
              file=sys.stderr)
        return 1
    for m in missing:
        print(f"NOTE: --no-ocr-ok: publishing {m} without an OCR text check (owner's decision).",
              file=sys.stderr)
    state, warning = _resolve_start(repo, today or datetime.date.today())
    if warning:
        print(f"WARNING: {warning}", file=sys.stderr)

    # 1. gather and scan source images before anything is written to docs/.
    sources: list[tuple[str, Path]] = []
    cdir = Path(capture_dir) if capture_dir else repo / "_scratch" / "capture"
    if capture:
        if capture_dir is None:
            shutil.rmtree(cdir, ignore_errors=True)
        _run_capture(repo, cdir)
    if capture or capture_dir:
        sources += [(p.stem, p) for p in sorted(cdir.glob("*.png"))]
    if contact_sheet:
        sources.append(("contact-sheet", Path(contact_sheet)))
    findings = [f for _, p in sources for f in secrets.scan_image(p)]
    if clip:
        findings += _clip_text_findings(Path(clip))
    if findings:
        _print_findings(findings)
        print("Blocked: secrets found in capture images or clip text; nothing written, uploaded "
              "or committed.", file=sys.stderr)
        return 1

    # 2. build every output in a staging dir; docs/ is untouched until all scans pass.
    stage = repo / "_scratch" / "stage"
    shutil.rmtree(stage, ignore_errors=True)
    try:
        return _end_staged(repo, stage, state, warning, sources, cdir, capture, clip,
                           commit, co_author)
    finally:
        shutil.rmtree(stage, ignore_errors=True)


def _end_staged(repo, stage, state, warning, sources, cdir, capture, clip, commit, co_author):
    sid, start_sha = state["id"], state["start_sha"]
    end_sha = _git(repo, "rev-parse", "HEAD", check=False)
    cap_sha = end_sha
    meta = cdir / "capture.json"
    if meta.is_file():  # written by tools/ue/capture.ps1, for --capture and --capture-dir alike
        try:
            recorded = json.loads(meta.read_text("utf-8-sig")).get("commit")
        except (ValueError, AttributeError):
            recorded = None
        if isinstance(recorded, str) and re.fullmatch(r"[0-9a-f]{40}", recorded):
            cap_sha = recorded
        else:
            print(f"WARNING: {meta} has no valid 40-hex 'commit'; INDEX rows use HEAD.",
                  file=sys.stderr)
    media_rel = Path("docs") / "devlog" / "media" / sid
    captures: list[tuple[str, str]] = []
    rows: list[str] = []
    outputs: list[tuple[Path, Path]] = []  # (staged file, final path)
    for name, src in sources:
        staged = to_webp(src, stage / "media" / f"{name}.webp")
        outputs.append((staged, repo / media_rel / staged.name))
        captures.append((name, f"media/{sid}/{staged.name}"))
        if name != "contact-sheet":
            rows.append(f"| {sid[:10]} | {name} | {(media_rel / staged.name).as_posix()} "
                        f"| {cap_sha} |")
    if rows:
        idx = repo / INDEX
        staged_idx = stage / "INDEX.md"
        staged_idx.parent.mkdir(parents=True, exist_ok=True)
        staged_idx.write_text(
            _merge_index(idx.read_text("utf-8") if idx.is_file() else "", rows), "utf-8")
        outputs.append((staged_idx, idx))

    commits = _commits(repo, start_sha)
    clip_url = None
    if clip:
        try:
            clip_url = _clip_url(repo, sid)
        except (gh.GhError, OSError) as exc:
            print(f"WARNING: could not determine repo for clip link: {exc}", file=sys.stderr)
    data = {"id": sid, "goal": state.get("goal", ""), "start_sha": start_sha,
            "end_sha": end_sha, "commits": [c[0] for c in commits],
            "decisions": _decisions([c[1] for c in commits]), "captures": captures,
            "clip": clip_url, "warning": warning}
    entry = _find_entry(repo, sid) or (
        repo / "docs" / "devlog" / f"{sid}-{state.get('slug') or 'session'}.md")
    staged_entry = stage / "entry.md"
    staged_entry.parent.mkdir(parents=True, exist_ok=True)
    staged_entry.write_text(_render_entry(repo, data), "utf-8")
    outputs.append((staged_entry, entry))

    findings = secrets.scan_paths([s for s, _ in outputs])
    if findings:
        _print_findings(findings)
        print("Blocked: secrets found in devlog output; nothing written, uploaded or "
              "committed.", file=sys.stderr)
        return 1

    # 3. everything is clean: upload (publish) last, then write and commit.
    if clip:
        manual = f"gh release upload media-{sid[:7]} {clip} --clobber"
        if clip_url:
            try:
                _upload_clip(repo, sid, Path(clip))
            except (gh.GhError, OSError) as exc:
                clip_url = None
                print(f"WARNING: clip upload failed: {exc}\nUpload it manually with: {manual}",
                      file=sys.stderr)
        else:
            print(f"WARNING: clip not uploaded. Upload it manually with: {manual}",
                  file=sys.stderr)
        if clip_url != data["clip"]:
            data["clip"] = clip_url
            staged_entry.write_text(_render_entry(repo, data), "utf-8")
    for staged, final in outputs:
        final.parent.mkdir(parents=True, exist_ok=True)
        shutil.copyfile(staged, final)
    print(f"Wrote {entry.relative_to(repo).as_posix()}")
    if not commit:
        return 0
    paths = [f.relative_to(repo).as_posix() for _, f in outputs]
    _git(repo, "add", "--", *paths)
    msg = f"docs: session {sid} devlog\n\nSession: {sid}\n"
    if co_author:
        msg += f"\n{co_author}\n"
    _git(repo, "commit", "-m", msg, "--", *paths)  # only the session outputs, not other staged work
    _state_path(repo).unlink(missing_ok=True)
    return 0


# ---- milestones ----

def _latest_shots(index_text: str) -> dict[str, str]:
    """camera -> image path of its last INDEX row."""
    shots: dict[str, str] = {}
    for line in index_text.splitlines():
        cells = [c.strip() for c in line.strip().strip("|").split("|")]
        if len(cells) == 4 and re.fullmatch(r"\d{4}-\d{2}-\d{2}", cells[0]):
            shots[cells[1]] = cells[2]
    return shots


def _previous_tag(repo: Path, n: int) -> str | None:
    best = None
    for t in _git(repo, "tag", "--list", "m[0-9]*-*").split():
        m = re.match(r"m(\d+)-", t)
        if m and int(m.group(1)) < n and (best is None or int(m.group(1)) > best[0]):
            best = (int(m.group(1)), t)
    return best[1] if best else None


def _img(path: str | None) -> str:
    return f"![](../../{path})" if path else "_none_"


def milestone(repo: Path, n: int, slug: str, today: datetime.date | None = None,
              commit: bool = True) -> int:
    """Write the page, scan it, commit it, then tag (so the tag contains the page).

    With commit=False the page is written but no commit or tag is made; rerun to finish.
    """
    repo = Path(repo)
    slug = _slugify(slug)
    tag = f"m{n}-{slug}"
    if _git(repo, "tag", "--list", tag):
        raise SessionError(f"tag {tag} already exists")
    prev = _previous_tag(repo, n)
    idx = repo / INDEX
    cur = _latest_shots(idx.read_text("utf-8") if idx.is_file() else "")
    old = (_latest_shots(_git(repo, "show", f"{prev}:{INDEX.as_posix()}", check=False))
           if prev else {})
    lines = ["| camera | previous | this milestone |", "|---|---|---|"]
    for cam in sorted(cur):
        lines.append(f"| {cam} | {_img(old.get(cam))} | {_img(cur[cam])} |")
    page = repo / "docs" / "milestones" / f"{tag}.md"
    text = _render(_template(repo, "docs/milestones/TEMPLATE.md"), {
        "n": str(n), "slug": slug, "tag": tag,
        "sha": _git(repo, "rev-parse", "HEAD"),
        "date": (today or datetime.date.today()).isoformat(),
        "previous": f"`{prev}`" if prev else "none",
        "shots": "\n".join(lines) if cur else "_No captures yet._"})
    findings = secrets.scan_text(text, page.as_posix())
    if findings:
        _print_findings(findings)
        print("Blocked: secrets found in milestone page; nothing written or tagged.",
              file=sys.stderr)
        return 1
    page.parent.mkdir(parents=True, exist_ok=True)
    page.write_text(text, "utf-8")
    if commit:
        rel = page.relative_to(repo).as_posix()
        _git(repo, "add", "--", rel)
        _git(repo, "commit", "-m", f"docs: milestone {tag}", "--", rel)
        _git(repo, "tag", "-a", tag, "-m", f"Milestone {n}: {slug}")
    print(tag)
    return 0


# ---- CLI ----

def _repo_root() -> Path:
    out = subprocess.run(["git", "rev-parse", "--show-toplevel"], capture_output=True,
                         text=True, encoding="utf-8")
    if out.returncode != 0:
        raise SessionError("not inside a git repository")
    return Path(out.stdout.strip())


def _run_session(args) -> int:
    try:
        repo = _repo_root()
        if args.action == "start":
            start(repo, args.goal, commit=args.commit)
            return 0
        return end(repo, capture=args.capture, clip=args.clip,
                   contact_sheet=args.contact_sheet, capture_dir=args.capture_dir,
                   commit=not args.no_commit, co_author=args.co_author,
                   no_ocr_ok=args.no_ocr_ok)
    except SessionError as exc:
        print(f"og session: {exc}", file=sys.stderr)
        return 1


def _run_milestone(args) -> int:
    try:
        return milestone(_repo_root(), args.n, args.slug, commit=not args.no_commit)
    except SessionError as exc:
        print(f"og milestone: {exc}", file=sys.stderr)
        return 1


def add_parser(sub) -> None:
    p = sub.add_parser("session", help="devlog session routine")
    p.set_defaults(func=_run_session)
    ss = p.add_subparsers(dest="action", required=True)
    s = ss.add_parser("start", help="open a session and create its devlog entry")
    s.add_argument("goal")
    s.add_argument("--commit", action="store_true",
                   help="also create an empty 'Session-Start: <id>' commit")
    e = ss.add_parser("end", help="write the devlog entry and commit it")
    e.add_argument("--capture", action="store_true", help="run tools/ue/capture.ps1 -Cameras all")
    e.add_argument("--capture-dir", type=Path, help="use existing capture PNGs from this dir")
    e.add_argument("--clip", type=Path, help="needs <clip>.ocr.txt next to it")
    e.add_argument("--contact-sheet", type=Path, help="needs <sheet>.ocr.txt next to it")
    e.add_argument("--no-ocr-ok", action="store_true",
                   help="publish the clip or contact sheet without .ocr.txt (owner checked it)")
    e.add_argument("--no-commit", action="store_true")
    e.add_argument("--co-author", help="full 'Co-Authored-By: ...' trailer line")
    m = sub.add_parser("milestone", help="tag a milestone and write its page")
    m.add_argument("n", type=int)
    m.add_argument("slug")
    m.add_argument("--no-commit", action="store_true")
    m.set_defaults(func=_run_milestone)
