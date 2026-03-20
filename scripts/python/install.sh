#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "${SCRIPT_DIR}/../.." && pwd)"

VENV_DIR="${VENV_DIR:-markql_venv}"
VENV_PATH="${REPO_ROOT}/${VENV_DIR}"

if [[ ! -d "${VENV_PATH}" ]]; then
  python3 -m venv "${VENV_PATH}"
fi

source "${VENV_PATH}/bin/activate"
python -m pip install -U pip
(
  cd "${REPO_ROOT}"
  python -m pip install -e .[test]
)
