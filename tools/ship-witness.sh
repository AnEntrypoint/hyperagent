#!/usr/bin/env bash
set -euo pipefail

build=${1:?usage: ship-witness.sh BUILD_DIR}
exec node "$(dirname "$0")/portable-setup-witness.cjs" "$build"
