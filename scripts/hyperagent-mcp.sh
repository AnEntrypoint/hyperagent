#!/bin/sh
set -eu
root=$(cd "$(dirname "$0")/.." && pwd)
bin=
for candidate in "$root"/build/release/*/bin/hyperagent "$root"/build/hyperagent; do
  if [ -x "$candidate" ]; then
    bin=$candidate
    break
  fi
done
if [ -z "$bin" ]; then
  echo "no hyperagent binary under build/: run scripts/fetch-release.sh" >&2
  exit 1
fi
export ORCH_PARENT="${ORCH_PARENT:-$(dirname "$root")}"
exec "$bin" mcp "$@"
