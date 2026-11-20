#!/usr/bin/env bash
set -euo pipefail
ROOT=$(cd "$(dirname "$0")/.." && pwd)
BUILD=${HYPERAGENT_BUILD:-$ROOT/build}
case "$BUILD" in /*) ;; *) BUILD=$PWD/$BUILD ;; esac
HYPERAGENT=$BUILD/hyperagent
CCBRIDGE=$BUILD/ccbridge
PORT=${FOLLOW_TEST_PORT:-18949}
LIVE_PORTS="8787 8795"
PEER=peer
SESSION=3c9e7a52-6f1d-4b8a-8e2f-0d4c5b7a9e16
SERVER_PID=
CLIENT_PID=
WATCH_PID=
HOLDER_PID=
TMP=

stop_pid() {
    if [ -n "$1" ] && kill -0 "$1" 2>/dev/null; then
        kill "$1" 2>/dev/null || true
        wait "$1" 2>/dev/null || true
    fi
}
cleanup() {
    stop_pid "$HOLDER_PID"
    stop_pid "$WATCH_PID"
    stop_pid "$CLIENT_PID"
    stop_pid "$SERVER_PID"
    if [ -n "$TMP" ]; then
        rm -rf "$TMP"
    fi
}
fail() {
    echo "FAIL: $1"
    for log in "${TMP:-/nonexistent}/server.log" "${TMP:-/nonexistent}/watch.log"; do
        if [ -s "$log" ]; then
            echo "--- $log"
            tail -n 20 "$log"
        fi
    done
    exit 1
}
trap cleanup EXIT

for live in $LIVE_PORTS; do
    [ "$PORT" != "$live" ] || fail "port $PORT belongs to a live orchestrator"
done
[ -x "$HYPERAGENT" ] || fail "no hyperagent binary at $HYPERAGENT; set HYPERAGENT_BUILD"
[ -x "$CCBRIDGE" ] || fail "no ccbridge binary in $BUILD"
[ -x "$BUILD/fake_claude/bin/claude" ] || fail "no fake claude in $BUILD"
command -v jq >/dev/null || fail "jq is required"

port_listening() {
    (exec 9<>"/dev/tcp/127.0.0.1/$PORT") 2>/dev/null
}
port_listening && fail "port $PORT is already in use; set FOLLOW_TEST_PORT"

TMP=$(mktemp -d)
export HOME=$TMP/home
export ORCH_HOME=$TMP/orch
export ORCH_HOST=127.0.0.1
export ORCH_PORT=$PORT
export FAKE_CLAUDE_TURN_MS=500
export PATH=$BUILD/fake_claude/bin:$PATH
export CCBRIDGE_TEMPLATES=$ROOT/airc/templates
unset ORCH_JOURNAL ORCH_PARENT CCBRIDGE_IDLE_REAP CCBRIDGE_CWD
mkdir -p "$HOME" "$ORCH_HOME" "$TMP/work" "$TMP/scratch"

"$CCBRIDGE" serve --host 127.0.0.1 --port "$PORT" --cwd "$TMP/scratch" --idle-reap 3600 > "$TMP/server.log" 2>&1 &
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
peer_record() {
    sessions | tr -d '\r\n' | grep -oE "\"$PEER\": ?\{[^}]*\}" || true
}
peer_alive() {
    peer_record | grep -qE '"alive": ?true'
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
transcript_lines() {
    wc -l < "$TRANSCRIPT"
}
journal_file() {
    find "$ORCH_HOME/projects" -name "$SESSION.jsonl" 2>/dev/null | head -n 1 || true
}
journal_count() {
    local file
    file=$(journal_file)
    if [ -n "$file" ]; then
        wc -l < "$file"
    else
        echo 0
    fi
}
journal_has() {
    local file
    file=$(journal_file)
    [ -n "$file" ] && jq -e --arg text "$1" 'select(.kind == "text" and .text == $text)' "$file" >/dev/null 2>&1
}
journal_has_result() {
    local file
    file=$(journal_file)
    [ -n "$file" ] && jq -e 'select(.kind == "result")' "$file" >/dev/null 2>&1
}
journal_results() {
    jq -r 'select(.kind == "result") | .status + " " + (.duration_s | tostring)' "$(journal_file)"
}
transcript_pairs() {
    jq -r 'if .type == "assistant" then (.message.content[] | if .type == "text" then "text\t" + .text elif .type == "tool_use" then "tool\t" + .name else empty end) elif .type == "user" and (.message.content | type) == "string" then "you\t" + .message.content else empty end'
}
journal_pairs() {
    jq -r 'select(.kind == "text" or .kind == "tool" or .kind == "you") | .kind + "\t" + (.text // "")' "$(journal_file)"
}
lock_held() {
    ! flock -n "$LOCK" -c true
}

SEED_DIR=$HOME/.claude/projects/$(printf '%s' "$TMP/work" | sed 's/[^A-Za-z0-9]/-/g')
mkdir -p "$SEED_DIR"
: > "$SEED_DIR/$SESSION.jsonl"
timeout 60 "$CCBRIDGE" client --url "ws://127.0.0.1:$PORT/ws?resume=$SESSION" --harness claude --session "$PEER" \
    --cwd "$TMP/work" "a turn before the watch starts" > "$TMP/client.out" 2>&1 &
CLIENT_PID=$!
wait "$CLIENT_PID" || fail "the first client turn failed: $(cat "$TMP/client.out")"
CLIENT_PID=
wait_until 50 peer_alive || fail "peer $PEER never listed alive: $(peer_record)"
peer_record | grep -q "$SESSION" || fail "peer $PEER is not resumed as $SESSION: $(peer_record)"
TRANSCRIPT=$(find "$HOME/.claude/projects" -name "$SESSION.jsonl" | head -n 1)
[ -n "$TRANSCRIPT" ] || fail "no Claude transcript for $SESSION under $HOME/.claude/projects"

"$HYPERAGENT" watch > "$TMP/watch.log" 2>&1 &
WATCH_PID=$!
sleep 3
kill -0 "$WATCH_PID" 2>/dev/null || fail "the watch exited: $(cat "$TMP/watch.log")"

R_THINK='{"type":"assistant","message":{"role":"assistant","content":[{"type":"thinking","thinking":"plan the read"}]}}'
R_TOOL='{"type":"assistant","message":{"role":"assistant","content":[{"type":"text","text":"Reading the file now."},{"type":"tool_use","id":"toolu_01","name":"Read","input":{"file_path":"/tmp/x"}}]}}'
R_TOOL_RESULT='{"type":"user","message":{"role":"user","content":[{"type":"tool_result","tool_use_id":"toolu_01","content":"ok"}]}}'
R_DONE='{"type":"assistant","message":{"role":"assistant","content":[{"type":"text","text":"Background agent finished."}]}}'
R_NOTE='{"type":"user","message":{"role":"user","content":"task-notification: the background agent finished"}}'
R_TURN='{"type":"system","subtype":"turn_duration","durationMs":4300}'
R_QUEUE='{"type":"queue-operation","operation":"enqueue"}'
R_LOCKED_TEXT='{"type":"assistant","message":{"role":"assistant","content":[{"type":"text","text":"Written while a job holds the lock."}]}}'
R_LOCKED_TURN='{"type":"system","subtype":"turn_duration","durationMs":1000}'
R_AFTER='{"type":"assistant","message":{"role":"assistant","content":[{"type":"text","text":"After the lock."}]}}'

BEFORE=$(transcript_lines)
printf '%s\n' "$R_THINK" "$R_TOOL" "$R_TOOL_RESULT" "$R_DONE" "$R_NOTE" "$R_TURN" "$R_QUEUE" >> "$TRANSCRIPT"
wait_until 60 journal_has_result || fail "the autonomous turn never reached the journal: $(cat "$TMP/watch.log")"
P1_END=$(transcript_lines)
sed -n "$((BEFORE + 1)),$((P1_END))p" "$TRANSCRIPT" | transcript_pairs > "$TMP/expect1"
journal_pairs > "$TMP/actual1"
diff "$TMP/expect1" "$TMP/actual1" || fail "the journal does not match the autonomous transcript records"
[ "$(journal_results)" = "turn_end 4.3" ] || fail "expected one turn_end result of 4.3 s, journal has: $(journal_results)"
echo "ok autonomous turn with no job attached: $(wc -l < "$TMP/actual1") records (text, tool, you) match the transcript one for one, plus one turn_end result"

LOCK=$ORCH_HOME/locks/$PEER.lock
wait_until 50 test -e "$LOCK" || fail "the watch never created $LOCK"
( exec 9>"$LOCK"; flock -x 9; exec sleep 300 ) &
HOLDER_PID=$!
wait_until 50 lock_held || fail "the holder never took $LOCK"
LOCKED_BEFORE=$(journal_count)
printf '%s\n' "$R_LOCKED_TEXT" "$R_LOCKED_TURN" >> "$TRANSCRIPT"
sleep 3
[ "$(journal_count)" = "$LOCKED_BEFORE" ] || fail "records were written to the journal while a job held the lock"
P2_END=$(transcript_lines)
stop_pid "$HOLDER_PID"
HOLDER_PID=
sleep 3
[ "$(journal_count)" = "$LOCKED_BEFORE" ] || fail "records written under the lock were journaled after it released"

printf '%s\n' "$R_AFTER" >> "$TRANSCRIPT"
wait_until 60 journal_has "After the lock." || fail "the turn after the lock never reached the journal"
{ sed -n "$((BEFORE + 1)),$((P1_END))p" "$TRANSCRIPT"; sed -n "$((P2_END + 1)),\$p" "$TRANSCRIPT"; } | transcript_pairs > "$TMP/expect2"
journal_pairs > "$TMP/actual2"
diff "$TMP/expect2" "$TMP/actual2" || fail "the journal duplicates or misses records around the job lock"
[ "$(journal_results)" = "turn_end 4.3" ] || fail "the journal has an unexpected result set: $(journal_results)"
echo "ok no record was duplicated while a job held the lock, and the turn after it was journaled once"

stop_pid "$WATCH_PID"
WATCH_PID=
echo PASS
