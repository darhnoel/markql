#!/usr/bin/env python3
"""lxml baseline extractor for the hockey benchmark."""

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

from lxml import html

_WS_RE = re.compile(r"\s+")


def normalize_text(value: str | None) -> str:
    if not value:
        return ""
    return _WS_RE.sub(" ", value).strip()


def has_class(node, class_name: str) -> bool:
    classes = (node.get("class") or "").split()
    return class_name in classes


def first_td_by_class(row, class_name: str):
    matches = row.xpath(
        f"./td[contains(concat(' ', normalize-space(@class), ' '), ' {class_name} ')]"
    )
    return matches[0] if matches else None


def extract_team(cell) -> str:
    if cell is None:
        return ""

    links = cell.xpath(".//a[1]")
    if links:
        return normalize_text(links[0].text_content())

    cities = cell.xpath(".//span[contains(concat(' ', normalize-space(@class), ' '), ' city ')]")
    nicks = cell.xpath(".//span[contains(concat(' ', normalize-space(@class), ' '), ' nick ')]")
    if cities and nicks:
        return normalize_text((cities[0].text_content() or "") + (nicks[0].text_content() or ""))

    direct_text = normalize_text(" ".join(cell.xpath("./text()")))
    if direct_text:
        return direct_text

    return normalize_text(cell.text_content())


def extract_cell_text(row, class_name: str) -> str:
    cell = first_td_by_class(row, class_name)
    if cell is None:
        return ""
    return normalize_text(cell.text_content())


def extract_rows(table) -> tuple[list[str], list[dict[str, str]]]:
    header_names: list[str] = []
    for tr in table.xpath(".//tr"):
        ths = tr.xpath("./th")
        if ths:
            header_names = [normalize_text(th.text_content()).lower() for th in ths]
            break

    has_notes = "notes" in header_names
    columns = ["team", "w", "l", "ot", "pts"] + (["notes"] if has_notes else [])

    rows: list[dict[str, str]] = []
    for tr in table.xpath(".//tr[td]"):
        if has_class(tr, "totals"):
            continue

        row = {
            "team": extract_team(first_td_by_class(tr, "team")),
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
    parser = argparse.ArgumentParser(description="lxml hockey extractor baseline")
    parser.add_argument("--input", required=True, help="Input HTML fixture")
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
    doc = html.fromstring(html_bytes)
    t_parse1 = time.perf_counter_ns()

    tables = doc.xpath("//table[contains(concat(' ', normalize-space(@class), ' '), ' hockey-stats ')]")
    if not tables:
        raise RuntimeError("Could not locate table.hockey-stats")

    t_extract0 = time.perf_counter_ns()
    columns, rows = extract_rows(tables[0])
    t_extract1 = time.perf_counter_ns()

    t_serialize0 = time.perf_counter_ns()
    csv_bytes = to_csv_bytes(columns, rows)
    t_serialize1 = time.perf_counter_ns()

    sys.stdout.buffer.write(csv_bytes)
    t1 = time.perf_counter_ns()

    if args.metrics_out:
        metrics = {
            "engine": "lxml",
            "parser": "lxml.html",
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
