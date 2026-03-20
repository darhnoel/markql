#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "${SCRIPT_DIR}/../.." && pwd)"

resolve_markql_bin() {
  if [[ -x "${REPO_ROOT}/build/markql" ]]; then
    echo "${REPO_ROOT}/build/markql"
    return 0
  fi

  if [[ -x "${SCRIPT_DIR}/markql" ]]; then
    echo "${SCRIPT_DIR}/markql"
    return 0
  fi

  if command -v markql >/dev/null 2>&1; then
    command -v markql
    return 0
  fi

  echo "markql binary not found. Build first with: ./scripts/build/build.sh" >&2
  exit 1
}

MARKQL_BIN="$(resolve_markql_bin)"

if [[ "${1:-}" == "explore" ]]; then
  exec "${MARKQL_BIN}" "$@"
fi

for arg in "$@"; do
  case "$arg" in
    --query|--query-file|--interactive|-q)
      exec "${MARKQL_BIN}" "$@"
      ;;
  esac
done

exec "${MARKQL_BIN}" --interactive "$@"
