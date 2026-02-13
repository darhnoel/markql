#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
EXERCISES_DIR="$(cd "$SCRIPT_DIR/.." && pwd)"
REPO_ROOT="$(cd "$EXERCISES_DIR/../.." && pwd)"
VERIFY_SCRIPT="$SCRIPT_DIR/verify_exercises.sh"

MARKQL_BIN="${1:-$REPO_ROOT/build/markql}"

"$VERIFY_SCRIPT" \
  --markql "$MARKQL_BIN" \
  --root "$SCRIPT_DIR/tests/verify_exercises/pass"

echo "[CHECK] pass fixture root succeeded"

if "$VERIFY_SCRIPT" \
  --markql "$MARKQL_BIN" \
  --root "$SCRIPT_DIR/tests/verify_exercises/fail"; then
  echo "Expected failure root to fail, but it passed" >&2
  exit 1
fi

echo "[CHECK] failing fixture root correctly detected mismatch"
