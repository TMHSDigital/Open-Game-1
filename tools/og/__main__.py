"""Entry point: python -m og <command>. Later tasks register more commands."""
import argparse
import sys

from og import assets, decisions, secrets


def main(argv=None) -> int:
    parser = argparse.ArgumentParser(prog="og")
    sub = parser.add_subparsers(dest="command", required=True)
    secrets.add_parser(sub)
    assets.add_parser(sub)
    decisions.add_parser(sub)
    args = parser.parse_args(argv)
    return args.func(args)


if __name__ == "__main__":
    sys.exit(main())
