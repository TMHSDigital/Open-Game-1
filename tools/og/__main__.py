"""Entry point: python -m og <command>."""
import argparse
import sys

from og import assets, decisions, secrets, session, site

# The repo checks, in order. `og check` is the one place this list lives: the pre-commit hook, CI
# and the release gates all call it, so a check added here runs in all three.
CHECKS = ("secrets", "assets", "decisions")


def _check_argv(name: str, args) -> list[str]:
    if name == "secrets":
        argv = ["secrets", *map(str, args.paths)]
        if args.staged:
            argv.append("--staged")
        if args.tracked:
            argv.append("--tracked")
        if args.lfs_pointers_ok:
            argv.append("--lfs-pointers-ok")
        return argv
    if name == "assets":
        return ["assets", "check"]
    return ["decisions", "check", "--offline"]


def run_check(args) -> int:
    """Run every selected check (all of them even after a failure) and return 1 if any failed."""
    selected = [c for c in CHECKS if not args.only or c in args.only]
    if "secrets" in selected and not (args.staged or args.tracked or args.paths):
        print("og check: say what the secrets scan covers: --staged, --tracked or paths.",
              file=sys.stderr)
        return 2
    failed = []
    for name in selected:
        argv = _check_argv(name, args)
        print(f">> og {' '.join(argv)}", flush=True)
        if main(argv) != 0:
            failed.append(name)
    if failed:
        print(f"og check: failed: {', '.join(failed)}", file=sys.stderr)
        return 1
    return 0


def _add_check_parser(sub) -> None:
    p = sub.add_parser("check", help="run the repo checks: secrets scan, asset licenses, decisions")
    p.add_argument("paths", nargs="*", help="extra files for the secrets scan")
    p.add_argument("--staged", action="store_true", help="secrets scan of the staged files")
    p.add_argument("--tracked", action="store_true", help="secrets scan of every tracked file")
    p.add_argument("--lfs-pointers-ok", action="store_true",
                   help="with --tracked: accept LFS pointers without their content (CI)")
    p.add_argument("--only", action="append", choices=CHECKS,
                   help="run only this check (repeatable)")
    p.set_defaults(func=run_check)


def main(argv=None) -> int:
    parser = argparse.ArgumentParser(prog="og")
    sub = parser.add_subparsers(dest="command", required=True)
    secrets.add_parser(sub)
    assets.add_parser(sub)
    decisions.add_parser(sub)
    session.add_parser(sub)
    site.add_parser(sub)
    _add_check_parser(sub)
    args = parser.parse_args(argv)
    return args.func(args)


if __name__ == "__main__":
    sys.exit(main())
