# MiniShell: Sample Inputs and Test Cases

This file lists every sample input file shipped with MiniShell, what each one
tests, how to run it, and the result it produces. The expected results below
were copied from real runs of the simulators, not written by hand.

All the automated checks live in `tests/run_tests.sh` (126 checks). Run them
with:

```sh
make test
```

## 1. Process scheduling inputs (`tests/processes/`)

File format: one process per line, `PID arrival burst priority`. Lines
starting with `#` are comments. A smaller priority number means higher
priority.

| File | Contents | What it tests |
| --- | --- | --- |
| `basic.txt` | 4 processes, arrivals 0-3, bursts 4-9 | Baseline workload for all six algorithms |
| `mixed.txt` | 5 processes, mixed arrivals, bursts, priorities | Main workload for the Module 4 comparison |
| `edge_single.txt` | 1 process (`1 0 5 2`) | **Edge case:** a single process |
| `edge_empty.txt` | No processes (comments only) | **Edge case:** an empty ready queue |
| `edge_idle.txt` | 2 processes, first arrives at t=10 | **Edge case:** CPU idle before the first arrival |

### Expected results

Averages from running each algorithm on the two main workloads:

| Algorithm | `basic.txt` avg waiting | `basic.txt` avg turnaround | `mixed.txt` avg waiting | `mixed.txt` avg turnaround |
| --- | ---: | ---: | ---: | ---: |
| FCFS | 8.75 | 15.25 | 5.20 | 9.20 |
| SJN | 7.75 | 14.25 | 3.80 | 7.80 |
| Priority | 7.75 | 14.25 | 5.40 | 9.40 |
| Round-Robin (q=4) | 11.75 | 18.25 | 5.60 | 9.60 |
| MLQ | 9.50 | 16.00 | 4.80 | 8.80 |
| MLFQ | 13.00 | 19.50 | 7.40 | 11.40 |

Gantt charts for `mixed.txt`:

```text
FCFS      [0-3] P1 | [3-9] P2 | [9-13] P3 | [13-15] P4 | [15-20] P5
SJN       [0-3] P1 | [3-7] P3 | [7-9] P4 | [9-14] P5 | [14-20] P2
Priority  [0-6] P2 | [6-8] P4 | [8-11] P1 | [11-16] P5 | [16-20] P3
RR (q=4)  [0-3] P1 | [3-7] P2 | [7-11] P3 | [11-13] P4 | [13-15] P2 | [15-20] P5
MLQ       [0-2] P1 | [2-4] P2 | [4-5] P1 | [5-7] P4 | [7-11] P2 | [11-15] P3 | [15-20] P5
MLFQ      [0-2] P1 | [2-4] P2 | [4-6] P3 | [6-8] P4 | [8-10] P5 | [10-11] P1 | [11-15] P2 | [15-17] P3 | [17-20] P5
```

Edge cases (FCFS shown):

| Command | Result |
| --- | --- |
| `sched fcfs tests/processes/edge_single.txt` | Gantt `[0-5] P1`; average waiting 0.00, turnaround 5.00 |
| `sched fcfs tests/processes/edge_empty.txt` | `No processes loaded.` (no crash) |
| `sched fcfs tests/processes/edge_idle.txt` | Gantt `[0-10] IDLE \| [10-13] P1 \| [13-15] P2`; average waiting 0.50, turnaround 3.00 |

### Error cases

| Command | Result |
| --- | --- |
| `sched nope tests/processes/basic.txt` | `unknown algorithm`, exit status 2 |
| `sched fcfs tests/processes/does-not-exist.txt` | file error, exit status 1 |
| `sched mlq --quantum 3 tests/processes/basic.txt` | `--quantum only applies to rr`, exit status 2 |

## 2. Deadlock inputs (`tests/deadlock/`)

File format (labeled): `processes N`, `resources M`, then `allocation`
(N rows), `max` (N rows), `available` (one row). Optional extras: a
`requests` matrix for detection and a `request PID r1 r2 ...` line for the
Banker's request test. `positional.txt` uses the compact unlabeled form.

| File | Size | What it tests | Result |
| --- | --- | --- | --- |
| `safe.txt` | 5 processes, 3 resources | Classic textbook safe state | `detect`: no deadlock. `banker`: **SAFE**, sequence `P1 -> P3 -> P0 -> P2 -> P4` |
| `unsafe.txt` | 3 processes, 2 resources | Every process needs 1 more of each, nothing free | `banker`: **UNSAFE**, unfinished `P0 P1 P2` |
| `deadlocked.txt` | 2 processes, 2 resources | Two processes stuck, Available = 0 0 | `detect`: deadlock, `P0 P1` + recovery suggestion |
| `positional.txt` | 3 processes, 2 resources | Unlabeled (positional) input format | `detect`: deadlock `P0 P1 P2`; `banker`: UNSAFE |
| `request.txt` | 5 processes, 3 resources | Request stored in the file (`request: 1 1 0 2`) | `banker`: request by P1 **GRANTED**, state stays safe |
| `requests_ok.txt` | 5 processes, 3 resources | Detection with a Request matrix, no deadlock | `detect`: no deadlock, order `P0 -> P2 -> P1 -> P3 -> P4` |
| `requests_deadlock.txt` | 5 processes, 3 resources | Same system, P2 asks for one more C | `detect`: deadlock `P1 P2 P3 P4`; only P0 can finish |
| `edge_single.txt` | 1 process, 1 resource | **Edge case:** a single process | `banker`: SAFE, sequence `P0` |

### Resource-request cases

| Command | Result |
| --- | --- |
| `deadlock banker tests/deadlock/safe.txt --request 1 1 0 2` | **GRANTED**, state remains safe |
| `deadlock banker tests/deadlock/safe.txt --request 0 7 4 3` | **DENIED**, not enough available resources (P0 asks for 7 of A, only 3 free) |
| `deadlock banker tests/deadlock/safe.txt --request 9 1 0 0` | error: request PID out of range, exit status 2 |

### Error cases

| Command | Result |
| --- | --- |
| `deadlock nope tests/deadlock/safe.txt` | unknown mode, exit status 2 |
| `deadlock banker tests/deadlock/does-not-exist.txt` | file error, exit status 1 |
| A file that declares `processes` twice | `process count already declared`, exit status 1 |

## 3. Shell test cases (`tests/run_tests.sh`)

The shell checks run MiniShell with `-c "command"` (or with piped input) and
compare the output and exit status. They are grouped below by spec requirement.

| Area | Example commands tested |
| --- | --- |
| File and directory built-ins | `cd`, `pwd`, `ls`, `mkdir -p`, `touch`, `cp`, `cp -r`, `mv`, `rm`, `rm -r`, `rmdir`, `cat` |
| Permissions | `chmod 640 file` then `ls -l file` shows `-rw-r-----` |
| Redirection | `echo hello > f`, `>> f`, `cat < f`, `ls missing 2> err`, `2>&1`, `>&2`, `1>&2` |
| Pipes | `echo hello \| grep hell`, three-stage `echo \| cat \| grep` |
| Variables | `NAME=Ana; echo $NAME`, `env \| grep HOME=`, temporary `NAME=value cmd` |
| Conditionals | `false \|\| echo recovered`, `true && echo yes`, `echo one; echo two`, `$?` |
| grep | `grep -n hello f` (line numbers), `grep -E h.llo f` (regex) |
| Error handling | unknown command (status 127), missing file (status 1), `exit 7` status, `echo a > > x` |
| Quoting | `echo "\|" ">"` prints the symbols instead of piping or redirecting |
| Input safety | `cat < file` from piped input does not close the shell; `cp -r dir dir/sub` is refused |
| Script mode | `minishell script.msh` runs a script with variables and pipes |
| Simulators | all six schedulers on `basic.txt`, every deadlock file above |

A sample script is also included at `tests/scripts/demo.msh`:

```sh
./minishell tests/scripts/demo.msh
```
