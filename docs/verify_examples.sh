#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BIN="$ROOT_DIR/build/markql"
BOOK_GLOB="$ROOT_DIR/docs/book/ch*.md"

if [[ ! -x "$BIN" ]]; then
  cmake -S "$ROOT_DIR" -B "$ROOT_DIR/build"
  cmake --build "$ROOT_DIR/build"
fi

declare -A seen_ids=()
total=0

run_block() {
  local id="$1"
  local cmd="$2"
  local expect_fail="$3"
  local output

  ((total += 1))

  if [[ -n "${seen_ids[$id]:-}" ]]; then
    echo "Duplicate VERIFY id: $id" >&2
    exit 1
  fi
  seen_ids[$id]=1

  if [[ -n "$expect_fail" ]]; then
    set +e
    output="$(bash -lc "cd '$ROOT_DIR' && $cmd" 2>&1)"
    status=$?
    set -e
    if [[ $status -eq 0 ]]; then
      echo "VERIFY $id expected failure but command succeeded" >&2
      echo "$output" >&2
      exit 1
    fi
    if ! grep -Fq "$expect_fail" <<<"$output"; then
      echo "VERIFY $id failed: expected error substring not found: $expect_fail" >&2
      echo "Actual output:" >&2
      echo "$output" >&2
      exit 1
    fi
  else
    if ! output="$(bash -lc "cd '$ROOT_DIR' && set -euo pipefail; $cmd" 2>&1)"; then
      echo "VERIFY $id failed: command returned non-zero" >&2
      echo "$output" >&2
      exit 1
    fi
  fi
}

for file in $BOOK_GLOB; do
  current_id=""
  pending_marker=0
  in_block=0
  cmd=""
  expect_fail=""

  while IFS= read -r line || [[ -n "$line" ]]; do
    if [[ $in_block -eq 0 ]]; then
      if [[ $line =~ ^\<\!--[[:space:]]VERIFY:[[:space:]]([^[:space:]]+)[[:space:]]--\>$ ]]; then
        current_id="${BASH_REMATCH[1]}"
        pending_marker=1
        continue
      fi

      if [[ $pending_marker -eq 1 ]]; then
        if [[ -z "$line" ]]; then
          continue
        fi
        if [[ "$line" == '```bash' ]]; then
          in_block=1
          cmd=""
          expect_fail=""
          pending_marker=0
          continue
        fi
        echo "VERIFY marker $current_id in $file must be followed by a bash code block" >&2
        exit 1
      fi
    else
      if [[ "$line" == '```' ]]; then
        if [[ -z "$cmd" ]]; then
          echo "VERIFY $current_id has an empty command block in $file" >&2
          exit 1
        fi
        run_block "$current_id" "$cmd" "$expect_fail"
        in_block=0
        current_id=""
        continue
      fi

      if [[ $line =~ ^#[[:space:]]EXPECT_FAIL:[[:space:]](.+)$ ]]; then
        expect_fail="${BASH_REMATCH[1]}"
        continue
      fi

      cmd+="$line"$'\n'
    fi
  done < "$file"

  if [[ $in_block -eq 1 ]]; then
    echo "Unclosed bash block in $file for VERIFY $current_id" >&2
    exit 1
  fi
  if [[ $pending_marker -eq 1 ]]; then
    echo "Dangling VERIFY marker in $file: $current_id" >&2
    exit 1
  fi
done

if [[ $total -eq 0 ]]; then
  echo "No VERIFY markers found in docs/book/ch*.md" >&2
  exit 1
fi

echo "Verified $total documented command blocks successfully."
