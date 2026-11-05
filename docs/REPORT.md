# MiniShell: Shell, CPU Scheduling, and Deadlock Simulator

**Course:** CmpSc 123  
**Project:** Final Project — Operating Systems  
**Language:** C (POSIX)

**Developers:**

- Queypo, Ckhiel Joshua
- Viloria, Julian Shaun

---

## Abstract

MiniShell is a small operating-system project written in C. It combines three
parts that are normally studied separately: a working command shell, a set of
CPU scheduling simulators, and a deadlock detection / Banker's algorithm
simulator. The shell uses POSIX process creation and pipes (`fork`, `execvp`,
`pipe`, and `dup2`) rather than simulating commands in user space. The scheduling
module implements FCFS, SJN, Priority, Round-Robin, Multilevel Queue, and
Multilevel Feedback Queue directly from their definitions. The deadlock module
implements both the detection algorithm and the Banker's safety algorithm,
including safe-sequence reporting and resource-request handling. A regression
suite of 127 checks and a comparison report generated from actual runs verify the
implementation.

## 1. Introduction

The project has four goals:

1. Build a shell that supports the required built-ins, external programs,
   redirection, pipes, variables, conditional execution, and robust error
   handling.
2. Simulate six CPU scheduling algorithms and report PCB tables, Gantt charts,
   waiting times, and turnaround times.
3. Detect deadlocks and apply the Banker's algorithm, including safe sequences
   and resource requests.
4. Compare the schedulers on the same workloads and document the results.

The project is intentionally written in one language. All algorithms are
implemented in C using only the standard library and POSIX APIs; no external
scheduling or deadlock library is used.

## 2. System design

MiniShell is organized as a set of cooperating modules:

```text
+----------------------------- MiniShell process -----------------------------+
|                                                                             |
|  shell_getline  ->  parser.c  ->  executor (main.c)                         |
|                         |              |                                    |
|                         |              +--> builtin.c (built-ins, grep)     |
|                         |              +--> fork / execvp / pipe / dup2      |
|                         |                                                   |
|                         +--> scheduler.c (six algorithms)                   |
|                         +--> deadlock.c  (detection, Banker's algorithm)    |
|                                                                             |
+-----------------------------------------------------------------------------+
```

The parser turns one command line into a sequence of command groups separated by
`;`, `&&`, and `||`. Each group is split into pipeline stages, and each stage is
split into arguments and redirections. The executor runs `cd` and `exit` in the
shell process, because they change the shell's own state, and runs every other
command (including built-ins such as `cat` and `grep`) in a child process. A
pipeline creates one child per stage and connects the stages with pipes.

The scheduler and deadlock simulators are built-ins. They read ordinary text
files, so they can be tested independently of the interactive shell.

## 3. Shell implementation

### 3.1 Parsing and quoting

The lexer in `Shell/parser.c` handles:

- whitespace separation;
- single quotes, which preserve all characters literally;
- double quotes, which preserve spaces but still expand variables;
- backslash escaping;
- variable expansion for `$NAME`, `${NAME}`, and `$?`;
- tilde expansion at the start of a word;
- redirection tokens such as `>`, `>>`, `<`, `2>`, `2>>`, and `2>&1`;
- comments beginning with `#` at the start of a word.

The sequence splitter recognizes `;`, `&&`, and `||` only outside quotes. The
`&` in `2>&1` is explicitly excluded from background-job detection; a standalone
`&` produces a clear "background jobs are not supported" error instead of being
misparsed.

### 3.2 Execution model

For an external command, the shell calls `fork()`. The child applies
redirections with `dup2()` and then calls `execvp()`. The parent waits for the
child and records its exit status.

For a pipeline, the shell creates one child per stage. Each child connects its
standard input and output to the appropriate pipe, applies its own redirections,
and then runs a built-in or an external command. The parent closes the pipe ends
it does not need and waits for all children. The pipeline status is the status of
its last stage.

`cd` and `exit` run in the shell process and temporarily save and restore file
descriptors when redirections are present, so a redirection does not leak into
the next command. All other built-ins run in a forked child, like external
commands. This lets Ctrl+C stop a built-in such as `cat`, and keeps a built-in
that reads standard input to end-of-file from closing the shell's own input.

`NAME=value cmd` sets `NAME` only for `cmd`. A line made only of assignments
(`NAME=value`) sets the variable in the shell.

### 3.3 Built-ins

The required built-ins are implemented in `Shell/builtin.c`:

`pwd`, `cd`, `ls`, `mkdir`, `rmdir`, `touch`, `cp`, `mv`, `rm`, `cat`, and
`chmod`. In addition, the shell provides `grep`, `echo`, `env`, `true`, `false`,
`exit`, and `help`.

`ls` supports `-a` and `-l`, including the standard `-rwx` permission string.
`chmod` accepts numeric octal modes. `cp` and `rm` support recursive operation.
`grep` supports substring matching by default and POSIX extended regular
expressions with `-E`; `-i` and `-n` are also available.

### 3.4 Redirection, pipes, variables, and conditionals

Redirection is applied left to right, so the order of `2>&1` matters. For
example, `cmd >out.txt 2>&1` sends both streams to the file, while
`cmd 2>&1 >out.txt` sends standard error to the original standard output and
standard output to the file.

Pipelines support two or more commands. Variables are stored in the process
environment, so they are visible to children and to `env`. `$?` reports the
previous command status. `&&` and `||` short-circuit; `;` always continues.

### 3.5 Error handling

Unknown commands print a "command not found" message and return 127. Missing
files, invalid options, and bad arguments print a diagnostic and return a
nonzero status. The shell continues running after these errors. Interactive
`SIGINT` is ignored by the shell while it waits for input, but foreground
children restore the default handler so Ctrl+C terminates the running child.

## 4. CPU scheduling

### 4.1 Process model and metrics

Each process record contains a PID, arrival time, burst time, priority, remaining
time, completion time, waiting time, turnaround time, and state. The input format
is:

```text
PID arrival burst priority
```

The scheduler computes:

```text
turnaround = completion - arrival
waiting    = turnaround - burst
average    = sum of the per-process values / number of processes
```

For each run it prints the initial PCB table (every process in the New state),
a Gantt chart, a log of process state transitions, the final PCB table (every
process Terminated), per-process results, and the two averages.

The state log replays the schedule and prints each change of PCB state with
its time:

- **New -> Ready** when the process arrives;
- **Ready -> Running** when it is dispatched to the CPU;
- **Running -> Ready** when it is preempted (quantum expired, or a
  higher-priority queue takes over in MLQ/MLFQ);
- **Running -> Terminated** when its burst completes.

The simulator has no I/O events, so no process enters the **Waiting** state; a
process that is not running waits in the Ready queue. For example, Round-Robin
(q=4) on `basic.txt` begins:

```text
Process state transitions:
  t=0     P1    New      -> Ready      (arrived)
  t=0     P1    Ready    -> Running    (dispatched)
  t=1     P2    New      -> Ready      (arrived)
  t=2     P3    New      -> Ready      (arrived)
  t=3     P4    New      -> Ready      (arrived)
  t=4     P1    Running  -> Ready      (preempted)
  t=4     P2    Ready    -> Running    (dispatched)
  t=8     P2    Running  -> Terminated (burst complete)
```

### 4.2 Algorithms

| Algorithm | Selection rule | Preemption / parameters |
| --- | --- | --- |
| FCFS | Earliest arrival; ties by PID | Non-preemptive |
| SJN | Shortest burst among ready processes | Non-preemptive; ties by arrival, then PID |
| Priority | Smallest priority number | Non-preemptive; ties by arrival, then PID |
| Round-Robin | FIFO ready queue | Quantum 4 by default; `--quantum N` overrides |
| MLQ | Highest nonempty fixed queue | Q0: priority 0-2, q=2; Q1: 3-5, q=4; Q2: 6+, FCFS |
| MLFQ | Highest nonempty feedback queue | q=2/4/8; new processes enter Q0; demote after a full quantum |

MLQ and MLFQ make queue decisions at scheduling points; a process is not
preempted in the middle of a quantum. MLFQ has no priority boost, which keeps the
simulation deterministic and makes the comparison with the other algorithms
reproducible.

### 4.3 Example Gantt charts

For `tests/processes/mixed.txt`:

```text
FCFS: [0-3] P1 | [3-9] P2 | [9-13] P3 | [13-15] P4 | [15-20] P5
SJN:  [0-3] P1 | [3-7] P3 | [7-9] P4 | [9-14] P5 | [14-20] P2
```

SJN moves the short jobs P3 and P4 ahead of the long job P2, which reduces the
average waiting time. FCFS preserves arrival order and therefore pays the convoy
cost of the long P2 burst.

## 5. Deadlock detection and Banker's algorithm

### 5.1 Data structures

The deadlock module stores the number of processes `p`, the number of resource
types `r`, and four matrices/vectors:

- `Allocation[p][r]` — resources currently held;
- `Max[p][r]` — maximum demand of each process;
- `Need[p][r]` — remaining demand, computed as `Max - Allocation`;
- `Available[r]` — currently free resources.

It accepts a labeled format (`processes`, `resources`, `allocation`, `max`,
`available`, optional `request`) and a positional format for compact test files.

### 5.2 Detection

The detection algorithm uses a `Work` vector initialized to `Available` and a
`Finish` array. It repeatedly finds an unfinished process whose `Need` is less
than or equal to `Work`, marks it finished, and releases its allocation into
`Work`. If every process finishes, the state is safe. If some remain unfinished,
those processes are reported as deadlocked and a recovery suggestion is printed.

When the input has a `requests` section, detection uses that Request matrix
instead of `Need`, which is the textbook detection algorithm: a process can
finish if its *current* request fits in `Work`, and a process that holds no
resources is never counted as deadlocked. Without it, the simulator assumes
the worst case (every process requests its full `Need`) and prints a note.

For `tests/deadlock/deadlocked.txt` the output is:

```text
Deadlock detected.
Processes that can finish: (none)
Deadlocked processes: P0 P1
Recovery suggestion: terminate P0 (or preempt its resources).
```

### 5.3 Banker's algorithm and requests

The Banker's algorithm runs the same safety check before committing resources.
For `tests/deadlock/safe.txt` it reports:

```text
State is SAFE.
Safe sequence: P1 -> P3 -> P0 -> P2 -> P4
```

A request is checked in three stages:

1. It must not exceed the process's remaining `Need`.
2. It must not exceed `Available`.
3. Pretending to grant it must leave a safe state.

The simulator grants the request only if all three checks pass. Otherwise it
prints `Request DENIED.` and a reason. For example:

```sh
deadlock banker tests/deadlock/safe.txt --request 1 1 0 2   # granted
deadlock banker tests/deadlock/safe.txt --request 0 7 4 3   # denied
```

### 5.4 Safe and unsafe examples

**Safe example** (`tests/deadlock/safe.txt`, 5 processes, 3 resources,
Available = 3 3 2). P1's Need (1 2 2) fits in Available, so P1 can finish and
release its allocation. That makes room for P3, then P0, P2, and P4:

```text
$ deadlock banker tests/deadlock/safe.txt
State is SAFE.
Safe sequence: P1 -> P3 -> P0 -> P2 -> P4
```

**Unsafe example** (`tests/deadlock/unsafe.txt`, 3 processes, 2 resources):

| Process | Allocation | Max | Need |
| --- | --- | --- | --- |
| P0 | 1 0 | 2 1 | 1 1 |
| P1 | 0 1 | 1 2 | 1 1 |
| P2 | 1 1 | 2 2 | 1 1 |

Available is 0 0. Every process still needs one more unit of each resource,
and nothing is free, so the safety check cannot finish any process:

```text
$ deadlock banker tests/deadlock/unsafe.txt
State is UNSAFE.
No safe sequence exists.
Unfinished processes: P0 P1 P2
Suggestion: reject or roll back an allocation request.
```

An unsafe state is not necessarily a deadlock yet. It means the system can no
longer *guarantee* that every process finishes if each one asks for its full
maximum. The Banker's algorithm exists to avoid ever entering such a state, by
denying any request that would lead to one.

## 6. Comparison of schedulers

### 6.1 Method

All six algorithms were run on the same two workloads:

- `tests/processes/mixed.txt` — five processes with different arrivals, bursts,
  and priorities;
- `tests/processes/basic.txt` — the five-process baseline workload.

The numbers below were produced by `tests/compare.sh`, which parses the average
waiting and turnaround lines from the actual simulator output. No values are
estimated.

### 6.2 Results for `mixed.txt`

| Algorithm | Average waiting | Average turnaround |
| --- | ---: | ---: |
| FCFS | 5.20 | 9.20 |
| SJN | 3.80 | 7.80 |
| Priority | 5.40 | 9.40 |
| Round-Robin (q=4) | 5.60 | 9.60 |
| MLQ | 4.80 | 8.80 |
| MLFQ | 7.40 | 11.40 |

### 6.3 Results for `basic.txt`

| Algorithm | Average waiting | Average turnaround |
| --- | ---: | ---: |
| FCFS | 8.75 | 15.25 |
| SJN | 7.75 | 14.25 |
| Priority | 7.75 | 14.25 |
| Round-Robin (q=4) | 11.75 | 18.25 |
| MLQ | 9.50 | 16.00 |
| MLFQ | 13.00 | 19.50 |

### 6.4 Analysis

**SJN** has the lowest average waiting and turnaround times on the mixed
workload. It deliberately favors short jobs, so the long P2 job is delayed until
the end. This is the classic SJN tradeoff: good averages, but potential starvation
for long jobs.

**FCFS** is simple and predictable, but it suffers from the convoy effect. On the
mixed workload, the long P2 burst immediately follows P1, so later short jobs
wait behind it. On the basic workload, FCFS is also worse than SJN and Priority.

**Priority** performs well when the supplied priority values match the desired
order. In the basic workload it ties SJN because the priority order selects the
same short jobs first. Its weakness is that low-priority processes can starve if
high-priority work keeps arriving.

**Round-Robin** is the fairest of the six for response time, but its average
waiting and turnaround times are higher on these small workloads. The quantum of
4 means that short jobs can be interrupted and later resumed, which adds
completion delay. A smaller quantum would improve response time but increase
context-switch overhead.

**MLQ** performs between FCFS and SJN on both workloads. Its fixed priority
queues give short/high-priority jobs a fast path, while the low-priority queue is
FCFS. The result depends heavily on the priority ranges and quantum sizes.

**MLFQ** has the highest averages in both workloads. New processes start in Q0,
but after using a full quantum they are demoted and must wait behind other
processes. Because there is no priority boost, long-running processes can remain
in lower queues. This is a deliberate policy choice; adding a periodic boost
would improve fairness and reduce starvation at the cost of more complexity.

Overall, no algorithm is best for every metric. SJN optimizes average waiting
time for known burst lengths, FCFS is the simplest, Priority reflects external
importance, Round-Robin improves responsiveness, and MLQ/MLFQ provide a
structure for mixing policies. The project's simulator makes these tradeoffs
visible by reporting both the Gantt chart and the averages.

## 7. Testing and verification

The regression suite is `tests/run_tests.sh` and is run with `make test`. It
currently contains **127 checks**. Coverage includes:

- all required shell built-ins and common error cases;
- quoting, variables, `$?`, `env`, and script mode;
- `>`, `>>`, `<`, `2>`, and `2>&1`, including the ordering of `2>&1`;
- pipelines with built-ins and external programs;
- `grep` substring and `-E` regular-expression modes;
- all six schedulers, custom RR quantum, empty input, single-process input, and
  initial idle time;
- deadlock detection, safe and unsafe Banker states, safe sequences, granted and
  denied requests, file-based requests, and invalid request PIDs.

The test inputs include at least three files per simulator, including edge cases:

- scheduler: `basic.txt`, `mixed.txt`, `edge_single.txt`, `edge_empty.txt`,
  `edge_idle.txt`;
- deadlock: `safe.txt`, `unsafe.txt`, `deadlocked.txt`, `edge_single.txt`,
  `positional.txt`, `request.txt`.

## 8. Division of work

The repository lists two developers. The work was divided by module and then
reviewed together:

| Member | Contributions |
| --- | --- |
| Queypo, Ckhiel Joshua | Shell parser and executor, built-ins, redirection/pipes, process control, integration, README review |
| Viloria, Julian Shaun | Scheduler algorithms, deadlock detection and Banker's algorithm, test fixtures, comparison report, documentation |

Both members participated in testing, debugging, and the final review of the
combined program.

## 9. Conclusion

MiniShell meets the project requirements in a single C codebase. The shell uses
real POSIX process and pipe operations, the scheduler implements six algorithms
with complete PCB/Gantt/average output, and the deadlock module supports both
detection and Banker's algorithm with safe-sequence and request handling. The
127-check regression suite and the measured comparison tables provide evidence
that the implementation works on normal and edge-case inputs. The main
limitations are the intentionally small option parsers, the POSIX-only process
model, and the simplified MLQ/MLFQ preemption rules; these are documented in the
README so the behavior is clear and reproducible.
