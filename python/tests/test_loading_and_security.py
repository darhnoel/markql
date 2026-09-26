import pathlib

import pytest

import markql


def test_load_local_file_with_base_dir(tmp_path: pathlib.Path) -> None:
    html_path = tmp_path / "sample.html"
    html_path.write_text("<html><body><a id='x'></a></body></html>", encoding="utf-8")
    doc = markql.load(str(html_path), base_dir=str(tmp_path))
    assert doc.source == str(html_path)
    assert "<a" in doc.html


def test_base_dir_traversal_blocked(tmp_path: pathlib.Path) -> None:
    outside = tmp_path.parent / "outside.html"
    outside.write_text("<html></html>", encoding="utf-8")
    with pytest.raises(ValueError):
        markql.load(str(outside), base_dir=str(tmp_path))


def test_network_disabled_by_default() -> None:
    with pytest.raises(ValueError):
        markql.load("https://example.com")


def test_ssrf_blocks_localhost() -> None:
    with pytest.raises(ValueError):
        markql.load("http://127.0.0.1/", allow_network=True)


def test_max_bytes_enforced(tmp_path: pathlib.Path) -> None:
    html_path = tmp_path / "big.html"
    html_path.write_bytes(b"<html>" + b"a" * 64 + b"</html>")
    with pytest.raises(ValueError):
        markql.load(str(html_path), base_dir=str(tmp_path), max_bytes=16)


def test_large_html_fails_fast() -> None:
    html = "<div></div>" * 100_001
    with pytest.raises(ValueError):
        markql.load(html)


# --- Content-Encoding -------------------------------------------------------
# Servers may compress a response even when it was not asked for: Yahoo
# Finance sends gzip to a request with no Accept-Encoding, and the loader
# decoded the compressed bytes as UTF-8 text, handing the engine noise.

import gzip
import http.server
import threading
import zlib


def _serve(body: bytes, encoding: str):
    class Handler(http.server.BaseHTTPRequestHandler):
        def do_GET(self):
            self.send_response(200)
            self.send_header("Content-Type", "text/html; charset=utf-8")
            self.send_header("Content-Encoding", encoding)
            self.send_header("Content-Length", str(len(body)))
            self.end_headers()
            self.wfile.write(body)

        def log_message(self, *args):
            pass

    server = http.server.HTTPServer(("127.0.0.1", 0), Handler)
    threading.Thread(target=server.serve_forever, daemon=True).start()
    return server, f"http://127.0.0.1:{server.server_port}/"


PAGE = "<ul><li class='row'>Fish &amp; Chips £4.50</li></ul>"


@pytest.mark.parametrize("encoding,compress", [
    ("gzip", gzip.compress),
    ("x-gzip", gzip.compress),
    ("deflate", zlib.compress),
    ("deflate", lambda d: zlib.compress(d)[2:-4]),  # raw deflate, as some servers send
])
def test_compressed_response_is_decoded(encoding, compress) -> None:
    server, url = _serve(compress(PAGE.encode("utf-8")), encoding)
    try:
        doc = markql.load(url, allow_network=True, allow_private_network=True)
    finally:
        server.shutdown()
    assert doc.html == PAGE


def test_compression_cannot_bypass_max_bytes() -> None:
    """A few kilobytes of gzip can expand to gigabytes; the limit is on HTML."""
    bomb = gzip.compress(b"<p>" + b"a" * 5_000_000 + b"</p>")
    assert len(bomb) < 50_000
    server, url = _serve(bomb, "gzip")
    try:
        with pytest.raises(ValueError, match="max_bytes"):
            markql.load(url, allow_network=True, allow_private_network=True,
                        max_bytes=1_000_000)
    finally:
        server.shutdown()


def test_unsupported_encoding_is_an_error_not_garbage() -> None:
    server, url = _serve(b"\x8b\x00\x80compressed", "br")
    try:
        with pytest.raises(ValueError, match="Content-Encoding"):
            markql.load(url, allow_network=True, allow_private_network=True)
    finally:
        server.shutdown()
