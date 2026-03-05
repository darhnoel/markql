#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
MANIFEST="$ROOT_DIR/tests/integration/rust/Cargo.toml"

exec cargo run --manifest-path "$MANIFEST" --quiet -- "$@"
