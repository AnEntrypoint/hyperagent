#!/usr/bin/env bash
set -euo pipefail

root=$(cd "$(dirname "$0")/.." && pwd)
cd "$root"
created=0
if [ -z "${HYPERAGENT_WITNESS_DIR:-}" ]; then
    HYPERAGENT_WITNESS_DIR=$(mktemp -d)
    created=1
    export HYPERAGENT_WITNESS_DIR
fi
mkdir -p "$HYPERAGENT_WITNESS_DIR"

finish() {
    code=$?
    if [ "$created" = "1" ]; then
        rm -rf "$HYPERAGENT_WITNESS_DIR"
    fi
    echo "PORTABLE_DONE $code"
    exit "$code"
}
trap finish EXIT

cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DHYPERAGENT_VERSION=portable-witness
cmake --build build --parallel 2
ctest --test-dir build --output-on-failure
node tools/validate-json.cjs
node tools/portable-paths.witness.cjs
node tools/release-publication.witness.cjs
for file in scripts/*.cjs tools/*.cjs; do node --check "$file"; done
python3 -I tools/check-python.py .
bash -n tools/*.sh tests/*.sh
node tools/portable-setup-witness.cjs build
