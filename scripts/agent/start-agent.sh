#!/usr/bin/env bash

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "${SCRIPT_DIR}/../.." && pwd)"

if [[ -n "${XSQL_AGENT_TOKEN:-}" && -z "${MARKQL_AGENT_TOKEN:-}" ]]; then
  MARKQL_AGENT_TOKEN="${XSQL_AGENT_TOKEN}"
fi

: "${MARKQL_AGENT_TOKEN:=S3CR3T102938}"
export MARKQL_AGENT_TOKEN
export XSQL_AGENT_TOKEN="${XSQL_AGENT_TOKEN:-${MARKQL_AGENT_TOKEN}}"

if [[ -x "${REPO_ROOT}/build/markql-agent" ]]; then
  exec "${REPO_ROOT}/build/markql-agent"
fi

if [[ -x "${REPO_ROOT}/build/browser_plugin/agent/markql-agent" ]]; then
  exec "${REPO_ROOT}/build/browser_plugin/agent/markql-agent"
fi

if [[ -x "${REPO_ROOT}/build/xsql-agent" ]]; then
  exec "${REPO_ROOT}/build/xsql-agent"
fi

if [[ -x "${REPO_ROOT}/build/browser_plugin/agent/xsql-agent" ]]; then
  exec "${REPO_ROOT}/build/browser_plugin/agent/xsql-agent"
fi

echo "markql-agent binary not found. Build first with: ./scripts/build/build.sh" >&2
exit 1
