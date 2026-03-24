#!/usr/bin/env bash

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "${SCRIPT_DIR}/../.." && pwd)"

usage() {
  cat <<'EOF'
Usage:
  ./scripts/format/format_cpp.sh --all
  ./scripts/format/format_cpp.sh --diff-base <git-rev>
  ./scripts/format/format_cpp.sh <path> [<path> ...]

Formats C/C++ source files with clang-format using the repository .clang-format.

Notes:
  - Prefer explicit paths or --diff-base for low-risk, touched-file formatting.
  - --all is available for repository-wide normalization, but is intentionally not
    the default to avoid accidental whole-tree churn.
EOF
}

if [[ $# -eq 0 ]]; then
  usage
  exit 2
fi

cd "${REPO_ROOT}"

cpp_pathspecs=(
  '*.c'
  '*.cc'
  '*.cpp'
  '*.cxx'
  '*.h'
  '*.hh'
  '*.hpp'
  '*.ipp'
  '*.tpp'
)

is_cpp_path() {
  case "$1" in
    *.c|*.cc|*.cpp|*.cxx|*.h|*.hh|*.hpp|*.ipp|*.tpp) return 0 ;;
    *) return 1 ;;
  esac
}

collect_all_cpp_files() {
  git ls-files -- "${cpp_pathspecs[@]}"
}

collect_diff_cpp_files() {
  local base_rev="$1"
  git diff --name-only --diff-filter=ACMRTUXB "${base_rev}...HEAD" -- "${cpp_pathspecs[@]}"
}

mode="paths"
base_rev=""
declare -a candidates=()

case "${1}" in
  --help|-h)
    usage
    exit 0
    ;;
  --all)
    mode="all"
    shift
    if [[ $# -ne 0 ]]; then
      echo "error: --all does not accept additional paths." >&2
      usage
      exit 2
    fi
    ;;
  --diff-base)
    if [[ $# -lt 2 ]]; then
      echo "error: --diff-base requires a git revision." >&2
      usage
      exit 2
    fi
    mode="diff"
    base_rev="$2"
    shift 2
    if [[ $# -ne 0 ]]; then
      echo "error: --diff-base does not accept additional paths." >&2
      usage
      exit 2
    fi
    ;;
esac

if [[ "${mode}" == "paths" ]]; then
  candidates=("$@")
elif [[ "${mode}" == "all" ]]; then
  mapfile -t candidates < <(collect_all_cpp_files)
else
  mapfile -t candidates < <(collect_diff_cpp_files "${base_rev}")
fi

if ! command -v clang-format >/dev/null 2>&1; then
  echo "error: clang-format is required but was not found in PATH." >&2
  echo "Install clang-format locally, then rerun this script." >&2
  exit 2
fi

declare -a files=()
for path in "${candidates[@]}"; do
  [[ -n "${path}" ]] || continue
  if ! is_cpp_path "${path}"; then
    echo "warning: skipping non-C++ path: ${path}" >&2
    continue
  fi
  if [[ ! -f "${path}" ]]; then
    echo "warning: skipping missing path: ${path}" >&2
    continue
  fi
  files+=("${path}")
done

if [[ ${#files[@]} -eq 0 ]]; then
  echo "No C++ files selected for formatting."
  exit 0
fi

clang-format -i --style=file "${files[@]}"

echo "Formatted ${#files[@]} C++ file(s)."
