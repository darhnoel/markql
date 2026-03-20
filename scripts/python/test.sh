#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "${SCRIPT_DIR}/../.." && pwd)"

VENV_DIR="${VENV_DIR:-markql_venv}"
VENV_PATH="${REPO_ROOT}/${VENV_DIR}"

if [[ ! -d "${VENV_PATH}" ]]; then
  echo "Virtual environment not found: ${VENV_PATH}" >&2
  echo "Run ./scripts/python/install.sh first." >&2
  exit 1
fi

source "${VENV_PATH}/bin/activate"
(
  cd "${REPO_ROOT}"
  PYTHONPATH="${PYTHONPATH:+${PYTHONPATH}:}python" pytest -v python/tests
)
