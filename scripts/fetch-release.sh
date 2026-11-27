#!/bin/sh
set -eu
repo=AnEntrypoint/hyperagent
tag=${1:-latest}
prefix=${HYPERAGENT_PREFIX:-build/release}
case "$(uname -s)-$(uname -m)" in
  Linux-x86_64) target=x64-linux ;;
  Linux-aarch64) target=arm64-linux ;;
  Darwin-arm64) target=arm64-macos ;;
  Darwin-x86_64) target=x64-macos ;;
  *) echo "no CI build for $(uname -s)-$(uname -m); build from source with the vcpkg manifest" >&2; exit 1 ;;
esac
tmp=$(mktemp -d)
trap 'rm -rf "$tmp"' EXIT
gh release download "$tag" -R "$repo" -D "$tmp" -p SHA256SUMS -p "hyperagent-$target.tar.gz"
want=$(grep " hyperagent-[^ ]*$target\.tar\.gz\$" "$tmp/SHA256SUMS" | head -1 | cut -d' ' -f1)
got=$(sha256sum "$tmp/hyperagent-$target.tar.gz" | cut -d' ' -f1)
[ -n "$want" ] && [ "$want" = "$got" ] || { echo "checksum mismatch for $target" >&2; exit 1; }
mkdir -p "$prefix"
tar -xzf "$tmp/hyperagent-$target.tar.gz" -C "$prefix"
bin=$(find "$prefix" -path '*/bin/hyperagent' | head -1)
echo "installed $bin"
"$bin" --version | head -1
