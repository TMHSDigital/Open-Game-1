"""Static public site: devlog timeline, gallery, decisions and Steam builds.

`og site build --out _site` reads docs/ and writes plain HTML (no JS framework, no external
requests, relative links only). Templates live in the repo's site/templates, so the generator
can build any docs tree, including test fixtures.
"""
from __future__ import annotations

import html
import json
import posixpath
import re
import shutil
import sys
from pathlib import Path

from markdown_it import MarkdownIt

from og import decisions, gh

SITE_DIR = Path(__file__).resolve().parents[2] / "site"
_SLOT = re.compile(r"\{\{(\w+)\}\}")
_FRONT = re.compile(r"\A---\r?\n(.*?)\r?\n---\r?\n?", re.S)
_DATE = re.compile(r"\d{4}-\d{2}-\d{2}")
_SAFE_ID = re.compile(r"[A-Za-z0-9][A-Za-z0-9._-]*")
_HTTP = re.compile(r"https?://", re.I)
_HREF = re.compile(r'href="([^"]+)"')
_NOT_RELATIVE = re.compile(r"(?:[a-z][a-z0-9+.-]*:|#|/)", re.I)
_LINK_ATTR = re.compile(r'(?:href|src)="([^"]+)"')


def _render_md(text: str) -> str:
    return MarkdownIt("commonmark", {"html": False}).enable("table").render(text)


def _esc(s) -> str:
    return html.escape(str(s), quote=True)


def _fill(template: str, values: dict[str, str]) -> str:
    return _SLOT.sub(lambda m: values.get(m.group(1), m.group(0)), template)


def _value(raw: str):
    raw = raw.strip()
    if raw in ("", "null", "~"):
        return None
    if raw.startswith("[") and raw.endswith("]"):
        return [_value(p) for p in raw[1:-1].split(",") if p.strip()]
    if len(raw) >= 2 and raw[0] == raw[-1] and raw[0] in "\"'":
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


class _Site:
    def __init__(self, repo: Path, out: Path, site_dir: Path):
        self.repo, self.out, self.site_dir = repo, out, site_dir
        self.docs = repo / "docs"
        self.skipped: list[str] = []  # devlog files left out because their front matter is bad

    def fix_links(self, body: str, src_dir: str) -> str:
        """Rewrite relative links to .md files in a rendered page one level below the site root.

        The docs link to each other by repo-relative markdown path, which works on GitHub but
        not here: only .html pages are published. A link to a decision or milestone that has a
        page goes to that page; any other .md link goes to the file on GitHub."""
        def sub(m: re.Match) -> str:
            href = html.unescape(m.group(1))
            path, sep, frag = href.partition("#")
            if _NOT_RELATIVE.match(href) or not path.endswith(".md"):
                return m.group(0)
            target = posixpath.normpath(posixpath.join(src_dir, path))
            new = None
            for folder, pattern in (("decisions", r"\d{4}-[^/]+"), ("milestones", r"m\d+-[^/]+")):
                hit = re.fullmatch(rf"docs/{folder}/({pattern})\.md", target)
                if hit and (self.docs / folder / f"{hit.group(1)}.md").is_file():
                    new = f"../{folder}/{hit.group(1)}.html{sep}{frag}"
            if new is None:
                slug = gh.repo_slug(self.repo)
                new = f"https://github.com/{slug}/blob/main/{target}{sep}{frag}" if slug else "#"
            return f'href="{html.escape(new, quote=True)}"'
        return _HREF.sub(sub, body)

    def template(self, name: str) -> str:
        return (self.site_dir / "templates" / f"{name}.html").read_text("utf-8")

    def page(self, rel: str, title: str, template: str, values: dict[str, str]) -> None:
        root = "../" * rel.count("/")
        content = _fill(self.template(template), {**values, "root": root})
        text = _fill(self.template("base"), {"title": _esc(title), "root": root,
                                              "content": content})
        dest = self.out / rel
        dest.parent.mkdir(parents=True, exist_ok=True)
        dest.write_text(text, "utf-8", newline="\n")

    def read(self, rel: str) -> str:
        p = self.docs / rel
        return p.read_text("utf-8") if p.is_file() else ""

    # ---- devlog ----

    def entries(self) -> list[dict]:
        out = []
        for p in sorted((self.docs / "devlog").glob("*.md")):
            if p.name == "TEMPLATE.md":
                continue
            meta, body = parse_front(p.read_text("utf-8"))
            if not meta.get("id") or not _DATE.match(str(meta.get("date") or "")):
                print(f"og site: skipping {p.name}: missing id or date", file=sys.stderr)
                self.skipped.append(p.name)
                continue
            if not _SAFE_ID.fullmatch(str(meta["id"])):
                print(f"og site: skipping {p.name}: unsafe id", file=sys.stderr)
                self.skipped.append(p.name)
                continue
            for key in ("decisions", "cameras"):
                if isinstance(meta.get(key), str):  # `decisions: OD-001` written without brackets
                    meta[key] = [meta[key]] if meta[key] else []
            if meta.get("clip") and not _HTTP.match(str(meta["clip"])):
                print(f"og site: {p.name}: ignoring non-http clip link", file=sys.stderr)
                meta["clip"] = None
            meta["body"] = body
            out.append(meta)
        out.sort(key=lambda e: str(e["id"]), reverse=True)
        return out

    def devlog(self) -> None:
        entries = self.entries()
        if entries:
            items = []
            for e in entries:
                bits = [_esc(e["date"])]
                if e.get("decisions"):
                    bits.append("decisions: " + _esc(", ".join(e["decisions"])))
                if e.get("cameras"):
                    bits.append(f"{len(e['cameras'])} camera(s)")
                if e.get("clip"):
                    bits.append(f'<a href="{_esc(e["clip"])}">clip</a>')
                items.append(
                    f'<li><a href="devlog/{_esc(e["id"])}.html">{_esc(e.get("goal") or e["id"])}'
                    f'</a><br><span class="meta">{" | ".join(bits)}</span></li>')
            listing = '<ul class="timeline">\n' + "\n".join(items) + "\n</ul>"
        else:
            listing = '<p class="empty">No sessions yet.</p>'
        self.page("index.html", "Devlog", "index",
                  {"entries": listing, "milestones": self.milestones()})
        for e in entries:
            meta = [_esc(e["date"])]
            for k in ("start_sha", "end_sha"):
                if e.get(k):
                    meta.append(f"{k.split('_')[0]}: {_esc(str(e[k])[:7])}")
            if e.get("clip"):
                meta.append(f'<a href="{_esc(e["clip"])}">clip</a>')
            body = self.fix_links(_render_md(e["body"]), "docs/devlog").replace(
                'src="media/', 'src="../media/')
            self.page(f"devlog/{e['id']}.html", str(e.get("goal") or e["id"]), "entry",
                      {"meta": " | ".join(meta), "body": body})

    def milestones(self) -> str:
        pages = sorted((self.docs / "milestones").glob("m[0-9]*-*.md"))
        if not pages:
            return '<p class="empty">No milestones yet.</p>'
        items = []
        for p in pages:
            body = self.fix_links(_render_md(p.read_text("utf-8")), "docs/milestones").replace(
                'src="../../docs/devlog/media/', 'src="../media/')
            self.page(f"milestones/{p.stem}.html", p.stem, "entry", {"meta": "", "body": body})
            items.append(f'<li><a href="milestones/{_esc(p.stem)}.html">{_esc(p.stem)}</a></li>')
        return '<ul class="timeline">\n' + "\n".join(items) + "\n</ul>"

    # ---- gallery ----

    def gallery(self) -> None:
        shots: dict[str, list[dict]] = {}
        for line in self.read("captures/INDEX.md").splitlines():
            cells = [c.strip() for c in line.strip().strip("|").split("|")]
            if len(cells) == 4 and _DATE.fullmatch(cells[0]):
                src = re.sub(r"^docs/devlog/", "", cells[2])
                shots.setdefault(cells[1], []).append({"date": cells[0], "src": src})
        if not shots:
            body = '<p class="empty">No captures yet.</p>'
        else:
            rows = []
            for cam in sorted(shots):
                lst = sorted(shots[cam], key=lambda s: s["date"])  # stable on ties
                data = _esc(json.dumps(lst, separators=(",", ":")))
                last = lst[-1]
                rows.append(
                    f'<section class="cam" data-camera="{_esc(cam)}" data-shots=\'{data}\'>\n'
                    f'<h2>{_esc(cam)}</h2>\n'
                    f'<img src="{_esc(last["src"])}" alt="{_esc(cam)} on {_esc(last["date"])}">\n'
                    f'<input type="range" min="0" max="{len(lst) - 1}" value="{len(lst) - 1}"'
                    f' aria-label="Shot date for {_esc(cam)}">\n'
                    f'<p class="when">{_esc(last["date"])}</p>\n</section>')
            body = "\n".join(rows)
        self.page("gallery.html", "Gallery", "gallery", {"cameras": body})

    # ---- decisions ----

    def decisions(self) -> None:
        opens = decisions.parse_open(self.read("OPEN-DECISIONS.md"))
        if opens:
            rows = "".join(
                f"<tr><td>{_esc(o.id)}</td><td>{_esc(o.question)}</td><td>{_esc(o.why)}</td>"
                f"<td>{_esc(o.blocks)}</td><td>{'#' + str(o.issue) if o.issue else '-'}</td>"
                f"</tr>\n" for o in opens)
            open_html = ("<table>\n<tr><th>ID</th><th>Question</th><th>Why it matters</th>"
                         f"<th>Blocks</th><th>Issue</th></tr>\n{rows}</table>")
        else:
            open_html = '<p class="empty">No open decisions.</p>'
        adrs = decisions.list_adrs(self.docs / "decisions")
        if adrs:
            items = []
            for a in adrs:
                text = (self.docs / "decisions" / a.filename).read_text("utf-8")
                stem = a.filename[:-3]
                self.page(f"decisions/{stem}.html", f"{a.number:04d}: {a.title}", "entry",
                          {"meta": _esc(a.status),
                           "body": self.fix_links(_render_md(text), "docs/decisions")})
                items.append(f'<li><a href="decisions/{_esc(stem)}.html">'
                             f'{a.number:04d}: {_esc(a.title)}</a><br>'
                             f'<span class="meta">{_esc(a.status)}</span></li>')
            res_html = '<ul class="timeline">\n' + "\n".join(items) + "\n</ul>"
        else:
            res_html = '<p class="empty">No resolved decisions yet.</p>'
        self.page("decisions.html", "Decisions", "decisions",
                  {"open": open_html, "resolved": res_html})

    # ---- builds ----

    def builds(self) -> None:
        header, rows = None, []
        for line in self.read("steam/BUILDS.md").splitlines():
            if not line.lstrip().startswith("|"):
                continue
            cells = [c.strip() for c in line.strip().strip("|").split("|")]
            if header is None:
                header = cells
            elif not all(re.fullmatch(r":?-+:?", c) for c in cells):
                rows.append(cells)
        if header and rows:
            th = "".join(f"<th>{_esc(h)}</th>" for h in header)
            trs = "".join("<tr>" + "".join(f"<td>{_esc(c)}</td>" for c in r) + "</tr>\n"
                          for r in reversed(rows))
            body = f"<table>\n<tr>{th}</tr>\n{trs}</table>"
        else:
            body = '<p class="empty">No builds yet.</p>'
        self.page("builds.html", "Steam builds", "builds", {"builds": body})

    # ---- assets ----

    def assets(self) -> None:
        static = self.out / "static"
        static.mkdir(parents=True, exist_ok=True)
        shutil.copyfile(self.site_dir / "static" / "site.css", static / "site.css")
        media = self.docs / "devlog" / "media"
        if media.is_dir():
            for p in media.rglob("*.webp"):
                dest = self.out / "media" / p.relative_to(media)
                dest.parent.mkdir(parents=True, exist_ok=True)
                shutil.copyfile(p, dest)


SITE_MARKER = ".og-site"


def build(repo: Path, out: Path, site_dir: Path | None = None) -> list[str]:
    """Render the site into out and return the devlog files that were skipped as malformed.

    out is deleted first, so it must be empty, missing, or a previous build (it then holds the
    SITE_MARKER file). Anything else, such as docs/ or the repository itself, is refused."""
    s = _Site(Path(repo), Path(out), Path(site_dir or SITE_DIR))
    if s.out.exists():
        if not s.out.is_dir() or (any(s.out.iterdir()) and not (s.out / SITE_MARKER).is_file()):
            raise SystemExit(f"og site build: refusing to delete {s.out}: it is not empty and "
                             "was not written by a previous site build.")
        shutil.rmtree(s.out)
    s.out.mkdir(parents=True)
    (s.out / SITE_MARKER).write_text("written by og site build\n", "utf-8")
    s.assets()
    s.devlog()
    s.gallery()
    s.decisions()
    s.builds()
    broken = broken_links(s.out)
    if broken:
        raise SystemExit("og site build: broken relative link(s), nothing should be deployed:\n"
                         + "\n".join(f"  {page}: {link}" for page, link in broken))
    return s.skipped


def broken_links(out: Path) -> list[tuple[str, str]]:
    """(page, link) for every relative href or src in the built site whose target is missing."""
    bad = []
    for page in sorted(out.rglob("*.html")):
        for link in _LINK_ATTR.findall(page.read_text("utf-8")):
            link = html.unescape(link)
            if _NOT_RELATIVE.match(link):
                continue
            if not (page.parent / link.split("#")[0]).exists():
                bad.append((page.relative_to(out).as_posix(), link))
    return bad


def _repo_root() -> Path:
    cwd = Path.cwd().resolve()
    for d in (cwd, *cwd.parents):
        if (d / ".git").exists():
            return d
    return cwd


def _run_build(args) -> int:
    skipped = build(_repo_root(), args.out)
    print(f"Wrote {args.out}")
    if skipped:
        print(f"og site build: {len(skipped)} devlog entr(y/ies) left out because of bad front "
              f"matter: {', '.join(skipped)}. Fix them; the site must not deploy without them.",
              file=sys.stderr)
        return 1
    return 0


def add_parser(sub) -> None:
    p = sub.add_parser("site", help="build the public devlog site")
    ss = p.add_subparsers(dest="site_command", required=True)
    b = ss.add_parser("build", help="render docs/ to static HTML")
    b.add_argument("--out", type=Path, default=Path("_site"))
    b.set_defaults(func=_run_build)
