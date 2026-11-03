#!/usr/bin/env bash
# MiniShell regression tests. Run with: make test
# The script is intentionally dependency-free beyond bash and coreutils.

ROOT=$(cd "$(dirname "$0")/.." && pwd)
cd "$ROOT" || exit 1

if [ -x ./minishell ]; then
    BIN=./minishell
elif [ -x ./minishell.exe ]; then
    BIN=./minishell.exe
else
    echo "error: build MiniShell first (run make)" >&2
    exit 1
fi

TMP=$(mktemp -d "${TMPDIR:-/tmp}/minishell-test.XXXXXX") || exit 1
trap 'rm -rf "$TMP"' EXIT

PASS=0
FAIL=0

pass() {
    PASS=$((PASS + 1))
    printf '  ok   %s\n' "$1"
}

fail() {
    FAIL=$((FAIL + 1))
    printf '  FAIL %s\n' "$1"
    if [ -n "${2:-}" ]; then
        printf '       %s\n' "$2"
    fi
}

check_contains() {
    local output=$1 needle=$2 name=$3
    if printf '%s\n' "$output" | grep -Fq -- "$needle"; then
        pass "$name"
    else
        fail "$name" "expected to find: $needle"
    fi
}

check_not_contains() {
    local output=$1 needle=$2 name=$3
    if printf '%s\n' "$output" | grep -Fq -- "$needle"; then
        fail "$name" "did not expect: $needle"
    else
        pass "$name"
    fi
}

check_status() {
    local actual=$1 expected=$2 name=$3
    if [ "$actual" -eq "$expected" ]; then
        pass "$name"
    else
        fail "$name" "expected status $expected, got $actual"
    fi
}

check_file() {
    local path=$1 name=$2
    if [ -f "$path" ]; then
        pass "$name"
    else
        fail "$name" "missing file: $path"
    fi
}

check_dir() {
    local path=$1 name=$2
    if [ -d "$path" ]; then
        pass "$name"
    else
        fail "$name" "missing directory: $path"
    fi
}

check_missing() {
    local path=$1 name=$2
    if [ ! -e "$path" ]; then
        pass "$name"
    else
        fail "$name" "path still exists: $path"
    fi
}

echo "== Shell core =="

out=$("$BIN" -c "cd '$TMP'; pwd" 2>&1)
check_contains "$out" "$TMP" "cd and pwd"

out=$("$BIN" -c 'NAME=Ana; echo $NAME' 2>&1)
check_contains "$out" "Ana" "variable assignment and expansion"

out=$("$BIN" -c 'false || echo recovered' 2>&1)
check_contains "$out" "recovered" "|| executes on failure"

out=$("$BIN" -c 'true && echo yes' 2>&1)
check_contains "$out" "yes" "&& executes on success"

out=$("$BIN" -c 'echo one; echo two' 2>&1)
check_contains "$out" "one" "; runs first command"
check_contains "$out" "two" "; runs second command"

out=$("$BIN" -c 'echo hello | grep hell' 2>&1)
check_contains "$out" "hello" "pipe builtin to builtin"

out=$("$BIN" -c 'echo "hello world" | grep hello' 2>&1)
check_contains "$out" "hello world" "quoted argument through a pipe"

out=$("$BIN" -c 'echo pipeline | cat | grep pipe' 2>&1)
check_contains "$out" "pipeline" "three-stage pipeline"

"$BIN" -c 'uname >/dev/null' >/dev/null 2>&1
st=$?
check_status "$st" 0 "external command"

"$BIN" -c "mkdir -p '$TMP/a/b'" >/dev/null 2>&1
check_dir "$TMP/a/b" "mkdir -p"

"$BIN" -c "touch '$TMP/a/file'" >/dev/null 2>&1
check_file "$TMP/a/file" "touch"

out=$("$BIN" -c "ls '$TMP/a'" 2>&1)
check_contains "$out" "file" "ls"

out=$("$BIN" -c "echo hello > '$TMP/out.txt'; cat < '$TMP/out.txt'" 2>&1)
check_contains "$out" "hello" "> and <"

"$BIN" -c "echo world >> '$TMP/out.txt'" >/dev/null 2>&1
out=$("$BIN" -c "cat '$TMP/out.txt'" 2>&1)
check_contains "$out" "hello" ">> keeps existing content"
check_contains "$out" "world" ">> appends"

"$BIN" -c "cp '$TMP/a/file' '$TMP/a/copy'" >/dev/null 2>&1
check_file "$TMP/a/copy" "cp"

"$BIN" -c "mkdir -p '$TMP/tree/sub'; echo nested > '$TMP/tree/sub/f'; cp -r '$TMP/tree' '$TMP/tree-copy'" >/dev/null 2>&1
check_file "$TMP/tree-copy/sub/f" "cp -r"
"$BIN" -c "rm -r '$TMP/tree-copy'" >/dev/null 2>&1
check_missing "$TMP/tree-copy" "rm -r"

"$BIN" -c "mv '$TMP/a/copy' '$TMP/a/moved'" >/dev/null 2>&1
check_file "$TMP/a/moved" "mv"
check_missing "$TMP/a/copy" "mv removes source name"

"$BIN" -c "rm '$TMP/a/moved'" >/dev/null 2>&1
check_missing "$TMP/a/moved" "rm"

"$BIN" -c "rmdir '$TMP/a/b'" >/dev/null 2>&1
check_missing "$TMP/a/b" "rmdir"

"$BIN" -c "chmod 640 '$TMP/a/file'" >/dev/null 2>&1
out=$("$BIN" -c "ls -l '$TMP/a/file'" 2>&1)
case "$(uname -s)" in
    MSYS*|MINGW*|CYGWIN*)
        # Windows filesystems used by MSYS2 may not expose POSIX group/other bits.
        check_contains "$out" "-rw" "chmod and ls -l permissions"
        ;;
    *)
        check_contains "$out" "-rw-r-----" "chmod and ls -l permissions"
        ;;
esac

out=$("$BIN" -c "ls '$TMP/missing' 2> '$TMP/err.txt'; cat '$TMP/err.txt'" 2>&1)
check_contains "$out" "No such file" "2> redirection"

out=$("$BIN" -c "ls '$TMP/missing' 2>&1 | grep -i 'no such file'" 2>&1)
check_contains "$out" "No such file" "2>&1 redirection"

out=$("$BIN" -c "grep -n hello '$TMP/out.txt'" 2>&1)
check_contains "$out" "1:hello" "grep -n"

out=$("$BIN" -c "grep -E h.llo '$TMP/out.txt'" 2>&1)
check_contains "$out" "hello" "grep -E"

out=$("$BIN" -c 'env | grep HOME=' 2>&1)
check_contains "$out" "HOME=" "env"

out=$("$BIN" -c 'definitely_not_a_command' 2>&1)
st=$?
check_status "$st" 127 "unknown command status"
check_contains "$out" "command not found" "unknown command message"

out=$("$BIN" -c "cat '$TMP/no-such-file'" 2>&1)
st=$?
check_status "$st" 1 "missing file status"
check_contains "$out" "No such file" "missing file message"

out=$("$BIN" -c 'false; echo $?' 2>&1)
check_contains "$out" "1" "captures previous exit status"

out=$("$BIN" -c 'false' 2>&1)
st=$?
check_status "$st" 1 "false returns 1"

out=$("$BIN" -c 'true' 2>&1)
st=$?
check_status "$st" 0 "true returns 0"

"$BIN" -c 'exit 7' >/dev/null 2>&1
st=$?
check_status "$st" 7 "exit status"

cat > "$TMP/script.msh" <<'EOF'
NAME=script
echo $NAME
echo second | grep second
EOF
out=$("$BIN" "$TMP/script.msh" 2>&1)
check_contains "$out" "script" "script file mode"
check_contains "$out" "second" "script file pipeline"

echo
echo "== Scheduler =="

for algo in fcfs sjn priority rr mlq mlfq; do
    out=$("$BIN" -c "sched $algo tests/processes/basic.txt" 2>&1)
    st=$?
    check_status "$st" 0 "sched $algo status"
    check_contains "$out" "Scheduling algorithm:" "sched $algo header"
    check_contains "$out" "Gantt chart:" "sched $algo Gantt"
    check_contains "$out" "PCB table (initial):" "sched $algo PCB"
    check_contains "$out" "Average waiting time:" "sched $algo average waiting"
    check_contains "$out" "Average turnaround time:" "sched $algo average turnaround"
done

out=$("$BIN" -c 'sched fcfs tests/processes/basic.txt' 2>&1)
check_contains "$out" "8.75" "FCFS average waiting value"

out=$("$BIN" -c 'sched rr --quantum 2 tests/processes/basic.txt' 2>&1)
check_contains "$out" "Quantum: 2" "RR custom quantum"

out=$("$BIN" -c 'sched fcfs tests/processes/edge_empty.txt' 2>&1)
check_contains "$out" "No processes loaded." "empty process file"

out=$("$BIN" -c 'sched fcfs tests/processes/edge_single.txt' 2>&1)
check_contains "$out" "P1" "single-process file"
check_contains "$out" "Average waiting time:" "single-process averages"

out=$("$BIN" -c 'sched fcfs tests/processes/edge_idle.txt' 2>&1)
check_contains "$out" "IDLE" "idle time before first arrival"

out=$("$BIN" -c 'sched nope tests/processes/basic.txt' 2>&1)
st=$?
check_status "$st" 2 "unknown scheduler algorithm"

out=$("$BIN" -c 'sched fcfs tests/processes/does-not-exist.txt' 2>&1)
st=$?
check_status "$st" 1 "missing process file"

echo
echo "== Deadlock =="

out=$("$BIN" -c 'deadlock detect tests/deadlock/safe.txt' 2>&1)
check_contains "$out" "No deadlock detected." "detect safe state"

out=$("$BIN" -c 'deadlock detect tests/deadlock/deadlocked.txt' 2>&1)
check_contains "$out" "Deadlock detected." "detect deadlock"
check_contains "$out" "P0 P1" "list deadlocked processes"

out=$("$BIN" -c 'deadlock detect tests/deadlock/positional.txt' 2>&1)
check_contains "$out" "Deadlock detected." "positional input format"

out=$("$BIN" -c 'deadlock banker tests/deadlock/safe.txt' 2>&1)
check_contains "$out" "State is SAFE." "banker safe state"
check_contains "$out" "Safe sequence:" "banker safe sequence"

out=$("$BIN" -c 'deadlock banker tests/deadlock/unsafe.txt' 2>&1)
check_contains "$out" "State is UNSAFE." "banker unsafe state"

out=$("$BIN" -c 'deadlock banker tests/deadlock/edge_single.txt' 2>&1)
check_contains "$out" "State is SAFE." "banker single-process edge case"

out=$("$BIN" -c 'deadlock banker tests/deadlock/safe.txt --request 1 1 0 2' 2>&1)
check_contains "$out" "Request GRANTED." "banker grants safe request"

out=$("$BIN" -c 'deadlock banker tests/deadlock/safe.txt --request 0 7 4 3' 2>&1)
check_contains "$out" "Request DENIED." "banker denies impossible request"

out=$("$BIN" -c 'deadlock banker tests/deadlock/request.txt' 2>&1)
check_contains "$out" "Resource request: P1 requests 1 0 2" "file-based request"
check_contains "$out" "Request GRANTED." "file-based request granted"

out=$("$BIN" -c 'deadlock banker tests/deadlock/safe.txt --request 9 1 0 0' 2>&1)
st=$?
check_status "$st" 2 "invalid request PID status"
check_contains "$out" "out of range" "invalid request PID message"

out=$("$BIN" -c 'deadlock nope tests/deadlock/safe.txt' 2>&1)
st=$?
check_status "$st" 2 "unknown deadlock mode"

out=$("$BIN" -c 'deadlock banker tests/deadlock/does-not-exist.txt' 2>&1)
st=$?
check_status "$st" 1 "missing system file"

echo
printf 'Tests passed: %d, failed: %d\n' "$PASS" "$FAIL"
if [ "$FAIL" -ne 0 ]; then
    exit 1
fi
exit 0
