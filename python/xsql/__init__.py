"""Python interface for XSQL query execution with safety-first defaults."""

from __future__ import annotations

import json
import os
import re
import shutil
import subprocess
from pathlib import Path
from typing import Any, Dict, Optional

from ._loader import load_html_source
from ._meta import __version__
from ._security import FetchPolicy
from ._summary import summarize_document
from ._types import Document, ExportSink, QueryResult, TableResult

try:
    from . import _core
except Exception as exc:  # pragma: no cover - surfaced in import-time errors.
    _core = None
    _core_import_error = exc
else:
    _core_import_error = None

doc: Optional[Document] = None


def _require_core() -> None:
    if _core is None:
        raise RuntimeError("xsql native module is unavailable") from _core_import_error


def _resolve_cli_binary() -> Optional[str]:
    env_cli = os.environ.get("XSQL_CLI")
    if env_cli:
        return env_cli
    cli = shutil.which("markql") or shutil.which("xsql")
    if cli:
        return cli
    repo_root = Path(__file__).resolve().parents[2]
    for candidate in (repo_root / "build" / "markql", repo_root / "build" / "xsql"):
        if candidate.exists():
            return str(candidate)
    return None


def _run_cli(args: list[str]) -> str:
    cli = _resolve_cli_binary()
    if not cli:
        raise RuntimeError(
            "xsql native module is missing required APIs and markql CLI was not found for fallback"
        )
    result = subprocess.run([cli, *args], capture_output=True, text=True, check=False)
    if result.returncode not in (0, 1):
        stderr = result.stderr.strip()
        stdout = result.stdout.strip()
        detail = stderr or stdout or f"exit code {result.returncode}"
        raise RuntimeError(f"markql CLI fallback failed: {detail}")
    return result.stdout


def load(
    source: Any,
    *,
    base_dir: Optional[str] = None,
    allow_network: bool = False,
    allow_private_network: bool = False,
    timeout: int = 10,
    max_bytes: int = 5_000_000,
) -> Document:
    """Loads HTML into a module-level Document for reuse across queries.

    This function MUST be treated as not thread-safe because it mutates xsql.doc.
    It performs IO for file/URL sources and enforces size/network limits.
    """

    policy = FetchPolicy(
        allow_network=allow_network,
        allow_private_network=allow_private_network,
        timeout=timeout,
        max_bytes=max_bytes,
    )
    html, origin = load_html_source(source, base_dir=base_dir, policy=policy)
    document = Document(html=html, source=origin)
    globals()["doc"] = document
    return document


def summarize(doc: Document, *, max_nodes_preview: int = 50) -> Dict[str, object]:
    """Summarizes a document by tags and attribute keys without executing scripts.

    The summary MUST avoid disclosing paths beyond the provided document source.
    It performs parsing but does not execute or fetch external resources.
    """

    return summarize_document(doc, max_nodes_preview)


def execute(
    query: str,
    *,
    source: Any = None,
    doc: Optional[Document] = None,
    params: Optional[Dict[str, Any]] = None,
    allow_network: bool = False,
    base_dir: Optional[str] = None,
    timeout: int = 10,
    max_bytes: int = 5_000_000,
    max_results: int = 10_000,
) -> QueryResult:
    """Executes an XSQL query against a document or source with safety limits.

    The query MUST be executed without eval/exec and respects max_results limits.
    It may read files or URLs when a source is provided and allow_network permits it.
    """

    _require_core()
    if params:
        raise ValueError("params are not supported by XSQL execution")
    active = doc or globals().get("doc")
    if active is None:
        if source is None:
            raise ValueError("No document loaded; provide doc or source")
        policy = FetchPolicy(
            allow_network=allow_network,
            allow_private_network=False,
            timeout=timeout,
            max_bytes=max_bytes,
        )
        html, origin = load_html_source(source, base_dir=base_dir, policy=policy)
        active = Document(html=html, source=origin)
    raw = _core.execute_from_document(active.html, query)
    rows = raw.get("rows", [])
    tables = []
    for table in raw.get("tables", []):
        tables.append(TableResult(node_id=table["node_id"], rows=table["rows"]))
    if len(rows) > max_results:
        raise ValueError("Query result exceeds max_results")
    total_table_rows = sum(len(table.rows) for table in tables)
    if total_table_rows > max_results:
        raise ValueError("Table result exceeds max_results")
    export = raw.get("export_sink", {})
    export_sink = ExportSink(kind=export.get("kind", "none"), path=export.get("path", ""))
    return QueryResult(
        columns=raw.get("columns", []),
        warnings=raw.get("warnings", []),
        rows=rows,
        tables=tables,
        to_list=raw.get("to_list", False),
        to_table=raw.get("to_table", False),
        table_has_header=raw.get("table_has_header", True),
        export_sink=export_sink,
    )


def lint(query: str) -> list[dict]:
    """Parses + validates a query and returns diagnostics without execution."""
    _require_core()
    if hasattr(_core, "lint_query"):
        return list(_core.lint_query(query))
    raw = _run_cli(["--lint", query, "--format", "json"]).strip()
    if not raw:
        return []
    try:
        parsed = json.loads(raw)
    except json.JSONDecodeError as exc:
        raise RuntimeError("Failed to parse lint diagnostics JSON from markql CLI fallback") from exc
    if not isinstance(parsed, list):
        raise RuntimeError("Unexpected lint diagnostics shape from markql CLI fallback")
    return parsed


def core_version() -> str:
    """Returns core version + provenance string (version + commit, dirty if applicable)."""
    _require_core()
    if hasattr(_core, "core_version"):
        return str(_core.core_version())
    return _run_cli(["--version"]).strip()


def core_version_info() -> Dict[str, Any]:
    """Returns structured core version/provenance metadata."""
    _require_core()
    if hasattr(_core, "core_version_info"):
        return dict(_core.core_version_info())
    rendered = core_version()
    version_match = re.search(r"\b(\d+\.\d+\.\d+)\b", rendered)
    if not version_match:
        raise RuntimeError(f"Unable to parse version from core provenance string: {rendered}")
    version = version_match.group(1)
    provenance_match = re.search(r"\(([^)]+)\)", rendered)
    git_token = provenance_match.group(1) if provenance_match else "unknown"
    git_dirty = git_token.endswith("-dirty")
    git_commit = git_token[:-6] if git_dirty else git_token
    return {
        "version": version,
        "git_commit": git_commit,
        "git_dirty": git_dirty,
        "provenance": rendered,
    }


__all__ = [
    "Document",
    "QueryResult",
    "TableResult",
    "ExportSink",
    "__version__",
    "doc",
    "load",
    "summarize",
    "execute",
    "lint",
    "core_version",
    "core_version_info",
]
