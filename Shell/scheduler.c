#include "main.h"

/* ======================================================================
 * Scheduler data structures
 * ==================================================================== */

typedef struct {
    int pid;
    int arrival;
    int burst;
    int priority;       /* smaller number = higher priority */
    int remaining;
    int completion;
    int waiting;
    int turnaround;
    int finished;
    int queue;          /* used by MLQ / MLFQ */
} SchedProc;

typedef struct {
    SchedProc *items;
    size_t len;
    size_t cap;
} ProcList;

typedef struct {
    int pid;            /* -1 means idle */
    int start;
    int end;
} GanttSeg;

typedef struct {
    GanttSeg *items;
    size_t len;
    size_t cap;
} Gantt;

typedef struct {
    int *items;
    size_t head;
    size_t tail;
    size_t len;
    size_t cap;
} IntQueue;

typedef enum {
    ALG_FCFS,
    ALG_SJN,
    ALG_PRIORITY,
    ALG_RR,
    ALG_MLQ,
    ALG_MLFQ
} Algorithm;

/* ======================================================================
 * Small helpers
 * ==================================================================== */

static void proc_list_init(ProcList *pl) {
    pl->items = NULL;
    pl->len = 0;
    pl->cap = 0;
}

static void proc_list_push(ProcList *pl, SchedProc p) {
    if (pl->len == pl->cap) {
        pl->cap = pl->cap ? pl->cap * 2 : 16;
        pl->items = xrealloc(pl->items, pl->cap * sizeof(*pl->items));
    }
    pl->items[pl->len++] = p;
}

static void proc_list_free(ProcList *pl) {
    free(pl->items);
    pl->items = NULL;
    pl->len = pl->cap = 0;
}

static void reset_processes(ProcList *pl) {
    for (size_t i = 0; i < pl->len; i++) {
        SchedProc *p = &pl->items[i];
        p->remaining = p->burst;
        p->completion = 0;
        p->waiting = 0;
        p->turnaround = 0;
        p->finished = 0;
        p->queue = 0;
    }
}

static void gantt_init(Gantt *g) {
    g->items = NULL;
    g->len = 0;
    g->cap = 0;
}

static void gantt_add(Gantt *g, int pid, int start, int end) {
    if (end <= start) return;
    if (g->len > 0) {
        GanttSeg *last = &g->items[g->len - 1];
        if (last->pid == pid && last->end == start) {
            last->end = end;
            return;
        }
    }
    if (g->len == g->cap) {
        g->cap = g->cap ? g->cap * 2 : 16;
        g->items = xrealloc(g->items, g->cap * sizeof(*g->items));
    }
    g->items[g->len].pid = pid;
    g->items[g->len].start = start;
    g->items[g->len].end = end;
    g->len++;
}

static void gantt_print(const Gantt *g) {
    printf("\nGantt chart:\n");
    if (g->len == 0) {
        printf("  (empty)\n");
        return;
    }
    for (size_t i = 0; i < g->len; i++) {
        if (g->items[i].pid < 0) {
            printf("[%d-%d] IDLE", g->items[i].start, g->items[i].end);
        } else {
            printf("[%d-%d] P%d", g->items[i].start, g->items[i].end,
                   g->items[i].pid);
        }
        if (i + 1 < g->len) printf(" | ");
    }
    putchar('\n');
}

/* ======================================================================
 * Process state transitions
 *
 * Replays the finished schedule (arrivals + Gantt chart) and prints every
 * PCB state change: New -> Ready on arrival, Ready -> Running when a process
 * gets the CPU, Running -> Ready when it is preempted (quantum expired or a
 * queue switch), and Running -> Terminated when its burst completes.
 * ==================================================================== */

typedef enum {
    ST_NEW,
    ST_READY,
    ST_RUNNING,
    ST_TERMINATED
} ProcState;

static const char *state_name(ProcState s) {
    switch (s) {
        case ST_NEW: return "New";
        case ST_READY: return "Ready";
        case ST_RUNNING: return "Running";
        case ST_TERMINATED: return "Terminated";
    }
    return "?";
}

static int cmp_ints(const void *a, const void *b) {
    int x = *(const int *)a;
    int y = *(const int *)b;
    return (x > y) - (x < y);
}

static int find_proc(const ProcList *pl, int pid) {
    for (size_t i = 0; i < pl->len; i++) {
        if (pl->items[i].pid == pid) return (int)i;
    }
    return -1;
}

static void log_transition(int time, int pid, ProcState *state, ProcState to,
                           const char *why) {
    printf("  t=%-5d P%-4d %-8s -> %-10s %s\n", time, pid,
           state_name(*state), state_name(to), why);
    *state = to;
}

static void print_state_log(const ProcList *pl, const Gantt *g) {
    size_t n = pl->len;
    size_t ntimes = 0;
    int *times = xmalloc((n + 2 * g->len + 1) * sizeof(*times));
    ProcState *state = xmalloc((n ? n : 1) * sizeof(*state));

    for (size_t i = 0; i < n; i++) {
        state[i] = ST_NEW;
        times[ntimes++] = pl->items[i].arrival;
    }
    for (size_t s = 0; s < g->len; s++) {
        times[ntimes++] = g->items[s].start;
        times[ntimes++] = g->items[s].end;
    }
    qsort(times, ntimes, sizeof(*times), cmp_ints);

    printf("\nProcess state transitions:\n");
    for (size_t k = 0; k < ntimes; k++) {
        int t = times[k];
        if (k > 0 && times[k - 1] == t) continue; /* each time once */

        /* 1. The process whose CPU slice ends now leaves Running. */
        for (size_t s = 0; s < g->len; s++) {
            const GanttSeg *seg = &g->items[s];
            if (seg->pid < 0 || seg->end != t) continue;
            int i = find_proc(pl, seg->pid);
            if (i < 0 || state[i] != ST_RUNNING) continue;
            if (pl->items[i].completion == t) {
                log_transition(t, seg->pid, &state[i], ST_TERMINATED,
                               "(burst complete)");
            } else {
                log_transition(t, seg->pid, &state[i], ST_READY,
                               "(preempted)");
            }
        }

        /* 2. Processes arriving now are admitted to the ready queue. */
        for (size_t i = 0; i < n; i++) {
            if (state[i] == ST_NEW && pl->items[i].arrival == t) {
                log_transition(t, pl->items[i].pid, &state[i], ST_READY,
                               "(arrived)");
            }
        }

        /* 3. The process whose CPU slice starts now is dispatched. */
        for (size_t s = 0; s < g->len; s++) {
            const GanttSeg *seg = &g->items[s];
            if (seg->pid < 0 || seg->start != t) continue;
            int i = find_proc(pl, seg->pid);
            if (i < 0 || state[i] != ST_READY) continue;
            log_transition(t, seg->pid, &state[i], ST_RUNNING, "(dispatched)");
        }
    }
    printf("  (No process enters Waiting: the simulator has no I/O events, so a\n"
           "   process that is not running waits in the Ready queue.)\n");

    free(times);
    free(state);
}

static void gantt_free(Gantt *g) {
    free(g->items);
    g->items = NULL;
    g->len = g->cap = 0;
}

static void queue_init(IntQueue *q) {
    q->cap = 8;
    q->items = xmalloc(q->cap * sizeof(*q->items));
    q->head = q->tail = q->len = 0;
}

static void queue_free(IntQueue *q) {
    free(q->items);
    q->items = NULL;
    q->head = q->tail = q->len = q->cap = 0;
}

static void queue_push(IntQueue *q, int value) {
    if (q->len == q->cap) {
        size_t newcap = q->cap * 2;
        int *items = xmalloc(newcap * sizeof(*items));
        for (size_t i = 0; i < q->len; i++) {
            items[i] = q->items[(q->head + i) % q->cap];
        }
        free(q->items);
        q->items = items;
        q->cap = newcap;
        q->head = 0;
        q->tail = q->len;
    }
    q->items[q->tail] = value;
    q->tail = (q->tail + 1) % q->cap;
    q->len++;
}

static int queue_pop(IntQueue *q) {
    int value = q->items[q->head];
    q->head = (q->head + 1) % q->cap;
    q->len--;
    return value;
}

static int queue_empty(const IntQueue *q) {
    return q->len == 0;
}

static int next_arrival(const ProcList *pl, int now) {
    int best = INT_MAX;
    for (size_t i = 0; i < pl->len; i++) {
        const SchedProc *p = &pl->items[i];
        if (!p->finished && p->arrival > now && p->arrival < best) {
            best = p->arrival;
        }
    }
    return best;
}

static void finish_process(SchedProc *p, int now) {
    p->completion = now;
    p->finished = 1;
    p->remaining = 0;
    p->turnaround = p->completion - p->arrival;
    p->waiting = p->turnaround - p->burst;
    if (p->waiting < 0) p->waiting = 0;
}

/* ======================================================================
 * Input
 * ==================================================================== */

static int parse_pid_token(const char *s, int *out) {
    if (!s) return 0;
    if (s[0] == 'P' || s[0] == 'p') s++;
    return parse_int(s, out);
}

static int load_processes(const char *path, ProcList *pl, char **err) {
    FILE *fp = NULL;
    char *line = NULL;
    size_t cap = 0;
    int line_no = 0;

    *err = NULL;
    proc_list_init(pl);

    if (!strcmp(path, "-")) {
        fp = stdin;
    } else {
        fp = fopen(path, "r");
        if (!fp) {
            *err = xstrfmt("%s: %s", path, strerror(errno));
            return -1;
        }
    }

    while (shell_getline(&line, &cap, fp) >= 0) {
        line_no++;
        char *hash = strchr(line, '#');
        if (hash) *hash = '\0';
        char *text = trim(line);
        if (!*text) continue;

        if (!strncasecmp(text, "pid", 3) &&
            (text[3] == '\0' || isspace((unsigned char)text[3]))) {
            continue; /* header row */
        }

        char *save = NULL;
        char *tok = strtok_r(text, " \t\r\n", &save);
        int pid, arrival, burst, priority = 0;

        if (!tok || !parse_pid_token(tok, &pid)) {
            *err = xstrfmt("line %d: expected PID", line_no);
            goto fail;
        }
        tok = strtok_r(NULL, " \t\r\n", &save);
        if (!tok || !parse_int(tok, &arrival)) {
            *err = xstrfmt("line %d: expected arrival time", line_no);
            goto fail;
        }
        tok = strtok_r(NULL, " \t\r\n", &save);
        if (!tok || !parse_int(tok, &burst)) {
            *err = xstrfmt("line %d: expected burst time", line_no);
            goto fail;
        }
        tok = strtok_r(NULL, " \t\r\n", &save);
        if (tok) {
            if (!parse_int(tok, &priority)) {
                *err = xstrfmt("line %d: expected priority", line_no);
                goto fail;
            }
            tok = strtok_r(NULL, " \t\r\n", &save);
            if (tok) {
                *err = xstrfmt("line %d: too many fields", line_no);
                goto fail;
            }
        }

        if (pid < 0 || arrival < 0 || burst <= 0 || priority < 0) {
            *err = xstrfmt("line %d: PID/arrival must be >= 0, burst > 0, priority >= 0",
                           line_no);
            goto fail;
        }
        for (size_t i = 0; i < pl->len; i++) {
            if (pl->items[i].pid == pid) {
                *err = xstrfmt("line %d: duplicate PID %d", line_no, pid);
                goto fail;
            }
        }

        SchedProc p = {
            .pid = pid,
            .arrival = arrival,
            .burst = burst,
            .priority = priority,
            .remaining = burst,
            .completion = 0,
            .waiting = 0,
            .turnaround = 0,
            .finished = 0,
            .queue = 0
        };
        proc_list_push(pl, p);
    }

    free(line);
    if (fp != stdin) fclose(fp);
    return 0;

fail:
    free(line);
    if (fp != stdin) fclose(fp);
    proc_list_free(pl);
    return -1;
}

/* ======================================================================
 * Output
 * ==================================================================== */

static const char *queue_name(int q) {
    if (q == 0) return "Q0";
    if (q == 1) return "Q1";
    return "Q2";
}

static int mlq_level(const SchedProc *p) {
    if (p->priority <= 2) return 0;
    if (p->priority <= 5) return 1;
    return 2;
}

static void print_pcb(const ProcList *pl, int initial, int queue_mode) {
    printf("\nPCB table (%s):\n", initial ? "initial" : "final");
    printf("%-6s %-8s %-6s %-9s %-12s", "PID", "Arrival", "Burst",
           "Priority", "State");
    if (queue_mode) printf(" %-6s", "Queue");
    putchar('\n');

    for (size_t i = 0; i < pl->len; i++) {
        const SchedProc *p = &pl->items[i];
        int q = p->queue;
        if (initial && queue_mode == 1) q = mlq_level(p);
        if (initial && queue_mode == 2) q = 0;
        printf("%-6d %-8d %-6d %-9d %-12s", p->pid, p->arrival, p->burst,
               p->priority, initial ? "New" : "Terminated");
        if (queue_mode) printf(" %-6s", queue_name(q));
        putchar('\n');
    }
}

static void print_results(const ProcList *pl) {
    printf("\nResults:\n");
    printf("%-6s %-8s %-6s %-9s %-11s %-8s %-11s\n",
           "PID", "Arrival", "Burst", "Priority", "Completion", "Waiting",
           "Turnaround");

    double total_wait = 0.0;
    double total_turn = 0.0;
    for (size_t i = 0; i < pl->len; i++) {
        const SchedProc *p = &pl->items[i];
        printf("%-6d %-8d %-6d %-9d %-11d %-8d %-11d\n",
               p->pid, p->arrival, p->burst, p->priority, p->completion,
               p->waiting, p->turnaround);
        total_wait += p->waiting;
        total_turn += p->turnaround;
    }

    if (pl->len > 0) {
        printf("\nAverage waiting time:    %.2f\n", total_wait / (double)pl->len);
        printf("Average turnaround time: %.2f\n", total_turn / (double)pl->len);
    }
}

/* ======================================================================
 * Scheduling algorithms
 * ==================================================================== */

static void sim_fcfs(ProcList *pl, Gantt *g) {
    size_t n = pl->len;
    int *order = xmalloc((n ? n : 1) * sizeof(*order));
    for (size_t i = 0; i < n; i++) order[i] = (int)i;

    /* Selection sort: arrival, then PID. */
    for (size_t i = 0; i < n; i++) {
        for (size_t j = i + 1; j < n; j++) {
            SchedProc *a = &pl->items[order[i]];
            SchedProc *b = &pl->items[order[j]];
            if (b->arrival < a->arrival ||
                (b->arrival == a->arrival && b->pid < a->pid)) {
                int tmp = order[i];
                order[i] = order[j];
                order[j] = tmp;
            }
        }
    }

    int now = 0;
    for (size_t i = 0; i < n; i++) {
        SchedProc *p = &pl->items[order[i]];
        if (now < p->arrival) {
            gantt_add(g, -1, now, p->arrival);
            now = p->arrival;
        }
        int start = now;
        now += p->burst;
        gantt_add(g, p->pid, start, now);
        finish_process(p, now);
    }
    free(order);
}

static void sim_sjn(ProcList *pl, Gantt *g) {
    int now = 0;
    size_t done = 0;

    while (done < pl->len) {
        int best = -1;
        for (size_t i = 0; i < pl->len; i++) {
            SchedProc *p = &pl->items[i];
            if (p->finished || p->arrival > now) continue;
            if (best < 0) {
                best = (int)i;
                continue;
            }
            SchedProc *b = &pl->items[best];
            if (p->burst < b->burst ||
                (p->burst == b->burst && p->arrival < b->arrival) ||
                (p->burst == b->burst && p->arrival == b->arrival &&
                 p->pid < b->pid)) {
                best = (int)i;
            }
        }

        if (best < 0) {
            int next = next_arrival(pl, now);
            if (next == INT_MAX) break;
            gantt_add(g, -1, now, next);
            now = next;
            continue;
        }

        SchedProc *p = &pl->items[best];
        int start = now;
        now += p->burst;
        gantt_add(g, p->pid, start, now);
        finish_process(p, now);
        done++;
    }
}

static void sim_priority(ProcList *pl, Gantt *g) {
    int now = 0;
    size_t done = 0;

    while (done < pl->len) {
        int best = -1;
        for (size_t i = 0; i < pl->len; i++) {
            SchedProc *p = &pl->items[i];
            if (p->finished || p->arrival > now) continue;
            if (best < 0) {
                best = (int)i;
                continue;
            }
            SchedProc *b = &pl->items[best];
            if (p->priority < b->priority ||
                (p->priority == b->priority && p->arrival < b->arrival) ||
                (p->priority == b->priority && p->arrival == b->arrival &&
                 p->pid < b->pid)) {
                best = (int)i;
            }
        }

        if (best < 0) {
            int next = next_arrival(pl, now);
            if (next == INT_MAX) break;
            gantt_add(g, -1, now, next);
            now = next;
            continue;
        }

        SchedProc *p = &pl->items[best];
        int start = now;
        now += p->burst;
        gantt_add(g, p->pid, start, now);
        finish_process(p, now);
        done++;
    }
}

static void sim_rr(ProcList *pl, int quantum, Gantt *g) {
    size_t n = pl->len;
    if (n == 0) return;

    unsigned char *enqueued = calloc(n, 1);
    if (!enqueued) {
        fprintf(stderr, "minishell: out of memory\n");
        exit(EXIT_FAILURE);
    }
    IntQueue q;
    queue_init(&q);

    int now = 0;
    size_t done = 0;

    while (done < n) {
        for (size_t i = 0; i < n; i++) {
            SchedProc *p = &pl->items[i];
            if (!enqueued[i] && !p->finished && p->arrival <= now) {
                queue_push(&q, (int)i);
                enqueued[i] = 1;
            }
        }

        if (queue_empty(&q)) {
            int next = next_arrival(pl, now);
            if (next == INT_MAX) break;
            gantt_add(g, -1, now, next);
            now = next;
            continue;
        }

        int idx = queue_pop(&q);
        enqueued[idx] = 1; /* keep it marked while it is running */
        SchedProc *p = &pl->items[idx];
        int slice = quantum < p->remaining ? quantum : p->remaining;
        int start = now;
        now += slice;
        p->remaining -= slice;
        gantt_add(g, p->pid, start, now);

        for (size_t i = 0; i < n; i++) {
            SchedProc *other = &pl->items[i];
            if (!enqueued[i] && !other->finished && other->arrival <= now) {
                queue_push(&q, (int)i);
                enqueued[i] = 1;
            }
        }

        if (p->remaining == 0) {
            finish_process(p, now);
            done++;
        } else {
            queue_push(&q, idx);
            enqueued[idx] = 1;
        }
    }

    queue_free(&q);
    free(enqueued);
}

static void sim_mlq(ProcList *pl, Gantt *g) {
    size_t n = pl->len;
    if (n == 0) return;

    unsigned char *enqueued = calloc(n, 1);
    if (!enqueued) {
        fprintf(stderr, "minishell: out of memory\n");
        exit(EXIT_FAILURE);
    }
    IntQueue q[3];
    for (int i = 0; i < 3; i++) queue_init(&q[i]);

    int now = 0;
    size_t done = 0;

    while (done < n) {
        for (size_t i = 0; i < n; i++) {
            SchedProc *p = &pl->items[i];
            if (!enqueued[i] && !p->finished && p->arrival <= now) {
                int level = mlq_level(p);
                p->queue = level;
                queue_push(&q[level], (int)i);
                enqueued[i] = 1;
            }
        }

        int level = -1;
        for (int l = 0; l < 3; l++) {
            if (!queue_empty(&q[l])) {
                level = l;
                break;
            }
        }
        if (level < 0) {
            int next = next_arrival(pl, now);
            if (next == INT_MAX) break;
            gantt_add(g, -1, now, next);
            now = next;
            continue;
        }

        int idx = queue_pop(&q[level]);
        enqueued[idx] = 1; /* keep it marked while it is running */
        SchedProc *p = &pl->items[idx];
        p->queue = level;

        int slice;
        if (level == 2) {
            slice = p->remaining; /* low-priority queue is FCFS */
        } else {
            int quantum = level == 0 ? 2 : 4;
            slice = quantum < p->remaining ? quantum : p->remaining;
        }
        int start = now;
        now += slice;
        p->remaining -= slice;
        gantt_add(g, p->pid, start, now);

        for (size_t i = 0; i < n; i++) {
            SchedProc *other = &pl->items[i];
            if (!enqueued[i] && !other->finished && other->arrival <= now) {
                int l = mlq_level(other);
                other->queue = l;
                queue_push(&q[l], (int)i);
                enqueued[i] = 1;
            }
        }

        if (p->remaining == 0) {
            finish_process(p, now);
            done++;
        } else {
            queue_push(&q[level], idx);
            enqueued[idx] = 1;
        }
    }

    for (int i = 0; i < 3; i++) queue_free(&q[i]);
    free(enqueued);
}

static void sim_mlfq(ProcList *pl, Gantt *g) {
    size_t n = pl->len;
    if (n == 0) return;

    unsigned char *enqueued = calloc(n, 1);
    if (!enqueued) {
        fprintf(stderr, "minishell: out of memory\n");
        exit(EXIT_FAILURE);
    }
    IntQueue q[3];
    for (int i = 0; i < 3; i++) queue_init(&q[i]);

    int now = 0;
    size_t done = 0;

    while (done < n) {
        for (size_t i = 0; i < n; i++) {
            SchedProc *p = &pl->items[i];
            if (!enqueued[i] && !p->finished && p->arrival <= now) {
                p->queue = 0;
                queue_push(&q[0], (int)i);
                enqueued[i] = 1;
            }
        }

        int level = -1;
        for (int l = 0; l < 3; l++) {
            if (!queue_empty(&q[l])) {
                level = l;
                break;
            }
        }
        if (level < 0) {
            int next = next_arrival(pl, now);
            if (next == INT_MAX) break;
            gantt_add(g, -1, now, next);
            now = next;
            continue;
        }

        int idx = queue_pop(&q[level]);
        enqueued[idx] = 1; /* keep it marked while it is running */
        SchedProc *p = &pl->items[idx];
        p->queue = level;

        int quantum = level == 0 ? 2 : (level == 1 ? 4 : 8);
        int slice = quantum < p->remaining ? quantum : p->remaining;
        int start = now;
        now += slice;
        p->remaining -= slice;
        gantt_add(g, p->pid, start, now);

        for (size_t i = 0; i < n; i++) {
            SchedProc *other = &pl->items[i];
            if (!enqueued[i] && !other->finished && other->arrival <= now) {
                other->queue = 0;
                queue_push(&q[0], (int)i);
                enqueued[i] = 1;
            }
        }

        if (p->remaining == 0) {
            finish_process(p, now);
            done++;
        } else {
            if (level < 2) level++;
            p->queue = level;
            queue_push(&q[level], idx);
            enqueued[idx] = 1;
        }
    }

    for (int i = 0; i < 3; i++) queue_free(&q[i]);
    free(enqueued);
}

/* ======================================================================
 * Command entry point
 * ==================================================================== */

static void sched_usage(void) {
    printf("Usage:\n");
    printf("  sched <algorithm> [--quantum N] <processes.txt>\n");
    printf("  sched help\n\n");
    printf("Algorithms:\n");
    printf("  fcfs      First-Come, First-Served\n");
    printf("  sjn       Shortest Job Next (also: sjf)\n");
    printf("  priority  Non-preemptive priority (lower number = higher)\n");
    printf("  rr        Round-Robin (default quantum 4)\n");
    printf("  mlq       Multilevel Queue (Q0 priority 0-2, Q1 3-5, Q2 6+)\n");
    printf("  mlfq      Multilevel Feedback Queue (quantum 2, 4, 8)\n\n");
    printf("Process file format (one process per line):\n");
    printf("  PID arrival burst priority\n");
    printf("Example:\n");
    printf("  1 0 8 3\n");
}

int sched_main(int argc, char **argv) {
    if (argc == 2 && !strcasecmp(argv[1], "help")) {
        sched_usage();
        return 0;
    }

    const char *algo_name = NULL;
    const char *file = NULL;
    int quantum = 4;
    int quantum_given = 0;

    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--quantum")) {
            quantum_given = 1;
            if (i + 1 >= argc || !parse_int(argv[++i], &quantum)) {
                print_error("sched: --quantum requires a positive integer");
                return 2;
            }
        } else if (argv[i][0] == '-' && argv[i][1]) {
            print_error("sched: unknown option '%s'", argv[i]);
            return 2;
        } else if (!algo_name) {
            algo_name = argv[i];
        } else if (!file) {
            file = argv[i];
        } else {
            print_error("sched: too many arguments");
            return 2;
        }
    }

    if (!algo_name || !file) {
        sched_usage();
        return 2;
    }
    if (quantum <= 0) {
        print_error("sched: quantum must be greater than zero");
        return 2;
    }

    Algorithm alg;
    const char *display;
    int queue_mode = 0;

    if (!strcasecmp(algo_name, "fcfs")) {
        alg = ALG_FCFS;
        display = "FCFS";
    } else if (!strcasecmp(algo_name, "sjn") || !strcasecmp(algo_name, "sjf")) {
        alg = ALG_SJN;
        display = "SJN";
    } else if (!strcasecmp(algo_name, "priority") ||
               !strcasecmp(algo_name, "prio")) {
        alg = ALG_PRIORITY;
        display = "Priority";
    } else if (!strcasecmp(algo_name, "rr")) {
        alg = ALG_RR;
        display = "Round-Robin";
    } else if (!strcasecmp(algo_name, "mlq")) {
        alg = ALG_MLQ;
        display = "Multilevel Queue";
        queue_mode = 1;
    } else if (!strcasecmp(algo_name, "mlfq")) {
        alg = ALG_MLFQ;
        display = "Multilevel Feedback Queue";
        queue_mode = 2;
    } else {
        print_error("sched: unknown algorithm '%s'", algo_name);
        sched_usage();
        return 2;
    }
    if (quantum_given && alg != ALG_RR) {
        /* MLQ/MLFQ use fixed per-queue quanta; the others are not time-sliced. */
        print_error("sched: --quantum only applies to rr");
        return 2;
    }

    ProcList pl;
    char *err = NULL;
    if (load_processes(file, &pl, &err) < 0) {
        print_error("sched: %s", err ? err : "could not load processes");
        free(err);
        return 1;
    }

    printf("Scheduling algorithm: %s\n", display);
    if (alg == ALG_RR) printf("Quantum: %d\n", quantum);

    if (pl.len == 0) {
        printf("No processes loaded.\n");
        proc_list_free(&pl);
        return 0;
    }

    print_pcb(&pl, 1, queue_mode);
    reset_processes(&pl);

    Gantt g;
    gantt_init(&g);
    switch (alg) {
        case ALG_FCFS: sim_fcfs(&pl, &g); break;
        case ALG_SJN: sim_sjn(&pl, &g); break;
        case ALG_PRIORITY: sim_priority(&pl, &g); break;
        case ALG_RR: sim_rr(&pl, quantum, &g); break;
        case ALG_MLQ: sim_mlq(&pl, &g); break;
        case ALG_MLFQ: sim_mlfq(&pl, &g); break;
    }

    gantt_print(&g);
    print_state_log(&pl, &g);
    print_pcb(&pl, 0, queue_mode);
    print_results(&pl);

    gantt_free(&g);
    proc_list_free(&pl);
    return 0;
}
