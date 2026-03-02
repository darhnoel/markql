#!/usr/bin/env bash
set -euo pipefail

usage() {
  cat <<'EOF'
Usage: ./install_markql.sh [PATH_TO_DEB]

Installs a local MarkQL .deb package using apt.

If PATH_TO_DEB is omitted, the script auto-detects the newest file matching:
  ./markql_*.deb
  ./dist/deb/markql_*.deb
  ./_CPack_Packages/Linux/DEB/markql_*.deb
EOF
}

if [[ "${1:-}" == "-h" || "${1:-}" == "--help" ]]; then
  usage
  exit 0
fi

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

resolve_deb_path() {
  local input="${1:-}"
  if [[ -n "${input}" ]]; then
    if [[ ! -f "${input}" ]]; then
      echo "Package not found: ${input}" >&2
      exit 1
    fi
    realpath "${input}"
    return 0
  fi

  local candidates
  candidates="$(find "${SCRIPT_DIR}" \
    -maxdepth 4 \
    -type f \
    \( -path "${SCRIPT_DIR}/markql_*.deb" \
       -o -path "${SCRIPT_DIR}/dist/deb/markql_*.deb" \
       -o -path "${SCRIPT_DIR}/_CPack_Packages/Linux/DEB/markql_*.deb" \) \
    -printf '%T@ %p\n' | sort -nr || true)"

  if [[ -z "${candidates}" ]]; then
    echo "No local markql .deb found." >&2
    echo "Build one first (for example: ./scripts/package_deb.sh)." >&2
    exit 1
  fi

  echo "${candidates}" | head -n1 | cut -d' ' -f2-
}

DEB_PATH="$(resolve_deb_path "${1:-}")"

echo "Installing: ${DEB_PATH}"
sudo apt install -y "${DEB_PATH}"
