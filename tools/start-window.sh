#!/usr/bin/env bash
set -euo pipefail

if [ -z "${HYPERAGENT_WITNESS_DIR:-}" ]; then
    echo "HYPERAGENT_WITNESS_DIR is required" >&2
    exit 1
fi
mkdir -p "$HYPERAGENT_WITNESS_DIR"
result="$HYPERAGENT_WITNESS_DIR/window.log"
setsid nohup node scripts/gate-ticket.cjs run hyperagent-portable-core -- bash tools/window.sh > "$result" 2>&1 < /dev/null &
echo $! > "$HYPERAGENT_WITNESS_DIR/window.pid"
echo "$result"
