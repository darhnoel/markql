from __future__ import annotations

import argparse
import json
import sys

from .orchestrator import explain_query, repair_query, suggest_query


def _build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(prog="markql-helper")
    subparsers = parser.add_subparsers(dest="command", required=True)
    for name in ("suggest", "repair", "explain"):
        sub = subparsers.add_parser(name)
        sub.add_argument("--input", required=True, dest="input_path")
        sub.add_argument("--goal", required=True, dest="goal_text")
        sub.add_argument("--query", default="")
        sub.add_argument("--constraint", action="append", default=[])
        sub.add_argument("--field", action="append", default=[], dest="expected_fields")
    return parser


def main(argv: list[str] | None = None) -> int:
    args = _build_parser().parse_args(argv)
    if args.command == "suggest":
        result = suggest_query(
            input_path=args.input_path,
            goal_text=args.goal_text,
            constraints=args.constraint,
            expected_fields=args.expected_fields,
        )
    elif args.command == "repair":
        result = repair_query(
            input_path=args.input_path,
            goal_text=args.goal_text,
            query=args.query,
            constraints=args.constraint,
            expected_fields=args.expected_fields,
        )
    else:
        result = explain_query(
            input_path=args.input_path,
            goal_text=args.goal_text,
            query=args.query,
            constraints=args.constraint,
            expected_fields=args.expected_fields,
        )
    json.dump(result, sys.stdout, ensure_ascii=False, indent=2)
    sys.stdout.write("\n")
    return 0 if result["status"] == "done" else 1


if __name__ == "__main__":
    raise SystemExit(main())
