"""CLI entry point for rendering templated query files into plain MarkQL."""

from __future__ import annotations

import argparse
import sys
from typing import List, Optional

from .rendering import RenderError, render_j2_query_file


def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(prog="python -m markql._render_cli")
    parser.add_argument("--render", required=True, choices=["j2"])
    parser.add_argument("--template", required=True)
    parser.add_argument("--vars")
    return parser


def main(argv: Optional[List[str]] = None) -> int:
    parser = build_parser()
    args = parser.parse_args(argv)

    try:
        rendered = render_j2_query_file(args.template, vars_path=args.vars)
    except RenderError as exc:
        print(str(exc), file=sys.stderr)
        return 1

    sys.stdout.write(rendered.text)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
