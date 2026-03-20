#!/usr/bin/env bash

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "${SCRIPT_DIR}/../.." && pwd)"

: "${XSQL_AGENT_TOKEN:=S3CR3T102938}"
export XSQL_AGENT_TOKEN

if [[ -x "${REPO_ROOT}/build/xsql-agent" ]]; then
  exec "${REPO_ROOT}/build/xsql-agent"
fi

if [[ -x "${REPO_ROOT}/build/browser_plugin/agent/xsql-agent" ]]; then
  exec "${REPO_ROOT}/build/browser_plugin/agent/xsql-agent"
fi

echo "xsql-agent binary not found. Build first with: ./scripts/build/build.sh" >&2
exit 1
