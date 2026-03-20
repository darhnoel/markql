#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "${SCRIPT_DIR}/../.." && pwd)"
MANIFEST_PATH="${REPO_ROOT}/tools/html_inspector/Cargo.toml"

if [[ ! -f "${MANIFEST_PATH}" ]]; then
  echo "error: Rust inspector manifest not found at ${MANIFEST_PATH}" >&2
  exit 1
fi

if [[ $# -eq 0 ]]; then
  cat <<'USAGE'
Usage:
  ./scripts/tools/run_rust_inspector.sh <html-file-or-url>

Examples:
  ./scripts/tools/run_rust_inspector.sh docs/fixtures/basic.html
  ./scripts/tools/run_rust_inspector.sh https://example.com
USAGE
  exit 1
fi

exec cargo run --manifest-path "${MANIFEST_PATH}" -- "$@"
