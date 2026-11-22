#!/usr/bin/env bash
set -euo pipefail

build=${1:?usage: ship-witness.sh BUILD_DIR}
scratch=/workspace/tmp/hyperagent-release
mkdir -p "$scratch"
work=$(mktemp -d "$scratch/witness.XXXXXX")
trap 'rm -rf "$work"' EXIT

fail() { echo "WITNESS_FAIL $*" >&2; exit 1; }

cmake --install "$build" --prefix "$work/prefix" > "$work/install.log" 2>&1 || fail "cmake --install failed"
mkdir -p "$work/away" "$work/home"
cp -a "$work/prefix" "$work/away/ship"
rm -rf "$work/prefix"
ship=$work/away/ship
home=$work/home

for file in bin/hyperagent bin/ccbridge share/ccbridge/templates/claude.json share/ccbridge/web/index.html share/hyperagent/gui/public/index.html; do
    [ -e "$ship/$file" ] || fail "missing $file"
done

run() { (cd "$work/away" && env -i HOME="$home" PATH=/usr/bin:/bin "$ship/bin/hyperagent" "$@"); }

env -i PATH=/usr/bin:/bin sh -c 'command -v claude' > /dev/null 2>&1 && fail "claude is on the witness PATH"

version=$(run --version) || fail "--version failed"
[ -n "$version" ] || fail "--version printed nothing"
echo "version $version"

[ "$(run ccbridge)" = "$ship/bin/ccbridge" ] || fail "ccbridge not found beside the binary: $(run ccbridge)"

setup=$(run setup) || fail "setup exited nonzero"
echo "$setup"
case "$setup" in *"claude CLI not found"*) ;; *) fail "setup did not report the missing claude CLI" ;; esac
[ -d "$home/.local/share/hyperagent/.orch" ] || fail "state directory not under HOME"
run setup > /dev/null || fail "second setup failed"

echo "WITNESS_OK"
