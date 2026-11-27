"""Static public site: a home page for the game, the devlog timeline, gallery, decisions and Steam builds.

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
from og.repo import repo_root as _repo_root
from og.text import SLOT as _SLOT, fill as _fill
from og.devlog import parse_front, parse_index

SITE_DIR = Path(__file__).resolve().parents[2] / "site"
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


DESCRIPTION = ("Combat Simulator, a physics-driven combat simulator for Steam built in public with "
               "Unreal Engine 5.8: every decision, work session and build is published here.")


def readme_section(text: str, heading: str) -> str:
    """The markdown under `## <heading>` in a README, up to the next `## ` heading; empty if absent."""
    m = re.search(rf"^## {re.escape(heading)}\s*$(.*?)(?=^## |\Z)", text, re.M | re.S)
    return m.group(1).strip() if m else ""


class _Site:
    def __init__(self, repo: Path, out: Path, site_dir: Path):
        self.repo, self.out, self.site_dir = repo, out, site_dir
        self.docs = repo / "docs"
        self.skipped: list[str] = []  # devlog files left out because their front matter is bad
        self.pages: list[str] = []  # every page written, for the sitemap
        slug = gh.repo_slug(repo)
        # The published address (a project site under /<repo>/), for absolute links: the feed,
        # social previews and the sitemap. Empty without a GitHub origin (a test fixture).
        self.base = ""
        if slug:
            owner, name = slug.split("/", 1)
            self.base = f"https://{owner.lower()}.github.io/{name}/"
        self.slug = slug

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

    def page(self, rel: str, title: str, template: str, values: dict[str, str],
             description: str = DESCRIPTION, nav: str = "") -> None:
        root = "../" * rel.count("/")
        content = _fill(self.template(template), {**values, "root": root})
        # Social previews need absolute addresses; without a published base they are left out.
        social = ""
        if self.base:
            image = f"{self.base}static/img/fight-kick.webp"
            social = (f'<meta property="og:type" content="website">\n'
                      f'<meta property="og:site_name" content="Combat Simulator">\n'
                      f'<meta property="og:title" content="{_esc(title)}">\n'
                      f'<meta property="og:description" content="{_esc(description)}">\n'
                      f'<meta property="og:url" content="{_esc(self.base + rel)}">\n'
                      f'<meta property="og:image" content="{_esc(image)}">\n'
                      f'<meta name="twitter:card" content="summary_large_image">')
        repo_link = (f'<a href="https://github.com/{self.slug}">Source on GitHub</a>' if self.slug else "")
        text = _fill(self.template("base"), {"title": _esc(title), "root": root, "content": content,
                                              "description": _esc(description), "social": social,
                                              "repo": repo_link})
        # The current section is marked in the navigation, for the eye and for screen readers.
        if nav:
            text = text.replace(f'data-nav="{nav}"', f'data-nav="{nav}" aria-current="page"', 1)
        dest = self.out / rel
        dest.parent.mkdir(parents=True, exist_ok=True)
        dest.write_text(text, "utf-8", newline="\n")
        self.pages.append(rel)

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

    @staticmethod
    def timeline(entries: list[dict]) -> str:
        """The devlog list: goal, date, decisions touched, cameras and clip, newest first."""
        if not entries:
            return '<p class="empty">No sessions yet.</p>'
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
        return '<ul class="timeline">\n' + "\n".join(items) + "\n</ul>"

    def devlog(self) -> list[dict]:
        entries = self.entries()
        self.feed(entries)
        self.page("devlog.html", "Devlog", "devlog",
                  {"entries": self.timeline(entries), "milestones": self.milestones()},
                  "Every work session on Combat Simulator, newest first: the goal, what changed, the "
                  "decisions it touched and the captures of the build.", nav="devlog")
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
                      {"meta": " | ".join(meta), "body": body},
                      f"Combat Simulator devlog, session {e['id']}: {e.get('goal') or ''}", nav="devlog")
        return entries

    def feed(self, entries: list[dict]) -> None:
        """feed.xml: an Atom feed of the devlog, newest first, so the build can be followed
        without a GitHub account. Feed links must be absolute, so the Pages address is derived
        from the origin; with no GitHub origin an empty feed is written (the link in every
        page still resolves)."""
        base = self.base
        newest = sorted(entries, key=lambda e: (str(e["date"]), str(e["id"])), reverse=True)[:30]
        items = []
        for e in newest if base else []:
            url = f"{base}devlog/{e['id']}.html"
            items.append(
                "<entry>"
                f"<title>{_esc(str(e.get('goal') or e['id']))}</title>"
                f'<link href="{_esc(url)}"/>'
                f"<id>{_esc(url)}</id>"
                f"<updated>{_esc(str(e['date'])[:10])}T00:00:00Z</updated>"
                f"<summary>Devlog entry {_esc(str(e['id']))}: {_esc(str(e.get('goal') or ''))}</summary>"
                "</entry>")
        updated = f"{str(newest[0]['date'])[:10]}T00:00:00Z" if newest else "1970-01-01T00:00:00Z"
        text = ('<?xml version="1.0" encoding="utf-8"?>\n'
                '<feed xmlns="http://www.w3.org/2005/Atom">'
                "<title>Combat Simulator devlog</title>"
                f"<id>{_esc(base or 'urn:combat-simulator:devlog')}</id>"
                f"<updated>{updated}</updated>"
                "<author><name>Combat Simulator</name></author>"
                + (f'<link href="{_esc(base)}"/><link rel="self" href="{_esc(base)}feed.xml"/>'
                   if base else "")
                + "".join(items) + "</feed>\n")
        (self.out / "feed.xml").write_text(text, "utf-8", newline="\n")

    def milestones(self) -> str:
        pages = sorted((self.docs / "milestones").glob("m[0-9]*-*.md"))
        if not pages:
            return '<p class="empty">No milestones yet.</p>'
        items = []
        for p in pages:
            body = self.fix_links(_render_md(p.read_text("utf-8")), "docs/milestones").replace(
                'src="../../docs/devlog/media/', 'src="../media/')
            self.page(f"milestones/{p.stem}.html", p.stem, "entry", {"meta": "", "body": body}, nav="devlog")
            items.append(f'<li><a href="milestones/{_esc(p.stem)}.html">{_esc(p.stem)}</a></li>')
        return '<ul class="timeline">\n' + "\n".join(items) + "\n</ul>"

    # ---- gallery ----

    def gallery(self) -> None:
        shots: dict[str, list[dict]] = {}
        for row in parse_index(self.read("captures/INDEX.md")):
            src = re.sub(r"^docs/devlog/", "", row.path)
            shots.setdefault(row.camera, []).append({"date": row.date, "src": src})
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
        self.page("gallery.html", "Gallery", "gallery", {"cameras": body},
                  "The same fixed cameras photograph Combat Simulator every session: step through each "
                  "camera's shots to watch the game take shape.", nav="gallery")

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
                           "body": self.fix_links(_render_md(text), "docs/decisions")},
                          f"Combat Simulator decision {a.number:04d}: {a.title}", nav="decisions")
                items.append(f'<li><a href="decisions/{_esc(stem)}.html">'
                             f'{a.number:04d}: {_esc(a.title)}</a><br>'
                             f'<span class="meta">{_esc(a.status)}</span></li>')
            res_html = '<ul class="timeline">\n' + "\n".join(items) + "\n</ul>"
        else:
            res_html = '<p class="empty">No resolved decisions yet.</p>'
        self.page("decisions.html", "Decisions", "decisions",
                  {"open": open_html, "resolved": res_html},
                  "What Combat Simulator has settled and why, what was rejected, and the questions "
                  "still open for comment.", nav="decisions")

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
            slug = gh.repo_slug(self.repo)
            col = {name: i for i, name in enumerate(header)}

            def cell(row: list[str], name: str) -> str:
                return row[col[name]] if name in col and col[name] < len(row) else ""

            def render(row: list[str], prev: list[str] | None) -> str:
                """One table row: the commit is a link, and a last cell says what changed."""
                version, sha = cell(row, "version"), cell(row, "git_sha")
                tds = []
                for i, c in enumerate(row):
                    if slug and header[i] == "git_sha" and re.fullmatch(r"[0-9a-f]{7,40}", c):
                        c = (f'<a href="https://github.com/{slug}/commit/{c}">'
                             f"<code>{c[:7]}</code></a>")
                    else:
                        c = _esc(c)
                    tds.append(f"<td>{c}</td>")
                links = []
                prev_sha = cell(prev, "git_sha") if prev else ""
                if slug and prev_sha and sha:
                    links.append(f'<a href="https://github.com/{slug}/compare/{prev_sha}...{sha}">'
                                 f'changes since {_esc(cell(prev, "version"))}</a>')
                if slug and re.fullmatch(r"\d+\.\d+\.\d+", version):
                    links.append(f'<a href="https://github.com/{slug}/releases/tag/v{version}">'
                                 "release notes</a>")
                tds.append(f"<td>{' | '.join(links)}</td>")
                return "<tr>" + "".join(tds) + "</tr>\n"

            th = "".join(f"<th>{_esc(h)}</th>" for h in header) + "<th>what changed</th>"
            trs = "".join(render(r, rows[i - 1] if i else None)
                          for i, r in reversed(list(enumerate(rows))))
            body = f"<table>\n<tr>{th}</tr>\n{trs}</table>"
        else:
            body = '<p class="empty">No builds yet.</p>'
        self.page("builds.html", "Steam builds", "builds", {"builds": body},
                  "Every Combat Simulator build uploaded to Steam, and when it went live on each branch.",
                  nav="builds")

    # ---- home ----

    def latest_shots(self) -> dict[str, dict]:
        """The newest capture of each camera: {camera: {"date", "src"}}."""
        latest: dict[str, dict] = {}
        for row in parse_index(self.read("captures/INDEX.md")):
            src = re.sub(r"^docs/devlog/", "", row.path)
            if row.camera not in latest or row.date >= latest[row.camera]["date"]:
                latest[row.camera] = {"date": row.date, "src": src}
        return latest

    def home(self, entries: list[dict]) -> None:
        """index.html: what the game is and what is in it now, from the README and docs/, so the
        page says nothing the repository does not."""
        readme_path = self.repo / "README.md"
        readme = readme_path.read_text("utf-8") if readme_path.is_file() else ""
        in_build = readme_section(readme, "In the build")
        in_build_html = (self.fix_links(_render_md(in_build), "") if in_build
                         else '<p class="empty">Nothing described yet.</p>')

        # Status: the newest decision, build and session, and the questions open.
        status = []
        adrs = decisions.list_adrs(self.docs / "decisions") if (self.docs / "decisions").is_dir() else []
        if adrs:
            a = adrs[-1]
            status.append(("Decision", f'<a href="decisions/{_esc(a.filename[:-3])}.html">'
                                              f'{a.number:04d}: {_esc(a.title)}</a>'))
        builds = [line.split("|") for line in self.read("steam/BUILDS.md").splitlines()
                  if re.match(r"^\|\s*\d+\.\d+\.\d+\s*\|", line)]
        if builds:
            version = builds[-1][1].strip()
            status.append(("Steam", f'Build <a href="builds.html">{_esc(version)}</a> on the private '
                                    "development branch; not public yet"))
        if entries:
            e = entries[0]
            status.append(("Last session", f'<a href="devlog/{_esc(e["id"])}.html">'
                                             f'{_esc(e.get("goal") or e["id"])}</a> ({_esc(e["date"])})'))
        opens = decisions.parse_open(self.read("OPEN-DECISIONS.md"))
        status.append(("Open", f'<a href="decisions.html">{len(opens)} questions open for comment</a>'))
        status_html = "".join(f"<dt>{k}</dt><dd>{v}</dd>" for k, v in status)

        # The newest captures of a few fixed cameras: the game as it is today.
        shots = self.latest_shots()
        picks = [c for c in ("plane-overview", "tp-owner-shoulder", "gym-overview", "char-front") if c in shots]
        captures = "".join(
            f'<figure><img src="{_esc(shots[c]["src"])}" alt="{_esc(c)}, {_esc(shots[c]["date"])}" '
            f'loading="lazy"><figcaption>{_esc(c)} <span class="meta">{_esc(shots[c]["date"])}</span>'
            f"</figcaption></figure>" for c in picks)
        captures_html = (f'<div class="shots">{captures}</div>' if captures
                         else '<p class="empty">No captures yet.</p>')

        self.page("index.html", "Home", "home",
                  {"in_build": in_build_html, "status": status_html, "recent": self.timeline(entries[:6]),
                   "captures": captures_html, "sessions": str(len(entries)), "decisions": str(len(adrs)),
                   "builds": str(len(builds))}, nav="home")

    def extras(self) -> None:
        """The not-found page, robots.txt and the sitemap."""
        self.page("404.html", "Not found", "notfound", {}, "This page does not exist on the Combat Simulator site.")
        robots = "User-agent: *\nAllow: /\n"
        if self.base:
            robots += f"Sitemap: {self.base}sitemap.xml\n"
            urls = "".join(f"<url><loc>{_esc(self.base + rel)}</loc></url>"
                           for rel in sorted(self.pages) if rel != "404.html")
            (self.out / "sitemap.xml").write_text(
                '<?xml version="1.0" encoding="utf-8"?>\n'
                f'<urlset xmlns="http://www.sitemaps.org/schemas/sitemap/0.9">{urls}</urlset>\n',
                "utf-8", newline="\n")
        (self.out / "robots.txt").write_text(robots, "utf-8", newline="\n")

    # ---- assets ----

    def assets(self) -> None:
        # Everything in site/static (the stylesheet, the favicon, the stills of the game).
        shutil.copytree(self.site_dir / "static", self.out / "static")
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
    entries = s.devlog()
    s.gallery()
    s.decisions()
    s.builds()
    s.home(entries)
    s.extras()
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
