#!/usr/bin/env python3

from __future__ import annotations

import argparse
import pathlib
import subprocess
import sys


SOURCE_SUFFIXES = {
    ".c",
    ".cc",
    ".cpp",
    ".cxx",
    ".h",
    ".hh",
    ".hpp",
    ".ipp",
    ".tpp",
    ".py",
    ".js",
    ".ts",
    ".tsx",
    ".sh",
    ".bash",
    ".zsh",
    ".cmake",
}
SOURCE_FILENAMES = {
    "CMakeLists.txt",
}
EXCLUDED_PREFIXES = (
    ".github/",
    ".vcpkg/",
    "build/",
    "node_modules/",
    "third_party/",
    "vendor/",
    "vcpkg_installed/",
)


def repo_root() -> pathlib.Path:
    return pathlib.Path(__file__).resolve().parents[2]


def tracked_files(root: pathlib.Path) -> list[pathlib.Path]:
    output = subprocess.check_output(
        ["git", "ls-files", "-z"],
        cwd=root,
    )
    return [root / pathlib.Path(entry) for entry in output.decode("utf-8").split("\0") if entry]


def is_source_file(root: pathlib.Path, path: pathlib.Path) -> bool:
    rel = path.relative_to(root).as_posix()
    if any(rel.startswith(prefix) for prefix in EXCLUDED_PREFIXES):
        return False
    if path.name in SOURCE_FILENAMES:
        return True
    return path.suffix.lower() in SOURCE_SUFFIXES


def count_lines(path: pathlib.Path) -> int:
    with path.open("r", encoding="utf-8", errors="ignore") as handle:
        return sum(1 for _ in handle)


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description="Fail when a tracked source file reaches or exceeds the LOC threshold."
    )
    parser.add_argument(
        "--threshold",
        type=int,
        default=1000,
        help="Maximum allowed LOC per tracked source file is threshold - 1. Default: %(default)s.",
    )
    return parser.parse_args()


def main() -> int:
    args = parse_args()
    if args.threshold <= 0:
        print("error: --threshold must be positive.", file=sys.stderr)
        return 2

    root = repo_root()
    violations: list[tuple[int, str]] = []
    scanned = 0

    for path in tracked_files(root):
        if not is_source_file(root, path):
            continue
        scanned += 1
        line_count = count_lines(path)
        if line_count >= args.threshold:
            violations.append((line_count, path.relative_to(root).as_posix()))

    if violations:
        print(
            f"LOC guardrail failed: tracked source files must stay below {args.threshold} lines.",
            file=sys.stderr,
        )
        for line_count, rel in sorted(violations, reverse=True):
            print(f"  {line_count:5d} {rel}", file=sys.stderr)
        return 1

    print(
        f"LOC guardrail passed: scanned {scanned} tracked source file(s); "
        f"none reached {args.threshold} lines."
    )
    return 0


if __name__ == "__main__":
    sys.exit(main())
