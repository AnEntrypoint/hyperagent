#!/usr/bin/env bash
set -euo pipefail
ROOT=$(cd "$(dirname "$0")/.." && pwd)
BUILD=${HYPERAGENT_BUILD:-$ROOT/build}
case "$BUILD" in /*) ;; *) BUILD=$PWD/$BUILD ;; esac
CCBRIDGE=$BUILD/ccbridge
PORT=${IDLE_REAP_TEST_PORT:-18948}
LIVE_PORTS="8787 8795"
REAP_SECONDS=3
TURN_SECONDS=16
BUSY_CHECK_SECONDS=12
CLIENT_SECONDS=3
SERVER_PID=
TMP=

fail() {
    echo "FAIL: $1"
    [ -s "${TMP:-/nonexistent}/server.log" ] && { echo "--- server.log"; tail -n 20 "$TMP/server.log"; }
    exit 1
}

for live in $LIVE_PORTS; do
    [ "$PORT" != "$live" ] || fail "port $PORT belongs to a live orchestrator"
done
[ -x "$CCBRIDGE" ] || fail "no ccbridge binary in $BUILD; set HYPERAGENT_BUILD"
[ -x "$BUILD/fake_claude/bin/claude" ] || fail "no fake claude in $BUILD"

port_listening() {
    (exec 9<>"/dev/tcp/127.0.0.1/$PORT") 2>/dev/null
}
port_listening && fail "port $PORT is already in use; set IDLE_REAP_TEST_PORT"

TMP=$(mktemp -d)
export HOME=$TMP/home
export FAKE_CLAUDE_TURN_MS=$((TURN_SECONDS * 1000))
export PATH=$BUILD/fake_claude/bin:$PATH
export CCBRIDGE_TEMPLATES=$ROOT/airc/templates
unset CCBRIDGE_IDLE_REAP CCBRIDGE_CWD
mkdir -p "$HOME" "$TMP/worker" "$TMP/sleeper"

stop_test_server() {
    if [ -n "$SERVER_PID" ] && kill -0 "$SERVER_PID" 2>/dev/null; then
        kill "$SERVER_PID" 2>/dev/null || true
        wait "$SERVER_PID" 2>/dev/null || true
    fi
    rm -rf "$TMP"
}
trap stop_test_server EXIT

"$CCBRIDGE" serve --host 127.0.0.1 --port "$PORT" --cwd "$TMP/scratch" --idle-reap "$REAP_SECONDS" \
    > "$TMP/server.log" 2>&1 &
SERVER_PID=$!
for _ in $(seq 1 100); do
    port_listening && break
    kill -0 "$SERVER_PID" 2>/dev/null || fail "the test server exited before listening"
    sleep 0.1
done
port_listening || fail "the test server never listened on port $PORT"

sessions() {
    exec 8<>"/dev/tcp/127.0.0.1/$PORT"
    printf 'GET /sessions HTTP/1.1\r\nHost: 127.0.0.1\r\nConnection: close\r\n\r\n' >&8
    cat <&8
    exec 8>&-
}
session_record() {
    sessions | tr -d '\r\n' | grep -oE "\"$1\": ?\{[^}]*\}" || true
}
listed() {
    [ -n "$(session_record "$1")" ]
}
busy() {
    session_record "$1" | grep -qE '"busy": ?true'
}
clients() {
    session_record "$1" | grep -oE '"clients": ?[0-9]+' | grep -oE '[0-9]+$'
}
wait_until() {
    local tries=$1
    shift
    for _ in $(seq 1 "$tries"); do
        "$@" && return 0
        sleep 0.2
    done
    return 1
}
client_free() {
    [ "$(clients "$1")" = 0 ]
}
unlisted() {
    ! listed "$1"
}
attach_then_leave() {
    exec 7<>"/dev/tcp/127.0.0.1/$PORT"
    printf 'GET /ws?session=%s&harness=claude&cwd=%s HTTP/1.1\r\nHost: 127.0.0.1\r\nUpgrade: websocket\r\nConnection: Upgrade\r\nSec-WebSocket-Key: dGhlIHNhbXBsZSBub25jZQ==\r\nSec-WebSocket-Version: 13\r\n\r\n' \
        "$1" "$2" >&7
    local status
    IFS= read -r -t 10 status <&7 || fail "no answer to the websocket upgrade for $1"
    case "$status" in *" 101 "*) ;; *) fail "websocket upgrade for $1 refused: $status" ;; esac
    wait_until 100 listed "$1" || fail "session $1 never appeared"
    exec 7>&-
}

timeout "$CLIENT_SECONDS" "$CCBRIDGE" client --url "ws://127.0.0.1:$PORT/ws" --harness claude --session worker \
    --cwd "$TMP/worker" "a long TOOL turn" > "$TMP/client.out" 2>&1 &
CLIENT_PID=$!
wait_until 100 busy worker || fail "worker never became busy: $(session_record worker) $(cat "$TMP/client.out")"
PROMPTED=$(date +%s)
wait "$CLIENT_PID" && fail "the client saw the turn end instead of leaving mid-turn"
attach_then_leave sleeper "$TMP/sleeper"

wait_until 50 client_free worker || fail "worker still has a client: $(session_record worker)"
wait_until 50 client_free sleeper || fail "sleeper still has a client: $(session_record sleeper)"
busy worker || fail "worker is not busy in its long turn: $(session_record worker)"
busy sleeper && fail "sleeper is busy without a prompt: $(session_record sleeper)"
echo "ok worker is busy and sleeper is idle, both without a client"

wait_until $((REAP_SECONDS * 5 + 25)) unlisted sleeper || fail "idle sleeper outlived the reap period: $(session_record sleeper)"
echo "ok idle session without a client was reaped after $(($(date +%s) - PROMPTED)) s (period $REAP_SECONDS s)"

while [ $(($(date +%s) - PROMPTED)) -lt "$BUSY_CHECK_SECONDS" ]; do
    listed worker || fail "busy worker was reaped $(($(date +%s) - PROMPTED)) s into its turn"
    sleep 0.5
done
busy worker || fail "worker stopped being busy before the check: $(session_record worker)"
client_free worker || fail "worker gained a client: $(session_record worker)"
echo "ok busy session without a client survived $(($(date +%s) - PROMPTED)) s (period $REAP_SECONDS s)"

wait_until $(((TURN_SECONDS + REAP_SECONDS) * 5 + 50)) unlisted worker || fail "worker was not reaped after its turn ended: $(session_record worker)"
ELAPSED=$(($(date +%s) - PROMPTED))
[ "$ELAPSED" -ge $((TURN_SECONDS + REAP_SECONDS - 2)) ] || fail "worker was reaped $ELAPSED s after its prompt, before a full idle period followed its turn"
echo "ok worker was reaped $ELAPSED s after its prompt, once its $TURN_SECONDS s turn ended and the period passed"

echo PASS
