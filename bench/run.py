#!/usr/bin/env python3
"""Deterministic benchmark runner for MarkQL vs Python HTML extraction stacks."""

from __future__ import annotations

import argparse
import csv
import json
import math
import os
import re
import statistics
import subprocess
import sys
import tempfile
import time
from dataclasses import dataclass
from datetime import datetime, timezone
from pathlib import Path
from typing import Any

ROOT = Path(__file__).resolve().parents[1]
BENCH = ROOT / "bench"
FIXTURES = BENCH / "fixtures" / "hockey"
GOLD = BENCH / "gold"
QUERIES = BENCH / "queries"
BASELINES = BENCH / "baselines"
RESULTS_DIR = BENCH / "results"

TIME_BIN = Path("/usr/bin/time") if Path("/usr/bin/time").exists() else None
RSS_RE = re.compile(r"Maximum resident set size \(kbytes\):\s*(\d+)")


@dataclass(frozen=True)
class Variant:
    name: str
    fixture: Path
    gold: Path


VARIANTS: list[Variant] = [
    Variant("small", FIXTURES / "small.html", GOLD / "hockey_small.csv"),
    Variant("medium", FIXTURES / "medium.html", GOLD / "hockey_medium.csv"),
    Variant("big", FIXTURES / "big.html", GOLD / "hockey_big.csv"),
]


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description="Run MarkQL benchmark suite")
    parser.add_argument("--suite", default="hockey", choices=["hockey"], help="Benchmark suite")
    parser.add_argument("--warmups", type=int, default=2, help="Warmup runs per engine and variant")
    parser.add_argument("--runs", type=int, default=7, help="Measured runs per engine and variant")
    parser.add_argument("--python", default=sys.executable, help="Python executable")
    parser.add_argument("--markql-bin", default=str(ROOT / "build" / "markql"), help="Path to markql CLI")
    parser.add_argument(
        "--results",
        default=str(RESULTS_DIR / "results.json"),
        help="Output JSON results path",
    )
    parser.add_argument(
        "--bs4-parser",
        default="lxml",
        choices=["lxml", "html.parser"],
        help="BeautifulSoup parser backend",
    )
    return parser.parse_args()


def percentile(values: list[float], p: float) -> float:
    if not values:
        raise ValueError("values must not be empty")
    ordered = sorted(values)
    index = max(0, math.ceil(p * len(ordered)) - 1)
    return ordered[index]


def ns_to_ms(value_ns: int | None) -> float | None:
    if value_ns is None:
        return None
    return round(value_ns / 1_000_000.0, 3)


def stats_from_samples(samples: list[dict[str, Any]], key: str, unit: str) -> dict[str, Any] | None:
    values = [sample[key] for sample in samples if sample.get(key) is not None]
    if not values:
        return None

    if unit == "ms":
        converted = [value / 1_000_000.0 for value in values]
    else:
        converted = [float(value) for value in values]

    return {
        "median": round(statistics.median(converted), 3),
        "p90": round(percentile(converted, 0.9), 3),
        "unit": unit,
        "samples": [round(v, 3) for v in converted],
    }


def parse_peak_rss_kb(stderr_text: str) -> int | None:
    match = RSS_RE.search(stderr_text)
    if not match:
        return None
    return int(match.group(1))


def run_command(cmd: list[str]) -> tuple[bytes, str, int, int | None]:
    wrapped = [str(TIME_BIN), "-v", *cmd] if TIME_BIN else cmd
    t0 = time.perf_counter_ns()
    proc = subprocess.run(wrapped, capture_output=True)
    t1 = time.perf_counter_ns()

    stderr_text = proc.stderr.decode("utf-8", errors="replace")
    if proc.returncode != 0:
        rendered = " ".join(cmd)
        raise RuntimeError(
            f"Command failed ({proc.returncode}): {rendered}\n"
            f"stdout:\n{proc.stdout.decode('utf-8', errors='replace')}\n"
            f"stderr:\n{stderr_text}"
        )

    peak_rss_kb = parse_peak_rss_kb(stderr_text)
    return proc.stdout, stderr_text, t1 - t0, peak_rss_kb


def csv_shape(csv_bytes: bytes) -> tuple[int, int]:
    text = csv_bytes.decode("utf-8")
    rows = list(csv.reader(text.splitlines()))
    if not rows:
        return 0, 0
    col_count = len(rows[0])
    row_count = max(0, len(rows) - 1)
    return row_count, col_count


def mismatch_hint(expected: bytes, actual: bytes) -> str:
    expected_text = expected.decode("utf-8")
    actual_text = actual.decode("utf-8")
    exp_lines = expected_text.splitlines()
    act_lines = actual_text.splitlines()

    first_diff_line = None
    for i, (exp, act) in enumerate(zip(exp_lines, act_lines), start=1):
        if exp != act:
            first_diff_line = i
            break

    if first_diff_line is None and len(exp_lines) != len(act_lines):
        first_diff_line = min(len(exp_lines), len(act_lines)) + 1

    exp_rows, exp_cols = csv_shape(expected)
    act_rows, act_cols = csv_shape(actual)

    hint_lines = [
        f"expected rows={exp_rows}, cols={exp_cols}",
        f"actual rows={act_rows}, cols={act_cols}",
    ]

    if first_diff_line is not None:
        exp_line = exp_lines[first_diff_line - 1] if first_diff_line - 1 < len(exp_lines) else "<no line>"
        act_line = act_lines[first_diff_line - 1] if first_diff_line - 1 < len(act_lines) else "<no line>"
        hint_lines.append(f"first differing line {first_diff_line}:")
        hint_lines.append(f"  expected: {exp_line}")
        hint_lines.append(f"  actual  : {act_line}")

    return "\n".join(hint_lines)


def run_markql_once(markql_bin: str, variant: Variant) -> dict[str, Any]:
    query_file = QUERIES / "hockey_table_to_csv.sql"
    output_csv = Path("/tmp/markql_hockey.csv")

    if output_csv.exists():
        output_csv.unlink()

    cmd = [
        markql_bin,
        "--mode",
        "plain",
        "--color=disabled",
        "--query-file",
        str(query_file),
        "--input",
        str(variant.fixture),
    ]
    _, _, end_to_end_ns, peak_rss_kb = run_command(cmd)

    if not output_csv.exists():
        raise RuntimeError(f"MarkQL did not create expected CSV output file: {output_csv}")

    actual_csv = output_csv.read_bytes()
    return {
        "csv_bytes": actual_csv,
        "end_to_end_ns": end_to_end_ns,
        "parse_ns": None,
        "extract_ns": None,
        "serialize_ns": None,
        "peak_rss_kb": peak_rss_kb,
    }


def run_bs4_once(python_bin: str, variant: Variant, parser_name: str) -> dict[str, Any]:
    with tempfile.NamedTemporaryFile(prefix="bench_bs4_metrics_", suffix=".json", delete=False) as tf:
        metrics_path = Path(tf.name)

    try:
        cmd = [
            python_bin,
            str(BASELINES / "bs4_extract.py"),
            "--input",
            str(variant.fixture),
            "--parser",
            parser_name,
            "--metrics-out",
            str(metrics_path),
        ]
        stdout_bytes, _, end_to_end_ns, peak_rss_kb = run_command(cmd)
        metrics = json.loads(metrics_path.read_text(encoding="utf-8"))
    finally:
        if metrics_path.exists():
            metrics_path.unlink()

    return {
        "csv_bytes": stdout_bytes,
        "end_to_end_ns": end_to_end_ns,
        "parse_ns": int(metrics["parse_ns"]),
        "extract_ns": int(metrics["extract_ns"]),
        "serialize_ns": int(metrics["serialize_ns"]),
        "peak_rss_kb": peak_rss_kb,
        "parser": parser_name,
    }


def run_lxml_once(python_bin: str, variant: Variant) -> dict[str, Any]:
    with tempfile.NamedTemporaryFile(prefix="bench_lxml_metrics_", suffix=".json", delete=False) as tf:
        metrics_path = Path(tf.name)

    try:
        cmd = [
            python_bin,
            str(BASELINES / "lxml_extract.py"),
            "--input",
            str(variant.fixture),
            "--metrics-out",
            str(metrics_path),
        ]
        stdout_bytes, _, end_to_end_ns, peak_rss_kb = run_command(cmd)
        metrics = json.loads(metrics_path.read_text(encoding="utf-8"))
    finally:
        if metrics_path.exists():
            metrics_path.unlink()

    return {
        "csv_bytes": stdout_bytes,
        "end_to_end_ns": end_to_end_ns,
        "parse_ns": int(metrics["parse_ns"]),
        "extract_ns": int(metrics["extract_ns"]),
        "serialize_ns": int(metrics["serialize_ns"]),
        "peak_rss_kb": peak_rss_kb,
        "parser": "lxml.html",
    }


def run_engine_variant(
    engine: str,
    variant: Variant,
    warmups: int,
    runs: int,
    python_bin: str,
    markql_bin: str,
    bs4_parser: str,
) -> dict[str, Any]:
    expected = variant.gold.read_bytes()
    measured_samples: list[dict[str, Any]] = []
    total = warmups + runs

    for run_idx in range(total):
        if engine == "markql":
            sample = run_markql_once(markql_bin, variant)
        elif engine == "bs4":
            sample = run_bs4_once(python_bin, variant, bs4_parser)
        elif engine == "lxml":
            sample = run_lxml_once(python_bin, variant)
        else:
            raise ValueError(f"Unsupported engine: {engine}")

        actual = sample.pop("csv_bytes")
        if actual != expected:
            hint = mismatch_hint(expected, actual)
            raise RuntimeError(
                f"Correctness mismatch for engine={engine}, variant={variant.name}\n"
                f"fixture={variant.fixture}\n"
                f"gold={variant.gold}\n{hint}"
            )

        if run_idx >= warmups:
            measured_samples.append(sample)

    result: dict[str, Any] = {
        "engine": engine,
        "variant": variant.name,
        "fixture": str(variant.fixture.relative_to(ROOT)),
        "fixture_size_bytes": variant.fixture.stat().st_size,
        "gold": str(variant.gold.relative_to(ROOT)),
        "correctness": "pass",
        "runs": len(measured_samples),
        "metrics": {
            "end_to_end_ms": stats_from_samples(measured_samples, "end_to_end_ns", "ms"),
            "parse_ms": stats_from_samples(measured_samples, "parse_ns", "ms"),
            "extract_ms": stats_from_samples(measured_samples, "extract_ns", "ms"),
            "serialize_ms": stats_from_samples(measured_samples, "serialize_ns", "ms"),
            "peak_rss_kb": stats_from_samples(measured_samples, "peak_rss_kb", "kb"),
        },
        "samples": [
            {
                "end_to_end_ms": ns_to_ms(sample.get("end_to_end_ns")),
                "parse_ms": ns_to_ms(sample.get("parse_ns")),
                "extract_ms": ns_to_ms(sample.get("extract_ns")),
                "serialize_ms": ns_to_ms(sample.get("serialize_ns")),
                "peak_rss_kb": sample.get("peak_rss_kb"),
            }
            for sample in measured_samples
        ],
    }

    if engine == "bs4":
        result["parser"] = bs4_parser
    if engine == "lxml":
        result["parser"] = "lxml.html"

    return result


def print_summary_table(results: list[dict[str, Any]]) -> None:
    headers = [
        "engine",
        "variant",
        "fixture_kb",
        "correct",
        "end_to_end_median_ms",
        "end_to_end_p90_ms",
        "parse_median_ms",
        "extract_median_ms",
        "peak_rss_median_kb",
    ]
    rows: list[list[str]] = []

    for item in results:
        metrics = item["metrics"]

        def fmt_metric(metric_name: str, field: str = "median") -> str:
            metric = metrics.get(metric_name)
            if not metric:
                return "n/a"
            return str(metric[field])

        rows.append(
            [
                item["engine"],
                item["variant"],
                str(round(item["fixture_size_bytes"] / 1024.0, 1)),
                item["correctness"],
                fmt_metric("end_to_end_ms", "median"),
                fmt_metric("end_to_end_ms", "p90"),
                fmt_metric("parse_ms", "median"),
                fmt_metric("extract_ms", "median"),
                fmt_metric("peak_rss_kb", "median"),
            ]
        )

    widths = [len(h) for h in headers]
    for row in rows:
        for i, col in enumerate(row):
            widths[i] = max(widths[i], len(col))

    def render_row(cols: list[str]) -> str:
        return "  ".join(col.ljust(widths[i]) for i, col in enumerate(cols))

    print(render_row(headers))
    print(render_row(["-" * w for w in widths]))
    for row in rows:
        print(render_row(row))


def ensure_inputs_exist(markql_bin: str) -> None:
    if not Path(markql_bin).exists():
        raise RuntimeError(f"markql binary not found: {markql_bin}")

    required_paths = [
        FIXTURES / "small.html",
        FIXTURES / "medium.html",
        FIXTURES / "big.html",
        GOLD / "hockey_small.csv",
        GOLD / "hockey_medium.csv",
        GOLD / "hockey_big.csv",
        QUERIES / "hockey_table_to_csv.sql",
        QUERIES / "hockey_table_to_table.sql",
        BASELINES / "bs4_extract.py",
        BASELINES / "lxml_extract.py",
    ]
    for path in required_paths:
        if not path.exists():
            raise RuntimeError(f"Missing required benchmark file: {path}")


def ensure_python_deps(python_bin: str, bs4_parser: str) -> None:
    check_script = (
        "import importlib.util, sys\n"
        "parser = sys.argv[1]\n"
        "missing = []\n"
        "if importlib.util.find_spec('bs4') is None:\n"
        "    missing.append('beautifulsoup4')\n"
        "if importlib.util.find_spec('lxml') is None:\n"
        "    missing.append('lxml')\n"
        "if missing:\n"
        "    print(','.join(missing))\n"
        "    raise SystemExit(2)\n"
        "if parser == 'lxml' and importlib.util.find_spec('lxml') is None:\n"
        "    print('lxml-parser-backend')\n"
        "    raise SystemExit(3)\n"
    )
    proc = subprocess.run(
        [python_bin, "-c", check_script, bs4_parser],
        capture_output=True,
        text=True,
    )
    if proc.returncode == 0:
        return

    reason = proc.stdout.strip() or proc.stderr.strip() or "unknown"
    if "beautifulsoup4" in reason or "lxml" in reason:
        raise RuntimeError(
            "Missing Python dependencies for baselines in the selected interpreter "
            f"({python_bin}): {reason}. Install with: "
            f"'{python_bin} -m pip install beautifulsoup4 lxml' "
            "or run with '--bs4-parser html.parser' if only lxml is missing."
        )
    raise RuntimeError(
        f"Could not validate Python baseline dependencies using '{python_bin}'. "
        f"Details: {reason}"
    )


def main() -> int:
    args = parse_args()
    ensure_inputs_exist(args.markql_bin)
    ensure_python_deps(args.python, args.bs4_parser)

    if args.suite != "hockey":
        raise RuntimeError(f"Unsupported suite: {args.suite}")

    if args.warmups < 0 or args.runs <= 0:
        raise RuntimeError("--warmups must be >= 0 and --runs must be > 0")

    engines = ["markql", "bs4", "lxml"]
    results: list[dict[str, Any]] = []

    started = time.perf_counter_ns()
    for engine in engines:
        for variant in VARIANTS:
            print(f"Running engine={engine} variant={variant.name} ...", flush=True)
            item = run_engine_variant(
                engine=engine,
                variant=variant,
                warmups=args.warmups,
                runs=args.runs,
                python_bin=args.python,
                markql_bin=args.markql_bin,
                bs4_parser=args.bs4_parser,
            )
            results.append(item)

    finished = time.perf_counter_ns()

    RESULTS_DIR.mkdir(parents=True, exist_ok=True)
    results_path = Path(args.results)
    results_path.parent.mkdir(parents=True, exist_ok=True)

    payload = {
        "suite": args.suite,
        "timestamp_utc": datetime.now(timezone.utc).isoformat(),
        "config": {
            "warmups": args.warmups,
            "runs": args.runs,
            "python": args.python,
            "markql_bin": args.markql_bin,
            "bs4_parser": args.bs4_parser,
            "time_binary": str(TIME_BIN) if TIME_BIN else None,
            "cwd": os.getcwd(),
        },
        "total_elapsed_ms": ns_to_ms(finished - started),
        "results": results,
    }
    results_path.write_text(json.dumps(payload, indent=2), encoding="utf-8")

    print("\nSummary:")
    print_summary_table(results)
    print(f"\nWrote machine-readable results to: {results_path}")
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except RuntimeError as exc:
        print(f"Benchmark failed: {exc}", file=sys.stderr)
        raise SystemExit(1)
