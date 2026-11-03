#include "main.h"

/* ======================================================================
 * Deadlock simulator data
 * ==================================================================== */

typedef struct {
    int p;
    int r;
    int **alloc;
    int **max;
    int **need;
    int *avail;
    int has_request;
    int req_pid;
    int *request;
} DLSystem;

typedef enum {
    SEC_NONE,
    SEC_ALLOC,
    SEC_MAX,
    SEC_AVAIL
} Section;

static void dl_free(DLSystem *sys) {
    if (!sys) return;
    if (sys->alloc) {
        for (int i = 0; i < sys->p; i++) free(sys->alloc[i]);
        free(sys->alloc);
    }
    if (sys->max) {
        for (int i = 0; i < sys->p; i++) free(sys->max[i]);
        free(sys->max);
    }
    if (sys->need) {
        for (int i = 0; i < sys->p; i++) free(sys->need[i]);
        free(sys->need);
    }
    free(sys->avail);
    free(sys->request);
    memset(sys, 0, sizeof(*sys));
}

static int **matrix_alloc(int rows, int cols) {
    if (rows <= 0) return NULL;
    int **m = xmalloc((size_t)rows * sizeof(*m));
    for (int i = 0; i < rows; i++) {
        m[i] = xmalloc((size_t)(cols > 0 ? cols : 1) * sizeof(**m));
        for (int j = 0; j < cols; j++) m[i][j] = 0;
    }
    return m;
}

static int ensure_matrices(DLSystem *sys) {
    if (sys->p < 0 || sys->r < 1) return 0;
    if (sys->alloc) return 0;
    sys->alloc = matrix_alloc(sys->p, sys->r);
    sys->max = matrix_alloc(sys->p, sys->r);
    sys->need = matrix_alloc(sys->p, sys->r);
    sys->avail = xmalloc((size_t)sys->r * sizeof(*sys->avail));
    for (int j = 0; j < sys->r; j++) sys->avail[j] = 0;
    return 0;
}

static int starts_word(const char *line, const char *word) {
    size_t n = strlen(word);
    if (strncasecmp(line, word, n) != 0) return 0;
    return line[n] == '\0' || isspace((unsigned char)line[n]) || line[n] == ':';
}

static int parse_after_word(const char *line, const char *word, int *out) {
    const char *p = line + strlen(word);
    while (*p && (isspace((unsigned char)*p) || *p == ':')) p++;
    char *copy = xstrdup(p);
    char *save = NULL;
    char *tok = strtok_r(copy, " \t\r\n", &save);
    int ok = tok && parse_int(tok, out);
    free(copy);
    return ok;
}

static int parse_ints(const char *line, int *values, int max, int *count) {
    char *copy = xstrdup(line);
    char *save = NULL;
    char *tok = strtok_r(copy, " \t\r\n", &save);
    *count = 0;
    while (tok) {
        if (*count >= max) {
            free(copy);
            return -1;
        }
        int value;
        if (!parse_int(tok, &value)) {
            free(copy);
            return -1;
        }
        values[(*count)++] = value;
        tok = strtok_r(NULL, " \t\r\n", &save);
    }
    free(copy);
    return 0;
}

/* ======================================================================
 * Input parsing
 *
 * Supported forms:
 *
 *   processes 5          or     5 3
 *   resources 3
 *   allocation
 *   0 1 0
 *   ...
 *   max
 *   7 5 3
 *   ...
 *   available
 *   3 3 2
 *   request 1 1 0 2     (optional)
 *
 * The positional form also works: first line "P R", then P allocation
 * rows, P max rows, and one available vector.
 * ==================================================================== */

static int load_system(const char *path, DLSystem *sys, char **err) {
    FILE *fp = NULL;
    char *line = NULL;
    size_t cap = 0;
    int line_no = 0;
    int p = -1;
    int r = -1;
    int alloc_rows = 0;
    int max_rows = 0;
    int avail_count = 0;
    Section section = SEC_NONE;
    int values[512];

    memset(sys, 0, sizeof(*sys));
    sys->p = -1;
    sys->r = -1;
    *err = NULL;

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

        if (starts_word(text, "processes")) {
            if (!parse_after_word(text, "processes", &p) || p < 0) {
                *err = xstrfmt("line %d: invalid process count", line_no);
                goto fail;
            }
            sys->p = p;
            if (r >= 0 && ensure_matrices(sys) < 0) goto oom;
            continue;
        }
        if (starts_word(text, "resources")) {
            if (!parse_after_word(text, "resources", &r) || r < 1) {
                *err = xstrfmt("line %d: invalid resource count", line_no);
                goto fail;
            }
            sys->r = r;
            if (p >= 0 && ensure_matrices(sys) < 0) goto oom;
            continue;
        }
        if (starts_word(text, "allocation")) {
            section = SEC_ALLOC;
            continue;
        }
        if (starts_word(text, "max")) {
            section = SEC_MAX;
            continue;
        }
        if (starts_word(text, "available")) {
            section = SEC_AVAIL;
            continue;
        }
        if (starts_word(text, "request")) {
            if (r < 1) {
                *err = xstrfmt("line %d: resources must be declared before request",
                               line_no);
                goto fail;
            }
            const char *rest = text + strlen("request");
            while (*rest && (isspace((unsigned char)*rest) || *rest == ':')) rest++;
            int count = 0;
            if (parse_ints(rest, values, 512, &count) < 0 || count != r + 1) {
                *err = xstrfmt("line %d: request needs PID and %d values",
                               line_no, r);
                goto fail;
            }
            free(sys->request);
            sys->request = xmalloc((size_t)r * sizeof(*sys->request));
            sys->req_pid = values[0];
            for (int j = 0; j < r; j++) {
                if (values[j + 1] < 0) {
                    *err = xstrfmt("line %d: negative resource request", line_no);
                    goto fail;
                }
                sys->request[j] = values[j + 1];
            }
            sys->has_request = 1;
            continue;
        }

        int count = 0;
        if (parse_ints(text, values, 512, &count) < 0 || count == 0) {
            *err = xstrfmt("line %d: expected numbers or a section label", line_no);
            goto fail;
        }

        if (p < 0 || r < 0) {
            if (count != 2) {
                *err = xstrfmt("line %d: first data line must be 'processes resources'",
                               line_no);
                goto fail;
            }
            if (p >= 0 && r < 0) {
                *err = xstrfmt("line %d: missing 'resources' count", line_no);
                goto fail;
            }
            if (r >= 0 && p < 0) {
                *err = xstrfmt("line %d: missing 'processes' count", line_no);
                goto fail;
            }
            p = values[0];
            r = values[1];
            if (p < 0 || r < 1) {
                *err = xstrfmt("line %d: invalid process/resource count", line_no);
                goto fail;
            }
            sys->p = p;
            sys->r = r;
            if (ensure_matrices(sys) < 0) goto oom;
            section = (p == 0) ? SEC_AVAIL : SEC_ALLOC;
            continue;
        }

        if (section == SEC_NONE) {
            if (alloc_rows < p) section = SEC_ALLOC;
            else if (max_rows < p) section = SEC_MAX;
            else if (avail_count < r) section = SEC_AVAIL;
            else {
                *err = xstrfmt("line %d: unexpected extra data", line_no);
                goto fail;
            }
        }

        if (section == SEC_ALLOC) {
            if (alloc_rows >= p || count != r) {
                *err = xstrfmt("line %d: allocation row needs %d values", line_no, r);
                goto fail;
            }
            for (int j = 0; j < r; j++) {
                if (values[j] < 0) {
                    *err = xstrfmt("line %d: negative allocation", line_no);
                    goto fail;
                }
                sys->alloc[alloc_rows][j] = values[j];
            }
            alloc_rows++;
            if (alloc_rows == p) section = SEC_MAX;
        } else if (section == SEC_MAX) {
            if (max_rows >= p || count != r) {
                *err = xstrfmt("line %d: max row needs %d values", line_no, r);
                goto fail;
            }
            for (int j = 0; j < r; j++) {
                if (values[j] < 0) {
                    *err = xstrfmt("line %d: negative maximum", line_no);
                    goto fail;
                }
                sys->max[max_rows][j] = values[j];
            }
            max_rows++;
            if (max_rows == p) section = SEC_AVAIL;
        } else if (section == SEC_AVAIL) {
            if (avail_count >= r || count != r) {
                *err = xstrfmt("line %d: available vector needs %d values", line_no, r);
                goto fail;
            }
            for (int j = 0; j < r; j++) {
                if (values[j] < 0) {
                    *err = xstrfmt("line %d: negative available resource", line_no);
                    goto fail;
                }
                sys->avail[j] = values[j];
            }
            avail_count = r;
            section = SEC_NONE;
        }
    }

    if (p < 0 || r < 1) {
        *err = xstrfmt("missing process/resource counts");
        goto fail;
    }
    if (alloc_rows != p) {
        *err = xstrfmt("expected %d allocation rows, found %d", p, alloc_rows);
        goto fail;
    }
    if (max_rows != p) {
        *err = xstrfmt("expected %d max rows, found %d", p, max_rows);
        goto fail;
    }
    if (avail_count != r) {
        *err = xstrfmt("expected %d available values, found %d", r, avail_count);
        goto fail;
    }

    for (int i = 0; i < p; i++) {
        for (int j = 0; j < r; j++) {
            if (sys->max[i][j] < sys->alloc[i][j]) {
                *err = xstrfmt("P%d resource %d: max is less than allocation",
                               i, j);
                goto fail;
            }
            sys->need[i][j] = sys->max[i][j] - sys->alloc[i][j];
        }
    }

    free(line);
    if (fp != stdin) fclose(fp);
    return 0;

oom:
    *err = xstrdup("out of memory");
fail:
    free(line);
    if (fp != stdin) fclose(fp);
    dl_free(sys);
    return -1;
}

/* ======================================================================
 * Output
 * ==================================================================== */

static void print_vector(const char *name, const int *v, int n) {
    printf("%s: ", name);
    for (int i = 0; i < n; i++) printf("%d ", v[i]);
    putchar('\n');
}

static void print_matrix(const char *name, int **m, int rows, int cols) {
    printf("%s:\n", name);
    for (int i = 0; i < rows; i++) {
        printf("  P%-3d", i);
        for (int j = 0; j < cols; j++) printf(" %4d", m[i][j]);
        putchar('\n');
    }
}

static void print_system(const DLSystem *sys) {
    printf("Processes: %d, Resources: %d\n", sys->p, sys->r);
    print_vector("Available", sys->avail, sys->r);
    print_matrix("Allocation", sys->alloc, sys->p, sys->r);
    print_matrix("Max", sys->max, sys->p, sys->r);
    print_matrix("Need", sys->need, sys->p, sys->r);
}

static void print_sequence(const int *seq, int len) {
    if (len == 0) {
        printf("(none)\n");
        return;
    }
    for (int i = 0; i < len; i++) {
        printf("P%d", seq[i]);
        if (i + 1 < len) printf(" -> ");
    }
    putchar('\n');
}

/* ======================================================================
 * Safety / detection
 * ==================================================================== */

static int safety_check(DLSystem *sys, int *seq, int *seq_len,
                        unsigned char *finish) {
    int *work = xmalloc((size_t)sys->r * sizeof(*work));
    for (int j = 0; j < sys->r; j++) work[j] = sys->avail[j];
    for (int i = 0; i < sys->p; i++) finish[i] = 0;
    *seq_len = 0;

    while (*seq_len < sys->p) {
        int found = -1;
        for (int i = 0; i < sys->p; i++) {
            if (finish[i]) continue;
            int can_finish = 1;
            for (int j = 0; j < sys->r; j++) {
                if (sys->need[i][j] > work[j]) {
                    can_finish = 0;
                    break;
                }
            }
            if (can_finish) {
                found = i;
                break;
            }
        }
        if (found < 0) break;

        for (int j = 0; j < sys->r; j++) work[j] += sys->alloc[found][j];
        finish[found] = 1;
        seq[(*seq_len)++] = found;
    }

    free(work);
    return *seq_len == sys->p;
}

static void run_detection(DLSystem *sys) {
    int *seq = xmalloc((size_t)(sys->p > 0 ? sys->p : 1) * sizeof(*seq));
    unsigned char *finish = xmalloc((size_t)(sys->p > 0 ? sys->p : 1));
    int seq_len = 0;
    int safe = safety_check(sys, seq, &seq_len, finish);

    printf("\nDeadlock detection:\n");
    if (safe) {
        printf("No deadlock detected. All processes can finish.\n");
        printf("Possible completion order: ");
        print_sequence(seq, seq_len);
    } else {
        printf("Deadlock detected.\n");
        printf("Processes that can finish: ");
        print_sequence(seq, seq_len);
        printf("Deadlocked processes: ");
        int first = 1;
        for (int i = 0; i < sys->p; i++) {
            if (!finish[i]) {
                printf("%sP%d", first ? "" : " ", i);
                first = 0;
            }
        }
        putchar('\n');
        for (int i = 0; i < sys->p; i++) {
            if (!finish[i]) {
                printf("Recovery suggestion: terminate P%d (or preempt its resources).\n",
                       i);
                break;
            }
        }
    }

    free(seq);
    free(finish);
}

static void run_banker(DLSystem *sys) {
    int *seq = xmalloc((size_t)(sys->p > 0 ? sys->p : 1) * sizeof(*seq));
    unsigned char *finish = xmalloc((size_t)(sys->p > 0 ? sys->p : 1));
    int seq_len = 0;
    int safe = safety_check(sys, seq, &seq_len, finish);

    printf("\nBanker's algorithm:\n");
    if (safe) {
        printf("State is SAFE.\n");
        printf("Safe sequence: ");
        print_sequence(seq, seq_len);
    } else {
        printf("State is UNSAFE.\n");
        printf("No safe sequence exists.\n");
        printf("Unfinished processes: ");
        int first = 1;
        for (int i = 0; i < sys->p; i++) {
            if (!finish[i]) {
                printf("%sP%d", first ? "" : " ", i);
                first = 0;
            }
        }
        putchar('\n');
        printf("Suggestion: reject or roll back an allocation request.\n");
    }

    free(seq);
    free(finish);
}

static int run_request(DLSystem *sys) {
    if (!sys->has_request) return 0;
    if (sys->req_pid < 0 || sys->req_pid >= sys->p) {
        print_error("deadlock: request PID P%d is out of range", sys->req_pid);
        return 2;
    }
    if (!sys->request) return 0;

    int pid = sys->req_pid;
    printf("\nResource request: P%d requests", pid);
    for (int j = 0; j < sys->r; j++) printf(" %d", sys->request[j]);
    putchar('\n');

    for (int j = 0; j < sys->r; j++) {
        if (sys->request[j] > sys->need[pid][j]) {
            printf("Request DENIED.\n");
            printf("Reason: P%d requests more than its remaining need.\n", pid);
            return 0;
        }
    }
    for (int j = 0; j < sys->r; j++) {
        if (sys->request[j] > sys->avail[j]) {
            printf("Request DENIED.\n");
            printf("Reason: not enough available resources.\n");
            return 0;
        }
    }

    int *old_avail = xmalloc((size_t)sys->r * sizeof(*old_avail));
    int *old_alloc = xmalloc((size_t)sys->r * sizeof(*old_alloc));
    int *old_need = xmalloc((size_t)sys->r * sizeof(*old_need));
    for (int j = 0; j < sys->r; j++) {
        old_avail[j] = sys->avail[j];
        old_alloc[j] = sys->alloc[pid][j];
        old_need[j] = sys->need[pid][j];
        sys->avail[j] -= sys->request[j];
        sys->alloc[pid][j] += sys->request[j];
        sys->need[pid][j] -= sys->request[j];
    }

    int *seq = xmalloc((size_t)(sys->p > 0 ? sys->p : 1) * sizeof(*seq));
    unsigned char *finish = xmalloc((size_t)(sys->p > 0 ? sys->p : 1));
    int seq_len = 0;
    int safe = safety_check(sys, seq, &seq_len, finish);

    if (safe) {
        printf("Request GRANTED. State remains safe.\n");
        printf("Safe sequence after grant: ");
        print_sequence(seq, seq_len);
        print_vector("New Available", sys->avail, sys->r);
        print_matrix("New Allocation", sys->alloc, sys->p, sys->r);
        print_matrix("New Need", sys->need, sys->p, sys->r);
    } else {
        for (int j = 0; j < sys->r; j++) {
            sys->avail[j] = old_avail[j];
            sys->alloc[pid][j] = old_alloc[j];
            sys->need[pid][j] = old_need[j];
        }
        printf("Request DENIED.\n");
        printf("Reason: granting it would leave the system unsafe.\n");
    }

    free(seq);
    free(finish);
    free(old_avail);
    free(old_alloc);
    free(old_need);
    return 0;
}

/* ======================================================================
 * Command entry point
 * ==================================================================== */

static void deadlock_usage(void) {
    printf("Usage:\n");
    printf("  deadlock detect <system.txt>\n");
    printf("  deadlock banker <system.txt> [--request PID r1 r2 ...]\n");
    printf("  deadlock help\n\n");
    printf("System file format:\n");
    printf("  processes 5\n");
    printf("  resources 3\n");
    printf("  allocation\n");
    printf("  0 1 0\n");
    printf("  ...\n");
    printf("  max\n");
    printf("  7 5 3\n");
    printf("  ...\n");
    printf("  available\n");
    printf("  3 3 2\n");
    printf("  request 1 1 0 2   (optional, for banker)\n");
}

int deadlock_main(int argc, char **argv) {
    if (argc == 2 && !strcasecmp(argv[1], "help")) {
        deadlock_usage();
        return 0;
    }
    if (argc < 3) {
        deadlock_usage();
        return 2;
    }

    const char *mode = argv[1];
    const char *file = argv[2];
    int detect;

    if (!strcasecmp(mode, "detect")) {
        detect = 1;
    } else if (!strcasecmp(mode, "banker")) {
        detect = 0;
    } else {
        print_error("deadlock: unknown mode '%s'", mode);
        deadlock_usage();
        return 2;
    }

    DLSystem sys;
    char *err = NULL;
    if (load_system(file, &sys, &err) < 0) {
        print_error("deadlock: %s", err ? err : "could not load system");
        free(err);
        return 1;
    }

    print_system(&sys);
    int result = 0;

    if (detect) {
        run_detection(&sys);
    } else {
        run_banker(&sys);

        int request_index = 3;
        if (request_index < argc) {
            if (strcmp(argv[request_index], "--request") != 0) {
                print_error("deadlock: unknown option '%s'", argv[request_index]);
                dl_free(&sys);
                return 2;
            }
            request_index++;
            if (request_index >= argc || sys.r < 1) {
                print_error("deadlock: --request needs a PID and resource values");
                dl_free(&sys);
                return 2;
            }
            int pid;
            if (!parse_int(argv[request_index++], &pid)) {
                print_error("deadlock: invalid request PID");
                dl_free(&sys);
                return 2;
            }
            int count = argc - request_index;
            if (count != sys.r) {
                print_error("deadlock: request needs %d resource values", sys.r);
                dl_free(&sys);
                return 2;
            }
            free(sys.request);
            sys.request = xmalloc((size_t)sys.r * sizeof(*sys.request));
            for (int j = 0; j < sys.r; j++) {
                if (!parse_int(argv[request_index + j], &sys.request[j]) ||
                    sys.request[j] < 0) {
                    print_error("deadlock: invalid resource request");
                    dl_free(&sys);
                    return 2;
                }
            }
            sys.req_pid = pid;
            sys.has_request = 1;
        }

        result = run_request(&sys);
    }

    dl_free(&sys);
    return result;
}
