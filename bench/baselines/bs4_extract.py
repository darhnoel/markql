#!/usr/bin/env python3
"""BeautifulSoup baseline extractor for the hockey benchmark."""

from __future__ import annotations

import argparse
import csv
import io
import json
import re
import resource
import sys
import time
from pathlib import Path

from bs4 import BeautifulSoup

_WS_RE = re.compile(r"\s+")


def normalize_text(value: str | None) -> str:
    if not value:
        return ""
    return _WS_RE.sub(" ", value).strip()


def extract_team(cell) -> str:
    if cell is None:
        return ""

    link = cell.find("a")
    if link is not None:
        return normalize_text(link.get_text(" ", strip=True))

    city = cell.find("span", class_="city")
    nick = cell.find("span", class_="nick")
    if city is not None and nick is not None:
        return normalize_text(city.get_text(strip=True) + nick.get_text(strip=True))

    direct_nodes = [str(node) for node in cell.find_all(string=True, recursive=False)]
    direct_text = normalize_text(" ".join(direct_nodes))
    if direct_text:
        return direct_text

    return normalize_text(cell.get_text(" ", strip=True))


def extract_cell_text(row, cls: str) -> str:
    cell = row.find("td", class_=cls)
    if cell is None:
        return ""
    return normalize_text(cell.get_text(" ", strip=True))


def class_tokens(row) -> set[str]:
    classes = row.get("class") or []
    return {str(token) for token in classes}


def extract_rows(table) -> tuple[list[str], list[dict[str, str]]]:
    header_names: list[str] = []
    for tr in table.find_all("tr"):
        ths = tr.find_all("th")
        if ths:
            header_names = [normalize_text(th.get_text(" ", strip=True)).lower() for th in ths]
            break

    has_notes = "notes" in header_names
    columns = ["team", "w", "l", "ot", "pts"] + (["notes"] if has_notes else [])

    rows: list[dict[str, str]] = []
    for tr in table.find_all("tr"):
        if not tr.find("td"):
            continue
        if "totals" in class_tokens(tr):
            continue

        row = {
            "team": extract_team(tr.find("td", class_="team")),
            "w": extract_cell_text(tr, "w"),
            "l": extract_cell_text(tr, "l"),
            "ot": extract_cell_text(tr, "ot"),
            "pts": extract_cell_text(tr, "pts"),
        }
        if has_notes:
            row["notes"] = extract_cell_text(tr, "notes")
        rows.append(row)

    return columns, rows


def to_csv_bytes(columns: list[str], rows: list[dict[str, str]]) -> bytes:
    out = io.StringIO(newline="")
    writer = csv.DictWriter(out, fieldnames=columns, lineterminator="\n")
    writer.writeheader()
    for row in rows:
        writer.writerow({name: row.get(name, "") for name in columns})
    return out.getvalue().encode("utf-8")


def build_arg_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(description="BeautifulSoup hockey extractor baseline")
    parser.add_argument("--input", required=True, help="Input HTML fixture")
    parser.add_argument(
        "--parser",
        default="lxml",
        choices=["lxml", "html.parser"],
        help="BeautifulSoup parser backend",
    )
    parser.add_argument(
        "--metrics-out",
        default=None,
        help="Optional JSON output path for timing breakdown metrics",
    )
    return parser


def main() -> int:
    args = build_arg_parser().parse_args()

    t0 = time.perf_counter_ns()
    html_bytes = Path(args.input).read_bytes()

    t_parse0 = time.perf_counter_ns()
    soup = BeautifulSoup(html_bytes, args.parser)
    t_parse1 = time.perf_counter_ns()

    table = soup.select_one("table.hockey-stats")
    if table is None:
        raise RuntimeError("Could not locate table.hockey-stats")

    t_extract0 = time.perf_counter_ns()
    columns, rows = extract_rows(table)
    t_extract1 = time.perf_counter_ns()

    t_serialize0 = time.perf_counter_ns()
    csv_bytes = to_csv_bytes(columns, rows)
    t_serialize1 = time.perf_counter_ns()

    sys.stdout.buffer.write(csv_bytes)
    t1 = time.perf_counter_ns()

    if args.metrics_out:
        metrics = {
            "engine": "bs4",
            "parser": args.parser,
            "parse_ns": t_parse1 - t_parse0,
            "extract_ns": t_extract1 - t_extract0,
            "serialize_ns": t_serialize1 - t_serialize0,
            "end_to_end_ns": t1 - t0,
            "rows": len(rows),
            "peak_rss_kb_self": int(resource.getrusage(resource.RUSAGE_SELF).ru_maxrss),
        }
        Path(args.metrics_out).write_text(json.dumps(metrics, indent=2), encoding="utf-8")

    return 0


if __name__ == "__main__":
    raise SystemExit(main())
