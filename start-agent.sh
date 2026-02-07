#!/bin/bash

set -euo pipefail

: "${XSQL_AGENT_TOKEN:=S3CR3T102938}"
export XSQL_AGENT_TOKEN

if [[ -x "./build/xsql-agent" ]]; then
  exec ./build/xsql-agent
fi

if [[ -x "./build/browser_plugin/agent/xsql-agent" ]]; then
  exec ./build/browser_plugin/agent/xsql-agent
fi

echo "xsql-agent binary not found. Build first with: ./build.sh" >&2
exit 1
