#!/usr/bin/env bash
set -euo pipefail
ROOT=$(cd "$(dirname "$0")/.." && pwd)
BUILD=${HYPERAGENT_BUILD:-$ROOT/build}
case "$BUILD" in /*) ;; *) BUILD=$PWD/$BUILD ;; esac
HYPERAGENT=$BUILD/hyperagent
PORT=${ORCH_TEST_PORT:-18947}
LIVE_PORTS="8787 8795"

fail() {
    echo "FAIL: $1"
    for log in "${TMP:-/nonexistent}/mcp.err" "${TMP:-/nonexistent}/orch/server.log"; do
        [ -s "$log" ] && { echo "--- $log"; tail -n 20 "$log"; }
    done
    exit 1
}

for live in $LIVE_PORTS; do
    [ "$PORT" != "$live" ] || fail "port $PORT belongs to a live orchestrator"
done
[ -x "$HYPERAGENT" ] || fail "no hyperagent binary at $HYPERAGENT; set HYPERAGENT_BUILD"
[ -x "$BUILD/ccbridge" ] || fail "no ccbridge binary in $BUILD"
[ -x "$BUILD/fake_claude/bin/claude" ] || fail "no fake claude in $BUILD"

port_listening() {
    (exec 9<>"/dev/tcp/127.0.0.1/$PORT") 2>/dev/null
}
port_listening && fail "port $PORT is already in use; set ORCH_TEST_PORT"

TMP=$(mktemp -d)
export HOME=$TMP/home
export ORCH_HOME=$TMP/orch
export ORCH_PARENT=$TMP/projects
export ORCH_PORT=$PORT
export ORCH_HOST=127.0.0.1
export CCBRIDGE_BIN=$BUILD/ccbridge
export FAKE_CLAUDE_ARGV_LOG=$TMP/argv.log
export FAKE_CLAUDE_TURN_MS=200
export PATH=$BUILD/fake_claude/bin:$PATH
unset ORCH_CLAUDE_MODEL ORCH_JOURNAL CCBRIDGE_TEMPLATES HYPERAGENT_ROOT
mkdir -p "$HOME" "$ORCH_HOME" "$ORCH_PARENT"
for project in alpha beta gamma delta; do
    mkdir -p "$ORCH_PARENT/$project"
    : > "$ORCH_PARENT/$project/AGENTS.md"
done
: > "$FAKE_CLAUDE_ARGV_LOG"

MCP_PID=
stop_test_processes() {
    exec 3>&- 2>/dev/null || true
    [ -z "$MCP_PID" ] || wait "$MCP_PID" 2>/dev/null || true
    if [ -s "$ORCH_HOME/server.pid" ]; then
        local server
        server=$(cat "$ORCH_HOME/server.pid")
        if [ "$server" != "${STALE_PID:-}" ] && kill -0 "$server" 2>/dev/null; then
            kill "$server" 2>/dev/null || true
            for _ in $(seq 1 100); do
                kill -0 "$server" 2>/dev/null || break
                sleep 0.1
            done
        fi
    fi
    rm -rf "$TMP"
}
trap stop_test_processes EXIT

sleep 0 &
STALE_PID=$!
wait "$STALE_PID"
kill -0 "$STALE_PID" 2>/dev/null && fail "could not make a dead pid"
printf '%s' "$STALE_PID" > "$ORCH_HOME/server.pid"

mkfifo "$TMP/mcp.in"
"$HYPERAGENT" mcp < "$TMP/mcp.in" > "$TMP/mcp.out" 2> "$TMP/mcp.err" &
MCP_PID=$!
exec 3> "$TMP/mcp.in"

REQUEST_ID=0
REPLY_TEXT=
reply_for() {
    local id=$1 line
    for _ in $(seq 1 900); do
        line=$(grep -m1 -E "\"id\": ?$id[,}]" "$TMP/mcp.out" || true)
        if [ -n "$line" ]; then
            REPLY_TEXT=$(printf '%s' "$line" | sed 's/\\n/\n/g; s/\\"/"/g')
            return 0
        fi
        kill -0 "$MCP_PID" 2>/dev/null || fail "hyperagent mcp exited before answering request $id"
        sleep 0.2
    done
    fail "no reply to request $id"
}
tool() {
    REQUEST_ID=$((REQUEST_ID + 1))
    printf '{"jsonrpc":"2.0","id":%d,"method":"tools/call","params":{"name":"%s","arguments":%s}}\n' \
        "$REQUEST_ID" "$1" "$2" >&3
    reply_for "$REQUEST_ID"
}
expect_reply() {
    case "$REPLY_TEXT" in *"$1"*) ;; *) fail "$2: reply lacks '$1': $REPLY_TEXT" ;; esac
}
last_launch() {
    grep -F "\"cwd\":\"$ORCH_PARENT/$1\"" "$FAKE_CLAUDE_ARGV_LOG" | tail -n 1
}
launch_count() {
    grep -cF "\"cwd\":\"$ORCH_PARENT/$1\"" "$FAKE_CLAUDE_ARGV_LOG" || true
}
expect_launch() {
    local launch
    launch=$(last_launch "$1")
    case "$launch" in *"$2"*) ;; *) fail "$3: last launch of $1 lacks '$2': $launch" ;; esac
}
refuse_launch() {
    local launch
    launch=$(last_launch "$1")
    case "$launch" in *"$2"*) fail "$3: last launch of $1 has '$2': $launch" ;; esac
}
saved_model() {
    sed -n "/\"$1\": {/,/}/p" "$ORCH_HOME/state.json" | sed -n 's/.*"model": "\(.*\)".*/\1/p'
}

REQUEST_ID=$((REQUEST_ID + 1))
printf '{"jsonrpc":"2.0","id":%d,"method":"initialize","params":{"protocolVersion":"2025-06-18","capabilities":{},"clientInfo":{"name":"orch_launch","version":"0"}}}\n' "$REQUEST_ID" >&3
reply_for "$REQUEST_ID"
printf '{"jsonrpc":"2.0","method":"notifications/initialized"}\n' >&3

port_listening && fail "a server answered before any prompt was sent"
tool instruct_many '{"jobs":[{"name":"alpha","prompt":"hello alpha"},{"name":"delta","prompt":"hello delta"}]}'
expect_reply '"state": "running"' "instruct_many with a stale pid file"
TURN_IDS=$(printf '%s' "$REPLY_TEXT" | grep -oE '"[0-9a-f]{12}"' | tr -d '"')
[ "$(printf '%s\n' "$TURN_IDS" | grep -c .)" = 2 ] || fail "instruct_many returned no two turn ids: $REPLY_TEXT"
for turn in $TURN_IDS; do
    tool turn_result "{\"turn_id\":\"$turn\",\"wait_s\":90}"
    expect_reply '"status": "turn_end"' "background turn after a stale pid file"
    expect_reply 'fake reply to: hello' "background turn after a stale pid file"
done
port_listening || fail "no server listens after the prompt"
SERVERS=$(pgrep -fc -- "serve --host 127.0.0.1 --port $PORT" || true)
[ "$SERVERS" = 1 ] || fail "concurrent prompts left $SERVERS servers on port $PORT"
NEW_PID=$(cat "$ORCH_HOME/server.pid")
[ "$NEW_PID" != "$STALE_PID" ] || fail "server.pid still names the stale pid $STALE_PID"
kill -0 "$NEW_PID" 2>/dev/null || fail "server.pid names $NEW_PID, which is not running"
echo "ok stale pid file led to one server start for two concurrent prompts (pid $STALE_PID -> $NEW_PID)"

expect_launch alpha '"--model","haiku"' "default model"
[ -z "$(saved_model alpha)" ] || fail "alpha has a saved model without being given one"
echo "ok project without a saved model launches on the default"

tool start_projects '{"names":["beta"],"model":"fake-model-b"}'
expect_reply 'beta: started' "start_projects with a model"
expect_reply 'model=fake-model-b' "start_projects with a model"
expect_launch beta '"--model","fake-model-b"' "start with a model"
refuse_launch beta 'haiku' "start with a model"
[ "$(saved_model beta)" = "fake-model-b" ] || fail "state.json did not keep beta's model: $(cat "$ORCH_HOME/state.json")"
tool peer_status '{}'
expect_reply '"model": "fake-model-b"' "peer_status launch model"
expect_reply '"model": "haiku"' "peer_status launch model"
echo "ok model given at start reaches argv, state.json and peer_status"

tool instruct '{"name":"beta","prompt":"hello beta","wait":true}'
expect_reply 'fake reply to: hello beta' "instruct beta"
tool stop_peer '{"name":"beta"}'
expect_reply 'stopped beta' "stop beta"
BEFORE=$(launch_count beta)
tool start_projects '{"names":["beta"]}'
expect_reply 'beta: started' "restart beta"
[ "$(launch_count beta)" -gt "$BEFORE" ] || fail "beta was not launched again"
expect_launch beta '"--model","fake-model-b"' "start from the saved model"
echo "ok saved model reaches argv on a later start"

tool stop_peer '{"name":"beta"}'
BEFORE=$(launch_count beta)
tool resume_project '{"project":"beta"}'
expect_reply '"state": "resumed"' "resume beta"
expect_reply '"model": "fake-model-b"' "resume beta"
[ "$(launch_count beta)" -gt "$BEFORE" ] || fail "beta was not relaunched by resume"
expect_launch beta '"--resume"' "resume from the saved model"
expect_launch beta '"--model","fake-model-b"' "resume from the saved model"
echo "ok saved model reaches argv on resume"

tool stop_peer '{"name":"alpha"}'
tool resume_project '{"project":"alpha","harness":"claude-fable"}'
expect_reply '"state": "resumed"' "resume alpha on claude-fable"
expect_reply '"harness": "claude-fable"' "resume alpha on claude-fable"
expect_launch alpha '"--model","claude-fable-5-2"' "claude-fable template"
refuse_launch alpha 'haiku' "claude-fable template"
tool stop_peer '{"name":"alpha"}'
BEFORE=$(launch_count alpha)
tool start_projects '{"names":["alpha"]}'
expect_reply 'harness=claude-fable' "remembered claude-fable harness"
[ "$(launch_count alpha)" -gt "$BEFORE" ] || fail "alpha was not launched again"
expect_launch alpha '"--model","claude-fable-5-2"' "remembered claude-fable harness"
echo "ok claude-fable selects its template and is remembered"

"$HYPERAGENT" orch start gamma --model fake-model-c --settle 0.2 > "$TMP/cli.out" 2>&1 || fail "orch start gamma: $(cat "$TMP/cli.out")"
grep -qF 'model=fake-model-c' "$TMP/cli.out" || fail "orch start did not report the model: $(cat "$TMP/cli.out")"
expect_launch gamma '"--model","fake-model-c"' "orch start --model"
"$HYPERAGENT" orch prompt gamma "hello gamma" > "$TMP/cli.out" 2>&1 || fail "orch prompt gamma: $(cat "$TMP/cli.out")"
"$HYPERAGENT" orch stop gamma > /dev/null
BEFORE=$(launch_count gamma)
"$HYPERAGENT" orch resume gamma --settle 0.2 > "$TMP/cli.out" 2>&1 || fail "orch resume gamma: $(cat "$TMP/cli.out")"
[ "$(launch_count gamma)" -gt "$BEFORE" ] || fail "gamma was not relaunched by orch resume"
expect_launch gamma '"--resume"' "orch resume from the saved model"
expect_launch gamma '"--model","fake-model-c"' "orch resume from the saved model"
"$HYPERAGENT" orch stop gamma > /dev/null
"$HYPERAGENT" orch start gamma --model default --settle 0.2 > "$TMP/cli.out" 2>&1 || fail "orch start gamma --model default: $(cat "$TMP/cli.out")"
expect_launch gamma '"--model","haiku"' "forgotten model"
[ -z "$(saved_model gamma)" ] || fail "state.json still keeps gamma's model"
echo "ok orch CLI saves, reuses and forgets a model"

echo PASS
