from __future__ import annotations

import os
import pathlib
import re
import urllib.request
from html.parser import HTMLParser
from typing import Optional, Tuple, Union
from urllib.error import HTTPError, URLError

from ._security import FetchPolicy, apply_redirect, is_url, normalize_path, validate_url_target

_HTML_HINT = re.compile(r"<[^>]+>")
_MAX_NODES = 100_000
_ALLOWED_CONTENT_TYPES = {
    "text/html",
    "application/xhtml+xml",
    "application/xml",
    "text/xml",
}


class _NodeCountingParser(HTMLParser):
    """Counts HTML start tags to cap parser growth before execution."""

    def __init__(self, max_nodes: int) -> None:
        super().__init__(convert_charrefs=True)
        self.max_nodes = max_nodes
        self.count = 0

    def handle_starttag(self, tag: str, attrs) -> None:
        self.count += 1
        if self.count > self.max_nodes:
            raise ValueError("HTML exceeds maximum node limit")


class _NoRedirect(urllib.request.HTTPRedirectHandler):
    """Disables automatic redirect handling so each hop can be validated."""

    def redirect_request(self, req, fp, code, msg, headers, newurl):
        return None


def _enforce_max_nodes(html: str, max_nodes: int) -> None:
    parser = _NodeCountingParser(max_nodes)
    parser.feed(html)


def _read_bytes_limited(path: str, max_bytes: int) -> bytes:
    size = os.path.getsize(path)
    if size > max_bytes:
        raise ValueError("File exceeds maximum allowed size")
    with open(path, "rb") as handle:
        data = handle.read(max_bytes + 1)
    if len(data) > max_bytes:
        raise ValueError("File exceeds maximum allowed size")
    return data


def _decode_html(data: bytes) -> str:
    return data.decode("utf-8", errors="replace")


def _validate_content_type(content_type: Optional[str]) -> None:
    if not content_type:
        return
    content_main = content_type.split(";", 1)[0].strip().lower()
    if content_main not in _ALLOWED_CONTENT_TYPES:
        raise ValueError("Unsupported content-type for HTML fetch")


def _fetch_url(url: str, policy: FetchPolicy) -> Tuple[str, str]:
    if not policy.allow_network:
        raise ValueError("Network access is disabled by default")
    validate_url_target(url, policy.allow_private_network)
    current = url
    opener = urllib.request.build_opener(_NoRedirect)
    for _ in range(policy.max_redirects + 1):
        try:
            request = urllib.request.Request(current, headers={"User-Agent": "xsql-python/1.0"})
            with opener.open(request, timeout=policy.timeout) as response:
                if 300 <= response.status < 400:
                    location = response.headers.get("Location")
                    if not location:
                        raise ValueError("Redirect response missing Location header")
                    current = apply_redirect(current, location)
                    validate_url_target(current, policy.allow_private_network)
                    continue
                _validate_content_type(response.headers.get("Content-Type"))
                data = response.read(policy.max_bytes + 1)
                if len(data) > policy.max_bytes:
                    raise ValueError("Downloaded HTML exceeds max_bytes")
                return _decode_html(data), current
        except HTTPError as err:
            if 300 <= err.code < 400:
                location = err.headers.get("Location")
                if not location:
                    raise ValueError("Redirect response missing Location header") from err
                current = apply_redirect(current, location)
                validate_url_target(current, policy.allow_private_network)
                continue
            raise ValueError(f"HTTP error {err.code}") from err
        except URLError as err:
            raise ValueError("Failed to fetch URL") from err
    raise ValueError("Too many redirects")


def load_html_source(
    source: Union[str, bytes, pathlib.Path],
    *,
    base_dir: Optional[str],
    policy: FetchPolicy,
) -> Tuple[str, str]:
    """Loads HTML from a local path, URL, or raw HTML string."""

    if isinstance(source, bytes):
        if len(source) > policy.max_bytes:
            raise ValueError("HTML exceeds maximum allowed size")
        html = _decode_html(source)
        _enforce_max_nodes(html, _MAX_NODES)
        return html, "document"

    if isinstance(source, pathlib.Path):
        source = str(source)

    if not isinstance(source, str):
        raise TypeError("source must be str, bytes, or Path")

    if is_url(source):
        html, final_url = _fetch_url(source, policy)
        _enforce_max_nodes(html, _MAX_NODES)
        return html, final_url

    if os.path.exists(source):
        path = normalize_path(source, base_dir)
        data = _read_bytes_limited(path, policy.max_bytes)
        html = _decode_html(data)
        _enforce_max_nodes(html, _MAX_NODES)
        return html, source

    if _HTML_HINT.search(source):
        if len(source.encode("utf-8", errors="replace")) > policy.max_bytes:
            raise ValueError("HTML exceeds maximum allowed size")
        _enforce_max_nodes(source, _MAX_NODES)
        return source, "document"

    raise ValueError("Source is ambiguous; pass bytes for raw HTML")
