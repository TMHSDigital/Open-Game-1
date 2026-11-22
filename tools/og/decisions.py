"""Decision record: numbered ADR files, the open-decisions table, and the matching GitHub Issues."""
from __future__ import annotations

import datetime
import json
import re
import sys
from dataclasses import dataclass
from pathlib import Path

from og import gh
from og.assets import _repo_root

OPEN_DOC = Path("docs") / "OPEN-DECISIONS.md"
ADR_DIR = Path("docs") / "decisions"
LABEL = "decision"
LABEL_COLOR = "6e7781"
LABEL_DESC = "Open design or project decision"

OPEN_HEADER = ["ID", "Question", "Why it matters", "Blocks", "Options known",
               "GitHub Issue", "Status"]
RESOLVED_HEADER = ["ID", "Question", "Decision", "GitHub Issue"]

DEFAULT_TEMPLATE = """\
# {{number}}: {{title}}

Status: {{status}}
Date: {{date}}
Issue: {{issue}}
Supersedes: {{supersedes}}

## Context

{{context}}

## Decision

{{decision}}

## Consequences

{{consequences}}

## Alternatives

{{alternatives}}

## Links

{{links}}
"""

_STATUS_RX = re.compile(r"Accepted|Proposed|Superseded by \d{4}")
_ADR_FILE_RX = re.compile(r"(\d{4})-([a-z0-9-]+)\.md")
_NUM_RX = re.compile(r"#(\d+)")


@dataclass
class OpenDecision:
    id: str
    question: str
    why: str
    blocks: str
    options: str
    issue: int | None
    status: str = "Open"


@dataclass
class ResolvedDecision:
    id: str
    question: str
    adr: str  # file name inside docs/decisions
    issue: int | None


@dataclass
class Adr:
    number: int
    slug: str
    title: str
    status: str
    issue: int | None
    supersedes: int | None

    @property
    def filename(self) -> str:
        return f"{self.number:04d}-{self.slug}.md"


# ---- markdown table helpers -------------------------------------------------

def _cells(line: str) -> list[str]:
    inner = line.strip().strip("|")
    return [c.strip().replace("\\|", "|") for c in re.split(r"(?<!\\)\|", inner)]


def _esc(text: str) -> str:
    return " ".join(text.split()).replace("|", "\\|")


def _issue_cell(n: int | None, slug: str | None) -> str:
    if n is None:
        return "-"
    return f"[#{n}](https://github.com/{slug}/issues/{n})" if slug else f"#{n}"


def _issue_num(cell: str) -> int | None:
    m = _NUM_RX.search(cell)
    return int(m.group(1)) if m else None


def _table(header: list[str], rows: list[list[str]]) -> str:
    lines = ["| " + " | ".join(header) + " |", "|" + "---|" * len(header)]
    lines += ["| " + " | ".join(_esc(c) for c in r) + " |" for r in rows]
    return "\n".join(lines) + "\n"


def render_open_table(rows: list[OpenDecision], slug: str | None = None) -> str:
    return _table(OPEN_HEADER, [[r.id, r.question, r.why, r.blocks, r.options,
                                 _issue_cell(r.issue, slug), r.status] for r in rows])


def render_resolved_table(rows: list[ResolvedDecision], slug: str | None = None) -> str:
    return _table(RESOLVED_HEADER, [
        [r.id, r.question, f"[{r.adr[:-3]}](decisions/{r.adr})", _issue_cell(r.issue, slug)]
        for r in rows])


def _table_rows(md: str, ncols: int) -> list[list[str]]:
    out = []
    for line in md.splitlines():
        if line.lstrip().startswith("|"):
            c = _cells(line)
            if len(c) == ncols and re.fullmatch(r"OD-\d+", c[0]):
                out.append(c)
    return out


def parse_open(md: str) -> list[OpenDecision]:
    return [OpenDecision(c[0], c[1], c[2], c[3], c[4], _issue_num(c[5]), c[6])
            for c in _table_rows(md, len(OPEN_HEADER))]


def parse_resolved(md: str) -> list[ResolvedDecision]:
    out = []
    for c in _table_rows(md, len(RESOLVED_HEADER)):
        m = re.search(r"\(decisions/([^)\s]+)\)", c[2])
        out.append(ResolvedDecision(c[0], c[1], m.group(1) if m else c[2], _issue_num(c[3])))
    return out


def _replace_table(md: str, heading: str, table: str) -> str:
    """Replace the table under `heading` (or insert one right after it)."""
    lines = md.splitlines()
    try:
        h = next(i for i, ln in enumerate(lines) if ln.strip() == heading)
    except StopIteration:
        return md.rstrip("\n") + f"\n\n{heading}\n\n{table}"
    start = h + 1
    while start < len(lines) and not lines[start].strip():
        start += 1
    end = start
    while end < len(lines) and lines[end].lstrip().startswith("|"):
        end += 1
    new = lines[:h + 1] + [""] + table.rstrip("\n").splitlines() + lines[end:]
    return "\n".join(new) + "\n"


def _read_doc(repo: Path) -> str:
    return (repo / OPEN_DOC).read_text(encoding="utf-8")


def _write_doc(repo: Path, open_rows: list[OpenDecision],
               resolved: list[ResolvedDecision], slug: str | None) -> None:
    md = _replace_table(_read_doc(repo), "## Open", render_open_table(open_rows, slug))
    md = _replace_table(md, "## Resolved", render_resolved_table(resolved, slug))
    (repo / OPEN_DOC).write_text(md, encoding="utf-8", newline="\n")


# ---- ADR files --------------------------------------------------------------

def _front(text: str) -> tuple[str, dict[str, str]]:
    title, keys = "", {}
    for line in text.splitlines():
        if line.startswith("## "):
            break
        if line.startswith("# ") and not title:
            title = re.sub(r"^#\s*\d{4}[:.]?\s*", "", line).strip()
            continue
        m = re.match(r"(Status|Date|Issue|Supersedes):\s*(.*)$", line)
        if m:
            keys[m.group(1)] = m.group(2).strip()
    return title, keys


def list_adrs(directory: Path) -> list[Adr]:
    out = []
    for p in sorted(Path(directory).glob("[0-9][0-9][0-9][0-9]-*.md")):
        m = _ADR_FILE_RX.fullmatch(p.name)
        if not m:
            continue
        title, k = _front(p.read_text(encoding="utf-8"))
        sup = re.search(r"\d{4}", k.get("Supersedes", ""))
        out.append(Adr(int(m.group(1)), m.group(2), title, k.get("Status", ""),
                       _issue_num(k.get("Issue", "")), int(sup.group(0)) if sup else None))
    return out


def next_adr_number(repo: Path) -> int:
    """Highest number seen in files or Resolved rows, plus one: numbers are never reused."""
    seen = [a.number for a in list_adrs(repo / ADR_DIR)]
    if (repo / OPEN_DOC).exists():
        for r in parse_resolved(_read_doc(repo)):
            m = _ADR_FILE_RX.fullmatch(r.adr)
            if m:
                seen.append(int(m.group(1)))
    return max(seen, default=0) + 1


def _kebab(title: str) -> str:
    return re.sub(r"[^a-z0-9]+", "-", title.lower()).strip("-")[:60].strip("-") or "decision"


# ---- consistency ------------------------------------------------------------

def consistency(repo: Path, gh_issues: list[dict] | None) -> list[str]:
    """Problems found between the table, the ADR files and (if given) GitHub Issues."""
    repo = Path(repo)
    problems: list[str] = []
    adrs = list_adrs(repo / ADR_DIR)
    numbers = {a.number for a in adrs}
    for a in adrs:
        if not _STATUS_RX.fullmatch(a.status):
            problems.append(f"{a.filename}: bad Status {a.status!r} "
                            "(Proposed, Accepted or Superseded by NNNN)")
        m = re.fullmatch(r"Superseded by (\d{4})", a.status)
        if m and int(m.group(1)) not in numbers:
            problems.append(f"{a.filename}: superseded by {m.group(1)}, which does not exist")
        if a.supersedes is not None and a.supersedes not in numbers:
            problems.append(f"{a.filename}: supersedes {a.supersedes:04d}, which does not exist")
    if not (repo / OPEN_DOC).exists():
        return problems + [f"{OPEN_DOC.as_posix()} is missing"]
    md = _read_doc(repo)
    open_rows, resolved = parse_open(md), parse_resolved(md)
    ids = [r.id for r in open_rows] + [r.id for r in resolved]
    for i in sorted({x for x in ids if ids.count(x) > 1}):
        problems.append(f"{i}: appears more than once in {OPEN_DOC.as_posix()}")
    for r in resolved:
        if not (repo / ADR_DIR / r.adr).is_file():
            problems.append(f"{r.id}: Resolved row links {r.adr}, which does not exist")
    if gh_issues is None:
        return problems
    by_num = {int(i["number"]): i for i in gh_issues}
    for r in open_rows:
        if r.issue is None:
            problems.append(f"{r.id}: open row has no GitHub Issue (run og decisions sync-issues)")
        elif r.issue not in by_num:
            problems.append(f"{r.id}: Issue #{r.issue} not found among decision Issues")
        elif str(by_num[r.issue].get("state", "")).upper() == "CLOSED":
            problems.append(f"{r.id}: row is open but Issue #{r.issue} is closed on GitHub")
    for r in resolved:
        if r.issue in by_num and str(by_num[r.issue].get("state", "")).upper() == "OPEN":
            problems.append(f"{r.id}: resolved but Issue #{r.issue} is still open on GitHub")
    known = {r.issue for r in open_rows + resolved if r.issue is not None}
    for n, i in sorted(by_num.items()):
        if n not in known:
            problems.append(f"Issue #{n} ({i.get('title', '')}) is labelled {LABEL} "
                            "but has no row")
    return problems


# ---- GitHub Issues ----------------------------------------------------------

def _repo_flag(slug: str | None) -> list[str]:
    return ["--repo", slug] if slug else []


def _decision_issues(slug: str | None) -> list[dict]:
    out = gh._gh(["issue", "list", "--label", LABEL, "--state", "all", "--limit", "500",
                  "--json", "number,state,title", *_repo_flag(slug)])
    return json.loads(out or "[]")


def _ensure_label(slug: str | None) -> None:
    names = {x["name"] for x in json.loads(gh._gh(["label", "list", "--limit", "200", "--json",
                                                   "name", *_repo_flag(slug)]) or "[]")}
    if LABEL not in names:
        gh._gh(["label", "create", LABEL, "--color", LABEL_COLOR,
                "--description", LABEL_DESC, *_repo_flag(slug)])


def _doc_link(slug: str | None) -> str:
    if not slug:
        return "`docs/OPEN-DECISIONS.md`"
    return f"[docs/OPEN-DECISIONS.md](https://github.com/{slug}/blob/main/docs/OPEN-DECISIONS.md)"


def _bullets(options: str) -> str:
    parts = [p.strip() for p in options.split(";") if p.strip()]
    return "\n".join(f"- {p}" for p in parts) or "- not yet listed"


def _issue_body(r: OpenDecision, slug: str | None) -> str:
    url = (f"https://github.com/{slug}/blob/main/docs/OPEN-DECISIONS.md" if slug
           else "docs/OPEN-DECISIONS.md")
    return (f"**Question**\n\n{r.question}\n\n**Why it matters**\n\n{r.why}\n\n"
            f"**Blocks**\n\n{r.blocks}\n\n**Known options**\n\n{_bullets(r.options)}\n\n"
            f"Tracked in {_doc_link(slug)} ({url}). Comments here are welcome; when the "
            "decision is made it is recorded as a numbered file in `docs/decisions/` and "
            "this Issue is closed with a link to it.\n")


def _issue_title(r: OpenDecision) -> str:
    return f"{r.id}: {r.question}"


def _create_issue(r: OpenDecision, slug: str | None) -> int:
    out = gh._gh(["issue", "create", "--title", _issue_title(r), "--body", _issue_body(r, slug),
                  "--label", LABEL, *_repo_flag(slug)])
    m = re.search(r"/issues/(\d+)", out)
    if not m:
        raise gh.GhError(f"could not read the new Issue number from: {out!r}")
    return int(m.group(1))


def sync_issues(repo: Path, dry_run: bool = False) -> list[str]:
    """Create the Issue for every open row that has none; link rows to existing OD-NNN Issues."""
    repo = Path(repo)
    slug = gh.repo_slug(repo)
    md = _read_doc(repo)
    rows, resolved = parse_open(md), parse_resolved(md)
    existing = _decision_issues(slug)
    actions: list[str] = []
    todo = [r for r in rows if r.issue is None]
    labelled = False
    for r in todo:
        match = next((i for i in existing
                      if str(i["title"]).startswith(f"{r.id}:")), None)
        if match:
            actions.append(f"{r.id}: linked to existing Issue #{match['number']}")
            if not dry_run:
                r.issue = int(match["number"])
            continue
        if not labelled:
            names = {x["name"] for x in json.loads(gh._gh(
                ["label", "list", "--limit", "200", "--json", "name", *_repo_flag(slug)]) or "[]")}
            if LABEL not in names:
                actions.append(f"label {LABEL!r}: {'would create' if dry_run else 'created'}")
                if not dry_run:
                    _ensure_label(slug)
            labelled = True
        if dry_run:
            actions.append(f"{r.id}: would create Issue {_issue_title(r)!r}")
        else:
            r.issue = _create_issue(r, slug)
            actions.append(f"{r.id}: created Issue #{r.issue}")
    if not dry_run and todo:
        _write_doc(repo, rows, resolved, slug)
    for a in actions:
        print(a)
    if not actions:
        print("every open decision already has an Issue")
    return actions


def new_open(repo: Path, question: str, why: str, blocks: str, options: str) -> OpenDecision:
    repo = Path(repo)
    slug = gh.repo_slug(repo)
    md = _read_doc(repo)
    rows, resolved = parse_open(md), parse_resolved(md)
    nums = [int(r.id[3:]) for r in rows + resolved]
    row = OpenDecision(f"OD-{max(nums, default=0) + 1:03d}", question, why, blocks, options, None)
    _ensure_label(slug)
    row.issue = _create_issue(row, slug)
    _write_doc(repo, rows + [row], resolved, slug)
    return row


# ---- closing ----------------------------------------------------------------

def _template(repo: Path) -> str:
    p = repo / ADR_DIR / "TEMPLATE.md"
    return p.read_text(encoding="utf-8") if p.is_file() else DEFAULT_TEMPLATE


def close_decision(repo: Path, od_id: str, title: str, decision: str) -> Path:
    repo = Path(repo)
    slug = gh.repo_slug(repo)
    md = _read_doc(repo)
    rows, resolved = parse_open(md), parse_resolved(md)
    row = next((r for r in rows if r.id == od_id), None)
    if row is None:
        raise KeyError(f"{od_id} is not an open decision")
    number = next_adr_number(repo)
    filename = f"{number:04d}-{_kebab(title)}.md"
    rel = (ADR_DIR / filename).as_posix()
    fill = {
        "number": f"{number:04d}", "title": title, "status": "Accepted",
        "date": datetime.date.today().isoformat(),
        "issue": _issue_cell(row.issue, slug) if row.issue else "none",
        "supersedes": "none",
        "context": f"{row.question}\n\n{row.why}",
        "decision": decision,
        "consequences": "Not yet written. Edit this file to record what follows from the decision.",
        "alternatives": _bullets(row.options),
        "links": f"- Open decision: {row.id}",
    }
    text = _template(repo)
    for k, v in fill.items():
        text = text.replace("{{" + k + "}}", v)
    path = repo / rel
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(text, encoding="utf-8", newline="\n")
    rows.remove(row)
    resolved.append(ResolvedDecision(row.id, row.question, filename, row.issue))
    _write_doc(repo, rows, resolved, slug)
    if row.issue:
        url = f" (https://github.com/{slug}/blob/main/{rel})" if slug else ""
        gh._gh(["issue", "close", str(row.issue), *_repo_flag(slug),
                "--comment", f"Decided. Recorded in {rel}{url}."])
    return path


# ---- CLI --------------------------------------------------------------------

def add_parser(sub) -> None:
    p = sub.add_parser("decisions", help="decision record: ADRs, open decisions, Issues")
    dsub = p.add_subparsers(dest="decisions_command", required=True)
    n = dsub.add_parser("new-open", help="add an open decision and its Issue")
    n.add_argument("question")
    n.add_argument("--why", default="")
    n.add_argument("--blocks", default="")
    n.add_argument("--options", default="", help="';'-separated")
    n.set_defaults(func=run_new_open)
    c = dsub.add_parser("close", help="record a decision as an ADR and close its Issue")
    c.add_argument("od_id", metavar="OD-NNN")
    c.add_argument("--title", required=True)
    c.add_argument("--decision", required=True)
    c.set_defaults(func=run_close)
    k = dsub.add_parser("check", help="check table, ADR files and Issues agree")
    k.add_argument("--offline", action="store_true", help="file checks only, no GitHub")
    k.set_defaults(func=run_check)
    s = dsub.add_parser("sync-issues", help="create the missing Issue for each open row")
    s.add_argument("--dry-run", action="store_true")
    s.set_defaults(func=run_sync)


def _guard(fn) -> int:
    try:
        return fn()
    except (gh.GhError, KeyError, FileNotFoundError) as e:
        print(f"error: {e}", file=sys.stderr)
        return 2


def run_new_open(args) -> int:
    def go():
        r = new_open(_repo_root(), args.question, args.why, args.blocks, args.options)
        print(f"{r.id}: Issue #{r.issue}")
        return 0
    return _guard(go)


def run_close(args) -> int:
    def go():
        print(close_decision(_repo_root(), args.od_id, args.title, args.decision))
        return 0
    return _guard(go)


def run_sync(args) -> int:
    return _guard(lambda: (sync_issues(_repo_root(), dry_run=args.dry_run), 0)[1])


def run_check(args) -> int:
    def go():
        repo = _repo_root()
        issues = None if args.offline else _decision_issues(gh.repo_slug(repo))
        problems = consistency(repo, issues)
        for p in problems:
            print(p)
        if problems:
            print(f"{len(problems)} decision problem(s).", file=sys.stderr)
            return 1
        return 0
    return _guard(go)
