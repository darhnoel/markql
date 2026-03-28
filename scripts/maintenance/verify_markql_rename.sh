#!/usr/bin/env bash

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "${SCRIPT_DIR}/../.." && pwd)"
cd "${REPO_ROOT}"

forbidden_pattern='XSQL|(?<!py)xsql|xsql(?!\b)'
allowed_legacy_paths=(
  "CMakeLists.txt"
  "scripts/maintenance/verify_markql_rename.sh"
  "scripts/build/build.sh"
  "scripts/agent/start-agent.sh"
  "browser_plugin/agent/src/main.cpp"
  "browser_plugin/extension/popup/config.js"
  "browser_plugin/extension/content.js"
  "desktop/src/agent.rs"
  "editors/vscode/src/cli.ts"
  "editors/vscode/out/cli.js"
)

tracked_files=()
while IFS= read -r -d '' path; do
  if [[ -f "${path}" ]]; then
    tracked_files+=("${path}")
  fi
done < <(git ls-files -z)

filtered_files=()
for path in "${tracked_files[@]}"; do
  skip_file=0
  for allowed in "${allowed_legacy_paths[@]}"; do
    if [[ "${path}" == "${allowed}" ]]; then
      skip_file=1
      break
    fi
  done
  if [[ "${skip_file}" -eq 0 ]]; then
    filtered_files+=("${path}")
  fi
done

if ((${#filtered_files[@]} > 0)) && rg -n -P "${forbidden_pattern}" "${filtered_files[@]}"; then
  echo "Forbidden legacy xsql reference found outside the explicit compatibility allowlist." >&2
  exit 1
fi

echo "MarkQL rename verification passed."
