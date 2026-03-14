from __future__ import annotations

import ipaddress
import os
import socket
from dataclasses import dataclass
from typing import Iterable, Optional, Tuple
from urllib.parse import urlparse, urljoin


@dataclass(frozen=True)
class FetchPolicy:
    """Defines network and resource limits for fetching HTML content."""

    allow_network: bool
    allow_private_network: bool
    timeout: int
    max_bytes: int
    max_redirects: int = 3


_PRIVATE_NETS = [
    ipaddress.ip_network("127.0.0.0/8"),
    ipaddress.ip_network("10.0.0.0/8"),
    ipaddress.ip_network("172.16.0.0/12"),
    ipaddress.ip_network("192.168.0.0/16"),
    ipaddress.ip_network("169.254.0.0/16"),
    ipaddress.ip_network("::1/128"),
    ipaddress.ip_network("fc00::/7"),
    ipaddress.ip_network("fe80::/10"),
]


def is_url(source: str) -> bool:
    """Detects http/https URLs to avoid ambiguous HTML interpretation."""

    parsed = urlparse(source)
    return parsed.scheme in {"http", "https"} and bool(parsed.netloc)


def resolve_host(host: str) -> Iterable[ipaddress._BaseAddress]:
    """Resolves hostnames to IP addresses for SSRF filtering."""

    infos = socket.getaddrinfo(host, None)
    for family, _, _, _, sockaddr in infos:
        if family == socket.AF_INET:
            yield ipaddress.ip_address(sockaddr[0])
        elif family == socket.AF_INET6:
            yield ipaddress.ip_address(sockaddr[0])


def validate_url_target(url: str, allow_private: bool) -> None:
    """Blocks localhost/private targets unless explicitly allowed."""

    parsed = urlparse(url)
    if parsed.scheme not in {"http", "https"}:
        raise ValueError("Only http/https URLs are allowed")
    if not parsed.hostname:
        raise ValueError("URL must include a hostname")
    for addr in resolve_host(parsed.hostname):
        if any(addr in net for net in _PRIVATE_NETS) and not allow_private:
            raise ValueError("URL points to a private or localhost address")


def normalize_path(path: str, base_dir: Optional[str]) -> str:
    """Normalizes and confines file paths when base_dir is provided."""

    real = os.path.realpath(path)
    if base_dir:
        base = os.path.realpath(base_dir)
        if os.path.commonpath([real, base]) != base:
            raise ValueError("Path escapes the configured base_dir")
    return real


def apply_redirect(url: str, location: str) -> str:
    """Resolves redirect targets while preserving relative locations."""

    return urljoin(url, location)
