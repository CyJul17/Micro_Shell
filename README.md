# MiniShell

MiniShell is the CmpSc 123 final project: a small POSIX-style shell written in C,
plus six CPU scheduling simulators and a deadlock detection / Banker's algorithm
simulator. The whole project is implemented in C and uses only the standard C
library and POSIX system calls; no scheduling or deadlock algorithm library is
used.

Developers:

- Queypo, Ckhiel Joshua
- Viloria, Julian Shaun


## What is included

| Area | Implementation |
| --- | --- |
| Shell | Built-ins, external programs, quoting, variables, redirection, pipes, conditionals |
| CPU scheduling | FCFS, SJN, Priority, Round-Robin, Multilevel Queue, Multilevel Feedback Queue |
| Deadlock | Detection, Banker's safety algorithm, safe sequence, resource requests, recovery suggestion |
| Tests | 126 regression checks and edge-case input files |
| Report | Algorithm comparison with measured waiting/turnaround averages |

## Build

### Linux / macOS

```sh
cd Micro_Shell
make
```

Requirements: a C11 compiler with POSIX `fork`, `exec`, `pipe`, and `dup2`
support (GCC or Clang), plus `make` and `bash` for the tests. If `cc` is not
available, run `make CC=gcc`.

### Windows with MSYS2

Use the **MSYS2 POSIX toolchain**, not the UCRT64 toolchain. UCRT64 GCC does not
provide `fork`/`sys/wait.h`, which the shell needs.

```sh
# In an MSYS2 shell:
pacman -S --needed gcc make
cd /c/path/to/Micro_Shell
make
```

Or invoke MSYS2 from PowerShell/CMD:

```powershell
C:\msys64\usr\bin\bash.exe -lc "cd /c/path/to/Micro_Shell && make"
```

The build produces `minishell.exe` on Windows and `minishell` on POSIX systems.
The VS Code tasks in `.vscode/tasks.json` use
`C:\msys64\usr\bin\make.exe`; update that path if MSYS2 is installed elsewhere.
The debug configuration uses `C:\msys64\ucrt64\bin\gdb.exe`; install the UCRT64
GDB package or adjust `miDebuggerPath` if that file is missing.

### Make targets

```sh
make            # build the shell
make run        # build, then start the interactive shell
make test       # run the regression suite
make compare    # compare all six schedulers on tests/processes/mixed.txt
make clean      # remove build products
make token-demo # optional standalone tokenizer demo (not part of the shell)
```

## Running

```sh
./minishell                              # interactive mode
./minishell -c "pwd; echo hello"         # one command string
./minishell tests/scripts/demo.msh       # script-file mode
```

The interactive prompt is `minishell> `. The shell ignores `SIGINT`/`SIGQUIT`
while waiting for input; foreground children restore the default signal
handlers, so Ctrl+C terminates a running child instead of the shell.

A script file contains the same commands as the interactive shell, one command
sequence per line. Blank lines and lines whose first non-space character is `#`
are ignored. `-c` and script mode return the status of the last command.

## Shell reference

### Built-ins

| Command | Description |
| --- | --- |
| `pwd` | Print the current working directory |
| `cd [dir]` | Change directory; no argument uses `$HOME`; `cd -` uses `$OLDPWD` |
| `ls [-a] [-l] [path ...]` | List files; `-l` shows type, permissions, links, owner, group, size, and time |
| `mkdir [-p] dir ...` | Create directories; `-p` creates parents |
| `rmdir dir ...` | Remove empty directories |
| `touch file ...` | Create files or update timestamps |
| `cp [-r] source destination` | Copy a file or, with `-r`, a directory tree |
| `mv source destination` | Move/rename a file or directory |
| `rm [-r] [-f] path ...` | Remove files or directory trees |
| `cat [file ...]` | Copy files to standard output; no files reads standard input |
| `chmod MODE file ...` | Set numeric permission bits, for example `chmod 640 file` |
| `grep [-i] [-n] [-E] [--] PATTERN [file ...]` | Search text; `-E` enables POSIX extended regular expressions |
| `echo [-n] [text ...]` | Print text; `-n` suppresses the trailing newline |
| `env` | Print the environment |
| `true`, `false` | Return status 0 or 1 |
| `exit [N]` | Exit the shell with status `N & 0xff` |
| `help` | Show the command summary |
| `sched ...` | Run a CPU scheduling simulation (see below) |
| `deadlock ...` | Run a deadlock simulation (see below) |

`ls -l` uses the standard `-rwx` permission notation. Numeric `chmod` is
implemented with the normal POSIX mode bits; Windows filesystems used by MSYS2
may not expose group/other permission bits, so the test suite checks the
portable part of the mode string on Windows and the exact mode on POSIX.

### Redirection

| Syntax | Meaning |
| --- | --- |
| `cmd > file` | Truncate `file` and send standard output there |
| `cmd >> file` | Append standard output to `file` |
| `cmd < file` | Read standard input from `file` |
| `cmd 2> file` | Send standard error to `file` |
| `cmd 2>> file` | Append standard error to `file` |
| `cmd 2>&1` | Send standard error to the current standard output |
| `cmd >&2` or `cmd 1>&2` | Send standard output to the current standard error |

Quoted or escaped operators are ordinary words: `echo "|"` prints `|`.

Redirections are applied left to right, as in a POSIX shell. For example,
`cmd >out.txt 2>&1` sends both streams to `out.txt`, while
`cmd 2>&1 >out.txt` duplicates the original standard output for standard error
and then sends only standard output to the file.

### Pipes

```sh
cat input.txt | grep -i error | sort
```

A pipeline may contain two or more commands. Each command runs in its own child
process, and the exit status of a pipeline is the status of its last command.
Built-ins work in pipelines too.

### Variables

```sh
NAME=Ana
echo $NAME
echo ${NAME}
echo $?
env | grep NAME=
```

- `NAME=value` stores a shell variable and exports it to child processes.
- `$NAME` and `${NAME}` expand a variable.
- `$?` expands to the previous command status.
- A leading assignment before a command, such as `MODE=debug myprog`, is also
  stored in the shell environment; MiniShell keeps assignments persistent for
  simplicity.
- Unquoted expansion is inserted literally as one token; word splitting is not
  performed after expansion.

### Conditional execution

```sh
make && ./minishell
test -f input.txt || echo "input missing"
echo one; echo two
```

- `cmd1 && cmd2` runs `cmd2` only if `cmd1` succeeds (status 0).
- `cmd1 || cmd2` runs `cmd2` only if `cmd1` fails (nonzero).
- `cmd1 ; cmd2` always runs `cmd2`.
- Operators are evaluated left to right. Background jobs (`&` by itself) and
  subshell grouping are not supported.

### Quoting and comments

```sh
echo "hello world"
echo 'single quotes keep $NAME literal'
echo escaped\ space
```

Single quotes preserve everything literally. Double quotes preserve spaces but
still expand `$NAME`, `${NAME}`, and `$?`. A backslash escapes the next
character. A `#` starts a comment only at the beginning of a word; for example,
`echo a#b` prints `a#b`.

### grep

`grep` supports:

- basic substring matching by default;
- `-E` for POSIX extended regular expressions (a bonus over the required
  substring search);
- `-i` for case-insensitive matching;
- `-n` for line numbers;
- multiple file names, with the file name printed before each match;
- `--` to end option parsing.

Examples:

```sh
grep error log.txt
grep -inE "error|warning" log.txt
grep -E '^P[0-9]+' processes.txt
```

### Error handling

Unknown commands print `minishell: <name>: command not found` and return 127.
Missing files, bad options, and invalid arguments print a diagnostic to standard
error and return a nonzero status. The shell itself does not exit or crash on
these errors; it continues with the next command. `exit` and end-of-file are the
normal ways to leave interactive mode.

## CPU scheduling simulator

### Command

```sh
sched <algorithm> [--quantum N] <process-file>
sched help
```

Algorithms:

| Name | Algorithm |
| --- | --- |
| `fcfs` | First-Come First-Served, non-preemptive |
| `sjn` | Shortest Job Next, non-preemptive |
| `priority` | Non-preemptive priority; smaller number = higher priority |
| `rr` | Round-Robin; default quantum 4, override with `--quantum N` |
| `mlq` | Multilevel Queue |
| `mlfq` | Multilevel Feedback Queue |

### Process-file format

```text
# PID arrival burst priority
1 0 3 2
2 0 6 1
3 2 4 5
```

- `PID`, `arrival`, and `priority` are non-negative integers.
- `burst` must be greater than zero.
- `priority` is optional and defaults to 0.
- PIDs must be unique.
- Blank lines and lines beginning with `#` are ignored.
- A process that has not arrived yet is shown as idle time in the Gantt chart.

### Output

For every run the simulator prints:

1. the algorithm name and parameters;
2. the initial PCB table (all processes `New`);
3. a Gantt chart;
4. a timed log of PCB state changes: `New -> Ready` on arrival,
   `Ready -> Running` on dispatch, `Running -> Ready` on preemption, and
   `Running -> Terminated` on completion (there are no I/O events, so no
   process enters `Waiting`);
5. the final PCB table (all processes `Terminated`);
6. per-process completion, waiting, and turnaround times;
7. average waiting time and average turnaround time.

### Algorithm assumptions

- **FCFS** runs processes in arrival order; ties are broken by PID.
- **SJN** selects the ready process with the shortest burst; ties are broken by
  arrival time and then PID.
- **Priority** selects the ready process with the smallest priority number;
  ties are broken by arrival time and then PID.
- **Round-Robin** uses a default quantum of 4 time units. New arrivals are
  enqueued before a process that used a full quantum is placed back in the
  queue.
- **MLQ** uses three fixed queues:
  - Q0: priority 0-2, quantum 2;
  - Q1: priority 3-5, quantum 4;
  - Q2: priority 6+, FCFS.
  Higher queues have strict priority. Queue selection happens at scheduling
  points; a process is not preempted in the middle of its quantum.
- **MLFQ** uses three queues with quantums 2, 4, and 8. New processes enter Q0.
  A process that uses its full quantum is demoted one level (Q2 stays Q2), and a
  process that finishes leaves the system. There is no blocking model and no
  priority boost, so the simulation is deterministic and easy to compare with
  the other algorithms.

### Test process files

| File | Purpose |
| --- | --- |
| `tests/processes/basic.txt` | Five-process baseline workload |
| `tests/processes/mixed.txt` | Mixed arrivals, bursts, and priorities used in the report |
| `tests/processes/edge_single.txt` | One process |
| `tests/processes/edge_empty.txt` | No processes |
| `tests/processes/edge_idle.txt` | Initial idle time before the first arrival |

## Deadlock simulator

### Command

```sh
deadlock detect <system-file>
deadlock banker <system-file> [--request PID r1 r2 ...]
deadlock help
```

- `detect` runs the deadlock detection algorithm and lists processes that can
  finish, processes that are deadlocked, and a recovery suggestion.
- `banker` runs the Banker's safety algorithm, prints the safe sequence when the
  state is safe, and can test one resource request.

### Labeled system-file format

```text
# Comments and blank lines are ignored.
processes 5
resources 3
allocation
0 1 0
2 0 0
3 0 2
2 1 1
0 0 2
max
7 5 3
3 2 2
9 0 2
2 2 2
4 3 3
available
3 3 2

# Optional file-based request: PID followed by one value per resource.
request: 1 1 0 2
```

Section labels are case-insensitive. `Need` is computed as `Max - Allocation`.

For `detect`, an optional `requests` section (one row per process) gives each
process's current outstanding request. Detection then uses that Request matrix,
as in the standard detection algorithm, and a process holding no resources is
never reported as deadlocked. Without a `requests` section, `detect` assumes the
worst case, that every process is requesting its full `Need`, and says so in
its output. See `tests/deadlock/requests_ok.txt` and
`tests/deadlock/requests_deadlock.txt`.
The parser also accepts a positional form without labels:

```text
5 3
0 1 0
2 0 0
3 0 2
2 1 1
0 0 2
7 5 3
3 2 2
9 0 2
2 2 2
4 3 3
3 3 2
```

The first line is `processes resources`; then come `p` allocation rows, `p` max
rows, and one available vector. See `tests/deadlock/positional.txt`.

### Resource requests

A request may come from a file (`request: PID r1 r2 ...`) or from the command
line:

```sh
deadlock banker tests/deadlock/safe.txt --request 1 1 0 2
deadlock banker tests/deadlock/safe.txt --request 0 7 4 3
```

The first request is granted because the resulting state is safe. The second is
denied because it exceeds the available resources. The simulator also denies a
request that exceeds the process's remaining need or that would leave the
system in an unsafe state.

### Test deadlock files

| File | Purpose |
| --- | --- |
| `tests/deadlock/safe.txt` | Classic five-process safe state |
| `tests/deadlock/unsafe.txt` | Unsafe state for the Banker's algorithm |
| `tests/deadlock/deadlocked.txt` | Two-process deadlock cycle |
| `tests/deadlock/edge_single.txt` | Single-process edge case |
| `tests/deadlock/positional.txt` | Positional input format |
| `tests/deadlock/request.txt` | File-based request syntax |

## Tests

```sh
make test
```

The suite currently runs **126 checks** covering:

- shell built-ins, quoting, variables, conditionals, pipes, and redirection;
- `2>` and `2>&1` ordering and behavior;
- `grep`, `chmod`/`ls -l`, error statuses, and script mode;
- all six schedulers, custom RR quantum, empty input, single process, and idle
  time;
- deadlock detection, Banker's safe/unsafe states, safe sequence, granted and
  denied requests, file-based requests, and invalid request PIDs.

Run the comparison table generator separately:

```sh
make compare
# or:
bash tests/compare.sh tests/processes/mixed.txt
bash tests/compare.sh tests/processes/basic.txt
```

## Measured comparison

`tests/processes/mixed.txt`:

| Algorithm | Average waiting | Average turnaround |
| --- | ---: | ---: |
| FCFS | 5.20 | 9.20 |
| SJN | 3.80 | 7.80 |
| Priority | 5.40 | 9.40 |
| Round-Robin (q=4) | 5.60 | 9.60 |
| MLQ | 4.80 | 8.80 |
| MLFQ | 7.40 | 11.40 |

`tests/processes/basic.txt`:

| Algorithm | Average waiting | Average turnaround |
| --- | ---: | ---: |
| FCFS | 8.75 | 15.25 |
| SJN | 7.75 | 14.25 |
| Priority | 7.75 | 14.25 |
| Round-Robin (q=4) | 11.75 | 18.25 |
| MLQ | 9.50 | 16.00 |
| MLFQ | 13.00 | 19.50 |

## Project layout

```text
Micro_Shell/
├── Shell/
│   ├── main.c, main.h          entry point and shared declarations
│   ├── parser.c                lexer, sequence/pipeline/redirection parsing
│   ├── builtin.c               built-ins and grep
│   ├── scheduler.c             six CPU scheduling algorithms
│   ├── deadlock.c              detection, Banker's algorithm, requests
│   ├── utils.c                 vectors, strings, file helpers
│   └── shell_getline.c/.h      line input
├── tests/
│   ├── run_tests.sh            126-check regression suite
│   ├── compare.sh              six-algorithm comparison table
│   ├── processes/              scheduler fixtures
│   ├── deadlock/               deadlock fixtures
│   └── scripts/demo.msh        script-mode demo
├── docs/REPORT.md              written comparison report
├── Token/tokens.c              optional standalone tokenizer demo
├── Makefile
└── README.md
```

## Limitations and assumptions

- The shell is POSIX-oriented and needs `fork`; it is not a native Windows
  `CreateProcess` implementation.
- Background jobs (`&` alone), command substitution, subshells, job control, and
  history are not implemented. `&&`, `||`, `;`, and pipelines are implemented.
- Redirection supports file descriptors 0, 1, and 2, not arbitrary descriptor
  numbers.
- Variable expansion does not perform post-expansion word splitting or globbing.
- `chmod` accepts numeric modes; symbolic modes such as `u+x` are not
  implemented.
- `cp` and `rm` support recursive operation through `-r`; the option parser is
  intentionally small.
- Scheduler priorities use the convention "smaller number = higher priority."
- MLQ and MLFQ make queue decisions at scheduling points rather than preempting
  a process in the middle of its quantum.
- Windows permission bits are limited by the underlying filesystem; the POSIX
  build gives the full behavior.

## Academic integrity note

The implementation was written for the CmpSc 123 final project. It uses only
the C standard library and POSIX APIs; no external scheduler or deadlock library
is used. All test inputs and measured comparison values in this repository were
generated by the project's own programs.
