#!/usr/bin/env bash
set -euo pipefail

script_dir=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)
repo_root=$(cd "$script_dir/.." && pwd)
cases_file="${1:-$repo_root/tests/formal_conformance/core_select_doc_alias_cases.json}"
adapter="${FORMAL_ENGINE_ADAPTER:-$repo_root/scripts/engine_adapters/cli_json_adapter.sh}"

if [[ ! -f "$cases_file" ]]; then
  echo "missing case corpus: $cases_file" >&2
  exit 2
fi

if [[ ! -x "$adapter" ]]; then
  echo "missing or non-executable adapter: $adapter" >&2
  exit 2
fi

total=0
passed=0

while IFS= read -r case_json; do
  total=$((total + 1))
  query=$(jq -r '.query' <<<"$case_json")
  expected_verdict=$(jq -r '.expected_verdict' <<<"$case_json")

  if [[ "$expected_verdict" == "accept" ]]; then
    expected_exit=0
  elif [[ "$expected_verdict" == "reject" ]]; then
    expected_exit=1
  else
    echo "invalid expected_verdict in case corpus: $expected_verdict" >&2
    exit 2
  fi

  set +e
  observed_json=$("$adapter" "$query")
  adapter_exit=$?
  set -e

  if [[ "$adapter_exit" -eq 2 ]]; then
    echo "conformance runner stopped on adapter/tooling failure for query: $query" >&2
    exit 2
  fi

  if [[ "$adapter_exit" -ne 0 ]]; then
    echo "adapter returned unexpected exit code $adapter_exit for query: $query" >&2
    exit 2
  fi

  observed_verdict=$(jq -r '.verdict' <<<"$observed_json")
  observed_exit=$(jq -r '.exit_code' <<<"$observed_json")

  if [[ "$observed_verdict" != "$expected_verdict" ]]; then
    echo "verdict mismatch for query: $query" >&2
    echo "  expected: $expected_verdict" >&2
    echo "  observed: $observed_verdict" >&2
    exit 1
  fi

  if [[ "$observed_exit" -ne "$expected_exit" ]]; then
    echo "exit-code mismatch for query: $query" >&2
    echo "  expected: $expected_exit" >&2
    echo "  observed: $observed_exit" >&2
    exit 1
  fi

  passed=$((passed + 1))
done < <(jq -c '.[]' "$cases_file")

echo "formal conformance passed: $passed/$total cases"
