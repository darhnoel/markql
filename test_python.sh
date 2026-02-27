#!/usr/bin/env bash
set -euo pipefail

VENV_DIR="${VENV_DIR:-xsql_venv}"

if [[ ! -d "${VENV_DIR}" ]]; then
  echo "Virtual environment not found: ${VENV_DIR}" >&2
  echo "Run ./install_python.sh first." >&2
  exit 1
fi

source "${VENV_DIR}/bin/activate"
PYTHONPATH="${PYTHONPATH:+${PYTHONPATH}:}python" pytest -v python/tests
