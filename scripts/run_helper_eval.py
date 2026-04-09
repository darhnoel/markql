#!/usr/bin/env python3
from __future__ import annotations

import argparse
import json

from markql.helper import suggest_query


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--input", required=True)
    parser.add_argument("--goal", required=True)
    args = parser.parse_args()
    result = suggest_query(input_path=args.input, goal_text=args.goal)
    print(json.dumps(result, ensure_ascii=False, indent=2))
    return 0 if result["status"] == "done" else 1


if __name__ == "__main__":
    raise SystemExit(main())
