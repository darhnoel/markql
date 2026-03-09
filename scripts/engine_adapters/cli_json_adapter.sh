#!/usr/bin/env bash
set -euo pipefail

if [[ $# -ne 1 ]]; then
  echo "usage: $0 '<query>'" >&2
  exit 2
fi

script_dir=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)
repo_root=$(cd "$script_dir/../.." && pwd)
engine_bin="$repo_root/build/markql"
query=$1

if [[ ! -x "$engine_bin" ]]; then
  echo "adapter error: missing executable $engine_bin" >&2
  exit 2
fi

set +e
engine_output=$("$engine_bin" --lint "$query" --format json 2>&1)
engine_exit=$?
set -e

case "$engine_exit" in
  0)
    verdict="accept"
    ;;
  1)
    verdict="reject"
    ;;
  2)
    echo "adapter error: engine returned tooling failure for query: $query" >&2
    echo "$engine_output" >&2
    exit 2
    ;;
  *)
    echo "adapter error: unexpected engine exit code $engine_exit for query: $query" >&2
    echo "$engine_output" >&2
    exit 2
    ;;
esac

jq -n \
  --arg query "$query" \
  --arg verdict "$verdict" \
  --argjson exit_code "$engine_exit" \
  --arg engine_output "$engine_output" \
  '{
    query: $query,
    verdict: $verdict,
    exit_code: $exit_code,
    engine_output: $engine_output
  }'
