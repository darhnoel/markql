from __future__ import annotations

from collections import Counter
from html.parser import HTMLParser
from typing import Dict, List, Tuple

from ._types import Document


class _SummaryParser(HTMLParser):
    """Collects tag and attribute statistics without executing scripts."""

    def __init__(self) -> None:
        super().__init__(convert_charrefs=True)
        self.tag_counts: Counter[str] = Counter()
        self.attr_counts: Counter[str] = Counter()
        self.total_nodes = 0

    def handle_starttag(self, tag: str, attrs) -> None:
        self.total_nodes += 1
        self.tag_counts[tag] += 1
        for key, _ in attrs:
            if key:
                self.attr_counts[key] += 1


def summarize_document(doc: Document, max_nodes_preview: int) -> Dict[str, object]:
    """Produces a deterministic summary of tag and attribute usage."""

    parser = _SummaryParser()
    parser.feed(doc.html)
    tags = parser.tag_counts.most_common(max_nodes_preview)
    attrs = parser.attr_counts.most_common(max_nodes_preview)
    return {
        "total_nodes": parser.total_nodes,
        "tag_counts": tags,
        "attribute_keys": attrs,
    }
