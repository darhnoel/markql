#!/bin/bash

set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "${ROOT_DIR}"

forbidden_pattern='XSQL|(?<!py)xsql|xsql(?!\b)'

tracked_files=()
while IFS= read -r -d '' path; do
  if [[ -f "${path}" ]]; then
    tracked_files+=("${path}")
  fi
done < <(git ls-files -z)

if ((${#tracked_files[@]} > 0)) && rg -n -P "${forbidden_pattern}" "${tracked_files[@]}"; then
  echo "Forbidden legacy xsql reference found. Only exact 'pyxsql' is allowed." >&2
  exit 1
fi

echo "MarkQL rename verification passed."
