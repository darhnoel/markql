#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
EXERCISES_DIR="$(cd "$SCRIPT_DIR/.." && pwd)"
REPO_ROOT="$(cd "$EXERCISES_DIR/../.." && pwd)"

ROOT="$EXERCISES_DIR"
MARKQL_BIN="$REPO_ROOT/build/markql"
UPDATE=0

usage() {
  cat <<USAGE
Usage: docs/exercises/scripts/verify_exercises.sh [--root <dir>] [--markql <bin>] [--update]

Options:
  --root <dir>     Exercise root directory (default: docs/exercises)
  --markql <bin>   MarkQL binary path (default: ./build/markql)
  --update         Overwrite expected CSV files with current output
USAGE
}

while [[ $# -gt 0 ]]; do
  case "$1" in
    --root)
      ROOT="$2"
      shift 2
      ;;
    --markql)
      MARKQL_BIN="$2"
      shift 2
      ;;
    --update)
      UPDATE=1
      shift
      ;;
    -h|--help)
      usage
      exit 0
      ;;
    *)
      echo "Unknown argument: $1" >&2
      usage >&2
      exit 2
      ;;
  esac
done

if [[ ! -x "$MARKQL_BIN" ]]; then
  echo "markql binary not executable: $MARKQL_BIN" >&2
  exit 2
fi
if [[ ! -d "$ROOT" ]]; then
  echo "exercise root not found: $ROOT" >&2
  exit 2
fi

ROOT_ABS="$(realpath -m "$ROOT")"

if [[ "$ROOT_ABS" == "$EXERCISES_DIR" ]]; then
  mapfile -t SQL_FILES < <(find "$ROOT_ABS" \
    \( -path "$EXERCISES_DIR/scripts/tests" -o -path "$EXERCISES_DIR/scripts/tests/*" \) -prune -o \
    -type f \( -name "*_solution.sql" -o -name "solution.sql" \) -print | sort)
else
  mapfile -t SQL_FILES < <(find "$ROOT_ABS" -type f \( -name "*_solution.sql" -o -name "solution.sql" \) | sort)
fi
if [[ ${#SQL_FILES[@]} -eq 0 ]]; then
  echo "no solution SQL files found under $ROOT" >&2
  exit 2
fi

pass=0
fail=0

tmp_dir="$(mktemp -d)"
trap 'rm -rf "$tmp_dir"' EXIT

for sql in "${SQL_FILES[@]}"; do
  sql_dir="$(dirname "$sql")"
  sql_base="$(basename "$sql")"

  fixture_rel="$(sed -n '1{s#^[[:space:]]*/\*[[:space:]]*FIXTURE:[[:space:]]*\([^*]*\)\*/[[:space:]]*$#\1#p}' "$sql" | sed -E 's/^[[:space:]]+|[[:space:]]+$//g')"
  if [[ -z "$fixture_rel" ]]; then
    echo "[FAIL] $sql"
    echo "       missing fixture directive on first line: /* FIXTURE: <path> */"
    fail=$((fail + 1))
    continue
  fi

  fixture_path="$(realpath -m "$sql_dir/$fixture_rel")"
  if [[ ! -f "$fixture_path" ]]; then
    echo "[FAIL] $sql"
    echo "       fixture not found: $fixture_path"
    fail=$((fail + 1))
    continue
  fi

  expected_csv=""
  if [[ "$sql_base" == "solution.sql" ]]; then
    expected_csv="$sql_dir/expected.csv"
  else
    expected_csv="${sql%_solution.sql}_expected.csv"
  fi

  query_body="$(cat "$sql")"
  query_body="$(printf "%s" "$query_body" | sed '1{/^[[:space:]]*\/\*[[:space:]]*FIXTURE:/d;}')"

  if printf "%s" "$query_body" | grep -Eiq '(^|[^A-Za-z0-9_])TO[[:space:]]+(CSV|JSON|NDJSON|PARQUET|LIST|TABLE)[[:space:]]*\('; then
    echo "[FAIL] $sql"
    echo "       solution query must not include TO <sink>(...) (harness appends TO CSV)"
    fail=$((fail + 1))
    continue
  fi

  query_body="$(printf "%s" "$query_body" | sed -E ':a;N;$!ba;s/[[:space:]]+$//')"
  query_body="$(printf "%s" "$query_body" | sed -E ':a;N;$!ba;s/;[[:space:]]*$//')"

  out_csv="$tmp_dir/$(echo "$sql" | tr '/:' '__').csv"
  out_sql="$tmp_dir/$(echo "$sql" | tr '/:' '__').sql"

  cat > "$out_sql" <<Q
$query_body
TO CSV('$out_csv');
Q

  run_log="$tmp_dir/$(echo "$sql" | tr '/:' '__').log"
  if ! "$MARKQL_BIN" --input "$fixture_path" --query-file "$out_sql" --quiet >"$run_log" 2>&1; then
    echo "[FAIL] $sql"
    sed 's/^/       /' "$run_log"
    fail=$((fail + 1))
    continue
  fi

  if [[ ! -f "$out_csv" ]]; then
    echo "[FAIL] $sql"
    echo "       expected output CSV was not produced: $out_csv"
    fail=$((fail + 1))
    continue
  fi

  if [[ $UPDATE -eq 1 ]]; then
    cp "$out_csv" "$expected_csv"
    echo "[UPDATE] $expected_csv"
    pass=$((pass + 1))
    continue
  fi

  if [[ ! -f "$expected_csv" ]]; then
    echo "[FAIL] $sql"
    echo "       expected CSV missing: $expected_csv"
    fail=$((fail + 1))
    continue
  fi

  if diff -u "$expected_csv" "$out_csv" > "$tmp_dir/diff.txt"; then
    echo "[PASS] $sql"
    pass=$((pass + 1))
  else
    echo "[FAIL] $sql"
    sed 's/^/       /' "$tmp_dir/diff.txt"
    fail=$((fail + 1))
  fi
done

echo
echo "Verification summary: pass=$pass fail=$fail total=${#SQL_FILES[@]}"

if [[ $fail -gt 0 ]]; then
  exit 1
fi
