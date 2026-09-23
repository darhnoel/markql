"""Local structural evidence using the same native parser as query execution."""
from __future__ import annotations

import copy
import json
from dataclasses import dataclass
from pathlib import Path
from typing import Union

from ._loader import load_html_source
from ._security import FetchPolicy
from ._types import Document


@dataclass(frozen=True)
class StructuralEvidence:
    """Versioned, advisory evidence; candidate IDs belong to one snapshot only."""

    _payload: dict

    def to_dict(self) -> dict:
        return copy.deepcopy(self._payload)

    def to_json(self) -> str:
        return json.dumps(self._payload, ensure_ascii=False)


def inspect(
    source: Union[Document, str, bytes, Path], *, max_bytes: int = 5_000_000
) -> StructuralEvidence:
    """Inspect local HTML, bytes, a path, or a Document. Never fetch URLs."""
    from . import _core, _require_core

    _require_core()
    if isinstance(max_bytes, bool) or not isinstance(max_bytes, int) or max_bytes <= 0:
        raise ValueError('max_bytes must be a positive integer')
    value = source.html.encode('utf-8') if isinstance(source, Document) else source
    policy = FetchPolicy(allow_network=False, allow_private_network=False, timeout=10, max_bytes=max_bytes)
    html, _ = load_html_source(value, base_dir=None, policy=policy)
    return StructuralEvidence(_core.inspect_document(html))
