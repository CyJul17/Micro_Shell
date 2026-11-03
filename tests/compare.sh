#!/usr/bin/env bash
# Compare the six scheduling algorithms on one process file.
# Usage: bash tests/compare.sh [process-file]
# Default file: tests/processes/mixed.txt

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

FILE=${1:-tests/processes/mixed.txt}
if [ ! -f "$FILE" ]; then
    echo "error: process file not found: $FILE" >&2
    exit 1
fi

printf 'Comparison for %s\n\n' "$FILE"
printf '%-10s %-18s %-18s\n' "Algorithm" "Avg waiting" "Avg turnaround"
printf '%-10s %-18s %-18s\n' "----------" "------------------" "------------------"

for algo in fcfs sjn priority rr mlq mlfq; do
    output=$("$BIN" -c "sched $algo '$FILE'" 2>&1)
    waiting=$(printf '%s\n' "$output" | awk '/Average waiting time:/ {print $4}')
    turnaround=$(printf '%s\n' "$output" | awk '/Average turnaround time:/ {print $4}')
    if [ -z "$waiting" ] || [ -z "$turnaround" ]; then
        printf '%-10s %-18s %-18s\n' "$algo" "ERROR" "ERROR"
        continue
    fi
    printf '%-10s %-18s %-18s\n' "$algo" "$waiting" "$turnaround"
done
