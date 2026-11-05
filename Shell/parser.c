#include "main.h"

#if !defined(__APPLE__) && !defined(__FreeBSD__)
#include <stdio_ext.h>  /* __fpurge */
#endif

/* ---------- small growable token buffer ---------- */

typedef struct {
    char *data;
    size_t len;
    size_t cap;
} TokenBuf;

static void tb_init(TokenBuf *b) {
    b->cap = 64;
    b->data = xmalloc(b->cap);
    b->len = 0;
}

static void tb_reserve(TokenBuf *b, size_t extra) {
    if (b->len + extra + 1 <= b->cap) return;
    size_t newcap = b->cap;
    while (newcap < b->len + extra + 1) newcap *= 2;
    b->data = xrealloc(b->data, newcap);
    b->cap = newcap;
}

static void tb_putc(TokenBuf *b, char c) {
    tb_reserve(b, 1);
    b->data[b->len++] = c;
}

static void tb_puts(TokenBuf *b, const char *s) {
    size_t n = strlen(s);
    tb_reserve(b, n);
    memcpy(b->data + b->len, s, n);
    b->len += n;
}

static char *tb_take(TokenBuf *b) {
    b->data[b->len] = '\0';
    char *result = b->data;
    b->cap = 64;
    b->data = xmalloc(b->cap);
    b->len = 0;
    return result;
}

static void tb_free(TokenBuf *b) {
    free(b->data);
    b->data = NULL;
    b->len = b->cap = 0;
}

/* ---------- sequence splitting: ; && || ---------- */

static int seq_add(SeqItem **items, size_t *count, size_t *cap,
                   const char *start, size_t len, int join, char **err) {
    char *raw = xstrndup(start, len);
    char *text = trim(raw);
    if (*text) {
        char *clean = xstrdup(text);
        free(raw);
        if (*count == *cap) {
            *cap = *cap ? *cap * 2 : 8;
            *items = xrealloc(*items, *cap * sizeof(**items));
        }
        (*items)[*count].text = clean;
        (*items)[*count].join = join;
        (*count)++;
    } else {
        free(raw);
        if (join != JOIN_ALWAYS) {
            *err = xstrdup("syntax error: expected a command after && or ||");
            return -1;
        }
    }
    return 0;
}

void seq_items_free(SeqItem *items, size_t count) {
    if (!items) return;
    for (size_t i = 0; i < count; i++) free(items[i].text);
    free(items);
}

int shell_split_sequence(const char *line, SeqItem **items, size_t *count,
                         char **err) {
    *items = NULL;
    *count = 0;
    *err = NULL;

    size_t cap = 0;
    size_t start = 0;
    size_t i = 0;
    size_t len = strlen(line);
    int quote = 0;
    int escaped = 0;
    int join = JOIN_ALWAYS;
    int at_token_start = 1;

    while (i < len) {
        char c = line[i];

        if (escaped) {
            escaped = 0;
            at_token_start = 0;
            i++;
            continue;
        }
        if (quote) {
            if (c == '\\' && quote == '"') {
                escaped = 1;
                i++;
                continue;
            }
            if (c == quote) quote = 0;
            i++;
            continue;
        }
        if (c == '\\') {
            escaped = 1;
            at_token_start = 0;
            i++;
            continue;
        }
        if (c == '\'' || c == '"') {
            quote = c;
            at_token_start = 0;
            i++;
            continue;
        }
        if (c == '#' && at_token_start) {
            break;
        }
        if (isspace((unsigned char)c)) {
            at_token_start = 1;
            i++;
            continue;
        }

        if (c == ';') {
            if (seq_add(items, count, &cap, line + start, i - start, join, err) < 0)
                goto fail;
            join = JOIN_ALWAYS;
            i++;
            start = i;
            at_token_start = 1;
            continue;
        }
        if (c == '&' && i + 1 < len && line[i + 1] == '&') {
            if (seq_add(items, count, &cap, line + start, i - start, join, err) < 0)
                goto fail;
            join = JOIN_AND;
            i += 2;
            start = i;
            at_token_start = 1;
            continue;
        }
        if (c == '|' && i + 1 < len && line[i + 1] == '|') {
            if (seq_add(items, count, &cap, line + start, i - start, join, err) < 0)
                goto fail;
            join = JOIN_OR;
            i += 2;
            start = i;
            at_token_start = 1;
            continue;
        }
        if (c == '&') {
            /* The & in 2>&1 or >&2 is part of a redirection, not a background job. */
            if (i > 0 && line[i - 1] == '>' && i + 1 < len &&
                (line[i + 1] == '1' || line[i + 1] == '2')) {
                i++;
                continue;
            }
            *err = xstrdup("background jobs ('&') are not supported");
            goto fail;
        }
        at_token_start = 0;
        i++;
    }

    if (quote) {
        *err = xstrdup("unterminated quote");
        goto fail;
    }
    if (escaped) {
        *err = xstrdup("trailing backslash");
        goto fail;
    }
    if (seq_add(items, count, &cap, line + start, len - start, join, err) < 0)
        goto fail;
    return 0;

fail:
    seq_items_free(*items, *count);
    *items = NULL;
    *count = 0;
    return -1;
}

/* ---------- lexer: words, quotes, $expansion, operators ---------- */

/*
 * Each token remembers whether it is an operator. A quoted or expanded "|"
 * or ">" is an ordinary word and must never be treated as a pipe or redirect.
 */
typedef struct {
    char *text;
    int is_op;
} Token;

typedef struct {
    Token *items;
    size_t len;
    size_t cap;
} TokenVec;

static void tv_init(TokenVec *v) {
    v->items = NULL;
    v->len = 0;
    v->cap = 0;
}

static void tv_push(TokenVec *v, char *text, int is_op) {
    if (v->len == v->cap) {
        v->cap = v->cap ? v->cap * 2 : 8;
        v->items = xrealloc(v->items, v->cap * sizeof(*v->items));
    }
    v->items[v->len].text = text;
    v->items[v->len].is_op = is_op;
    v->len++;
}

static void tv_free(TokenVec *v) {
    for (size_t i = 0; i < v->len; i++) free(v->items[i].text);
    free(v->items);
    tv_init(v);
}

static int is_name_start(int c) {
    return isalpha((unsigned char)c) || c == '_';
}

static int is_name_char(int c) {
    return isalnum((unsigned char)c) || c == '_';
}

static void append_number(TokenBuf *b, long value) {
    char buf[32];
    snprintf(buf, sizeof(buf), "%ld", value);
    tb_puts(b, buf);
}

static void expand_dollar(const char *input, size_t *index, TokenBuf *b) {
    size_t i = *index;
    size_t j = i + 1;

    if (input[j] == '?') {
        append_number(b, g_last_status);
        *index = j + 1;
        return;
    }
    if (input[j] == '$') {
        append_number(b, (long)getpid());
        *index = j + 1;
        return;
    }
    if (input[j] == '{') {
        const char *close = strchr(input + j + 1, '}');
        if (!close) {
            tb_putc(b, '$');
            *index = j;
            return;
        }
        char *name = xstrndup(input + j + 1, (size_t)(close - (input + j + 1)));
        const char *value = getenv(name);
        if (value) tb_puts(b, value);
        free(name);
        *index = (size_t)(close - input) + 1;
        return;
    }
    if (is_name_start((unsigned char)input[j])) {
        size_t end = j;
        while (is_name_char((unsigned char)input[end])) end++;
        char *name = xstrndup(input + j, end - j);
        const char *value = getenv(name);
        if (value) tb_puts(b, value);
        free(name);
        *index = end;
        return;
    }

    tb_putc(b, '$');
    *index = j;
}

static void flush_token(TokenBuf *b, TokenVec *tokens, int *in_token,
                        int *token_quoted) {
    if (*in_token) {
        tv_push(tokens, tb_take(b), 0);
        *in_token = 0;
        *token_quoted = 0;
    }
}

/* Lexes >, >>, >&N with an optional 1/2 prefix, e.g. 2>>, 2>&1, 1>&2. */
static void lex_redirect(const char *input, size_t *index, TokenBuf *b,
                         TokenVec *tokens, int *in_token, int *token_quoted) {
    size_t i = *index;
    char prefix = 0;
    char op[8];
    size_t oplen = 0;

    if (*in_token && !*token_quoted && b->len == 1 &&
        (b->data[0] == '1' || b->data[0] == '2')) {
        prefix = b->data[0];
        b->len = 0;
        *in_token = 0;
    } else {
        flush_token(b, tokens, in_token, token_quoted);
    }

    if (prefix) op[oplen++] = prefix;
    op[oplen++] = '>';
    if (input[i + 1] == '>') {
        op[oplen++] = '>';
        i += 2;
    } else if (input[i + 1] == '&' &&
               (input[i + 2] == '1' || input[i + 2] == '2')) {
        op[oplen++] = '&';
        op[oplen++] = input[i + 2];
        i += 3;
    } else {
        i += 1;
    }
    op[oplen] = '\0';
    tv_push(tokens, xstrdup(op), 1);
    *index = i;
}

static int shell_lex(const char *input, TokenVec *tokens, char **err) {
    TokenBuf b;
    size_t i = 0;
    size_t len = strlen(input);
    int in_token = 0;
    int token_quoted = 0;
    int quote = 0;
    int escaped = 0;

    tv_init(tokens);
    *err = NULL;
    tb_init(&b);

    while (i < len) {
        char c = input[i];

        if (escaped) {
            tb_putc(&b, c);
            escaped = 0;
            in_token = 1;
            i++;
            continue;
        }

        if (quote) {
            if (c == quote) {
                quote = 0;
                i++;
                continue;
            }
            if (quote == '"' && c == '\\') {
                escaped = 1;
                i++;
                continue;
            }
            if (quote == '"' && c == '$') {
                expand_dollar(input, &i, &b);
                in_token = 1;
                continue;
            }
            tb_putc(&b, c);
            in_token = 1;
            i++;
            continue;
        }

        if (c == '\\') {
            escaped = 1;
            in_token = 1;
            token_quoted = 1;
            i++;
            continue;
        }
        if (c == '\'' || c == '"') {
            quote = c;
            in_token = 1;
            token_quoted = 1;
            i++;
            continue;
        }
        if (isspace((unsigned char)c)) {
            flush_token(&b, tokens, &in_token, &token_quoted);
            i++;
            continue;
        }
        if (c == '#' && !in_token) {
            break;
        }
        if (c == '$') {
            expand_dollar(input, &i, &b);
            in_token = 1;
            continue;
        }
        if (c == '~' && !in_token) {
            char next = input[i + 1];
            if (next == '\0' || next == '/' || isspace((unsigned char)next) ||
                next == ';' || next == '|' || next == '&' ||
                next == '<' || next == '>') {
                const char *home = getenv("HOME");
                if (home) tb_puts(&b, home);
                else tb_putc(&b, '~');
            } else {
                tb_putc(&b, '~');
            }
            in_token = 1;
            i++;
            continue;
        }
        if (c == ';') {
            flush_token(&b, tokens, &in_token, &token_quoted);
            tv_push(tokens, xstrdup(";"), 1);
            i++;
            continue;
        }
        if (c == '|') {
            flush_token(&b, tokens, &in_token, &token_quoted);
            if (input[i + 1] == '|') {
                tv_push(tokens, xstrdup("||"), 1);
                i += 2;
            } else {
                tv_push(tokens, xstrdup("|"), 1);
                i++;
            }
            continue;
        }
        if (c == '&') {
            flush_token(&b, tokens, &in_token, &token_quoted);
            if (input[i + 1] == '&') {
                tv_push(tokens, xstrdup("&&"), 1);
                i += 2;
            } else {
                *err = xstrdup("background jobs ('&') are not supported");
                goto fail;
            }
            continue;
        }
        if (c == '>') {
            lex_redirect(input, &i, &b, tokens, &in_token, &token_quoted);
            continue;
        }
        if (c == '<') {
            flush_token(&b, tokens, &in_token, &token_quoted);
            tv_push(tokens, xstrdup("<"), 1);
            i++;
            continue;
        }

        tb_putc(&b, c);
        in_token = 1;
        i++;
    }

    if (quote) {
        *err = xstrdup("unterminated quote");
        goto fail;
    }
    if (escaped) {
        *err = xstrdup("trailing backslash");
        goto fail;
    }
    flush_token(&b, tokens, &in_token, &token_quoted);
    tb_free(&b);
    return 0;

fail:
    tb_free(&b);
    tv_free(tokens);
    return -1;
}

/* ---------- command parsing and execution ---------- */

static void command_init(Command *cmd) {
    memset(cmd, 0, sizeof(*cmd));
}

static void command_free(Command *cmd) {
    free(cmd->argv);
    free(cmd->redirs);
    memset(cmd, 0, sizeof(*cmd));
}

static void command_add_arg(Command *cmd, char *arg) {
    cmd->argv = xrealloc(cmd->argv, (size_t)(cmd->argc + 2) * sizeof(*cmd->argv));
    cmd->argv[cmd->argc++] = arg;
    cmd->argv[cmd->argc] = NULL;
}

static void command_add_redirect(Command *cmd, int fd, int append, int dup_from,
                                 char *path) {
    if (cmd->nredirs == cmd->cap_redirs) {
        cmd->cap_redirs = cmd->cap_redirs ? cmd->cap_redirs * 2 : 4;
        cmd->redirs = xrealloc(cmd->redirs,
                               cmd->cap_redirs * sizeof(*cmd->redirs));
    }
    Redirect *r = &cmd->redirs[cmd->nredirs++];
    r->fd = fd;
    r->append = append;
    r->dup_from = dup_from;
    r->path = path;
}

/*
 * Decodes a redirection operator: <, >, >>, N>, N>>, >&M, N>&M (N, M in 1..2).
 * Returns 1 for a redirection, 0 for any other operator.
 */
static int parse_redirect_op(const char *s, int *fd, int *append,
                             int *dup_from) {
    *append = 0;
    *dup_from = -1;
    if (!strcmp(s, "<")) {
        *fd = 0;
        return 1;
    }

    const char *p = s;
    *fd = 1;
    if (*p == '1' || *p == '2') {
        *fd = *p - '0';
        p++;
    }
    if (*p != '>') return 0;
    p++;
    if (*p == '>') {
        *append = 1;
        p++;
    } else if (*p == '&' && (p[1] == '1' || p[1] == '2')) {
        *dup_from = p[1] - '0';
        p += 2;
    }
    return *p == '\0';
}

static int parse_stage(TokenVec *tokens, size_t start, size_t end, Command *cmd,
                       char **err) {
    command_init(cmd);

    for (size_t i = start; i < end; i++) {
        Token *tok = &tokens->items[i];

        if (!tok->is_op) {
            command_add_arg(cmd, tok->text);
            continue;
        }

        int fd, append, dup_from;
        if (!parse_redirect_op(tok->text, &fd, &append, &dup_from)) {
            *err = xstrfmt("syntax error near unexpected token '%s'", tok->text);
            command_free(cmd);
            return -1;
        }
        if (dup_from >= 0) {
            command_add_redirect(cmd, fd, 0, dup_from, NULL);
            continue;
        }
        if (i + 1 >= end || tokens->items[i + 1].is_op) {
            *err = xstrfmt("expected a file name after '%s'", tok->text);
            command_free(cmd);
            return -1;
        }
        command_add_redirect(cmd, fd, append, -1, tokens->items[++i].text);
    }

    if (cmd->argc == 0 && cmd->nredirs == 0) {
        *err = xstrdup("empty command");
        command_free(cmd);
        return -1;
    }
    return 0;
}

static int apply_redirects(Command *cmd) {
    for (size_t i = 0; i < cmd->nredirs; i++) {
        Redirect *r = &cmd->redirs[i];

        if (r->dup_from >= 0) {
            if (r->dup_from != r->fd && dup2(r->dup_from, r->fd) < 0) {
                print_error("dup2: %s", strerror(errno));
                return -1;
            }
            continue;
        }

        int flags;
        if (r->fd == 0) {
            flags = O_RDONLY;
        } else {
            flags = O_WRONLY | O_CREAT | (r->append ? O_APPEND : O_TRUNC);
        }
        int fd = open(r->path, flags, 0666);
        if (fd < 0) {
            print_error("%s: %s", r->path, strerror(errno));
            return -1;
        }
        if (dup2(fd, r->fd) < 0) {
            print_error("dup2: %s", strerror(errno));
            close(fd);
            return -1;
        }
        close(fd);
    }
    return 0;
}

static int save_fds(int saved[3]) {
    for (int i = 0; i < 3; i++) {
        saved[i] = dup(i);
        if (saved[i] < 0) {
            print_error("dup: %s", strerror(errno));
            for (int j = 0; j < i; j++) close(saved[j]);
            return -1;
        }
    }
    return 0;
}

static void restore_fds(int saved[3]) {
    for (int i = 0; i < 3; i++) {
        if (saved[i] >= 0) {
            dup2(saved[i], i);
            close(saved[i]);
        }
    }
}

static void reset_child_signals(void) {
    signal(SIGINT, SIG_DFL);
    signal(SIGQUIT, SIG_DFL);
}

/*
 * A forked child inherits a copy of the shell's stdin buffer, which may hold
 * input the shell already read. Drop it so a built-in reads from fd 0 itself.
 */
static void discard_stdin_buffer(void) {
#if defined(__APPLE__) || defined(__FreeBSD__)
    fpurge(stdin);
#else
    __fpurge(stdin);
#endif
    clearerr(stdin);
}

static int wait_for_pid(pid_t pid) {
    int status;
    while (waitpid(pid, &status, 0) < 0) {
        if (errno == EINTR) continue;
        print_error("waitpid: %s", strerror(errno));
        return 1;
    }
    if (WIFEXITED(status)) return WEXITSTATUS(status);
    if (WIFSIGNALED(status)) return 128 + WTERMSIG(status);
    return 1;
}

/* ---------- NAME=value prefixes ---------- */

typedef struct {
    char *name;
    char *old;          /* NULL if the variable was unset */
} SavedVar;

static int count_assignments(const Command *cmd) {
    int n = 0;
    while (n < cmd->argc && is_assignment(cmd->argv[n], NULL, NULL)) n++;
    return n;
}

/*
 * Sets the first n NAME=value words. If saved is non-NULL (n zeroed entries),
 * the previous values are recorded so restore_assignments() can undo them.
 */
static int apply_assignments(Command *cmd, int n, SavedVar *saved) {
    for (int i = 0; i < n; i++) {
        char *name = NULL;
        char *value = NULL;
        is_assignment(cmd->argv[i], &name, &value);
        if (saved) {
            const char *old = getenv(name);
            saved[i].name = xstrdup(name);
            saved[i].old = old ? xstrdup(old) : NULL;
        }
        int rc = setenv(name, value, 1);
        free(name);
        free(value);
        if (rc < 0) {
            print_error("setenv: %s", strerror(errno));
            return -1;
        }
    }
    return 0;
}

static void restore_assignments(SavedVar *saved, int n) {
    if (!saved) return;
    /* Reverse order so repeated names (A=1 A=2 cmd) restore correctly. */
    for (int i = n - 1; i >= 0; i--) {
        if (!saved[i].name) continue;
        if (saved[i].old) setenv(saved[i].name, saved[i].old, 1);
        else unsetenv(saved[i].name);
        free(saved[i].name);
        free(saved[i].old);
    }
}

/* Built-ins that change the shell's own state must run in the shell process. */
static int builtin_needs_shell_process(const char *name) {
    return !strcmp(name, "cd") || !strcmp(name, "exit");
}

/* Never returns. Used by both single commands and pipeline children. */
static void execute_in_child(Command *cmd, int first_arg) {
    if (first_arg >= cmd->argc) _exit(0);

    char **args = cmd->argv + first_arg;
    const Builtin *builtin = builtin_find(args[0]);
    if (builtin) {
        discard_stdin_buffer();
        int status = builtin->fn(cmd->argc - first_arg, args);
        fflush(NULL);
        _exit(status);
    }

    execvp(args[0], args);
    if (errno == ENOENT) {
        fprintf(stderr, "minishell: %s: command not found\n", args[0]);
    } else {
        print_error("%s: %s", args[0], strerror(errno));
    }
    _exit(EXIT_CMD_NOT_FOUND);
}

static int run_redirects_only(Command *cmd) {
    int saved[3];
    if (save_fds(saved) < 0) return 1;
    int rc = apply_redirects(cmd) < 0 ? 1 : 0;
    fflush(NULL);
    restore_fds(saved);
    return rc;
}

static int execute_command(Command *cmd) {
    int nassign = count_assignments(cmd);

    /* Only assignments (and/or redirections): they persist in the shell. */
    if (nassign >= cmd->argc) {
        if (nassign > 0 && apply_assignments(cmd, nassign, NULL) < 0) return 1;
        return run_redirects_only(cmd);
    }

    char **args = cmd->argv + nassign;
    int argc = cmd->argc - nassign;
    const Builtin *builtin = builtin_find(args[0]);

    if (builtin && builtin_needs_shell_process(args[0])) {
        SavedVar *saved = NULL;
        if (nassign > 0) {
            saved = xmalloc((size_t)nassign * sizeof(*saved));
            memset(saved, 0, (size_t)nassign * sizeof(*saved));
        }
        int rc = 1;
        if (apply_assignments(cmd, nassign, saved) == 0) {
            int fds[3];
            if (save_fds(fds) == 0) {
                if (apply_redirects(cmd) == 0) rc = builtin->fn(argc, args);
                fflush(NULL);
                restore_fds(fds);
            }
        }
        restore_assignments(saved, nassign);
        free(saved);
        return rc;
    }

    /*
     * Every other command, including built-ins such as cat and grep, runs in
     * a child: Ctrl+C can stop it, and reading stdin to EOF or redirecting
     * stdin cannot disturb the shell's own input stream. NAME=value prefixes
     * are set only in the child's environment.
     */
    fflush(NULL);
    pid_t pid = fork();
    if (pid < 0) {
        print_error("fork: %s", strerror(errno));
        return 1;
    }
    if (pid == 0) {
        reset_child_signals();
        if (apply_redirects(cmd) < 0) _exit(1);
        if (apply_assignments(cmd, nassign, NULL) < 0) _exit(1);
        execute_in_child(cmd, nassign);
        _exit(EXIT_CMD_NOT_FOUND);
    }
    return wait_for_pid(pid);
}

static int is_pipe_token(const Token *t) {
    return t->is_op && !strcmp(t->text, "|");
}

static int execute_pipeline(TokenVec *tokens) {
    size_t stages_count = 1;
    for (size_t i = 0; i < tokens->len; i++) {
        if (is_pipe_token(&tokens->items[i])) stages_count++;
    }

    Command *stages = xmalloc(stages_count * sizeof(*stages));
    size_t stage = 0;
    size_t start = 0;
    char *err = NULL;

    for (size_t i = 0; i <= tokens->len; i++) {
        if (i == tokens->len || is_pipe_token(&tokens->items[i])) {
            if (parse_stage(tokens, start, i, &stages[stage], &err) < 0) {
                print_error("%s", err ? err : "parse error");
                free(err);
                for (size_t j = 0; j <= stage; j++) command_free(&stages[j]);
                free(stages);
                return 2;
            }
            stage++;
            start = i + 1;
        }
    }

    if (stages_count == 1) {
        int status = execute_command(&stages[0]);
        command_free(&stages[0]);
        free(stages);
        return status;
    }

    fflush(NULL);
    pid_t *pids = xmalloc(stages_count * sizeof(*pids));
    int prev_read = -1;
    int status = 1;

    for (size_t i = 0; i < stages_count; i++) {
        int pipefd[2] = {-1, -1};
        if (i + 1 < stages_count) {
            if (pipe(pipefd) < 0) {
                print_error("pipe: %s", strerror(errno));
                if (prev_read >= 0) close(prev_read);
                for (size_t j = 0; j < i; j++) wait_for_pid(pids[j]);
                free(pids);
                for (size_t j = 0; j < stages_count; j++) command_free(&stages[j]);
                free(stages);
                return 1;
            }
        }

        pid_t pid = fork();
        if (pid < 0) {
            print_error("fork: %s", strerror(errno));
            if (pipefd[0] >= 0) close(pipefd[0]);
            if (pipefd[1] >= 0) close(pipefd[1]);
            if (prev_read >= 0) close(prev_read);
            for (size_t j = 0; j < i; j++) wait_for_pid(pids[j]);
            free(pids);
            for (size_t j = 0; j < stages_count; j++) command_free(&stages[j]);
            free(stages);
            return 1;
        }

        if (pid == 0) {
            reset_child_signals();
            if (prev_read >= 0) {
                if (dup2(prev_read, STDIN_FILENO) < 0) _exit(1);
            }
            if (i + 1 < stages_count) {
                if (dup2(pipefd[1], STDOUT_FILENO) < 0) _exit(1);
            }
            if (prev_read >= 0) close(prev_read);
            if (pipefd[0] >= 0) close(pipefd[0]);
            if (pipefd[1] >= 0) close(pipefd[1]);

            if (apply_redirects(&stages[i]) < 0) _exit(1);
            int first_arg = count_assignments(&stages[i]);
            if (apply_assignments(&stages[i], first_arg, NULL) < 0) _exit(1);
            execute_in_child(&stages[i], first_arg);
            _exit(EXIT_CMD_NOT_FOUND);
        }

        pids[i] = pid;
        if (prev_read >= 0) close(prev_read);
        if (i + 1 < stages_count) {
            close(pipefd[1]);
            prev_read = pipefd[0];
        } else {
            prev_read = -1;
        }
    }

    for (size_t i = 0; i < stages_count; i++) {
        int s = wait_for_pid(pids[i]);
        if (i + 1 == stages_count) status = s;
    }

    free(pids);
    for (size_t i = 0; i < stages_count; i++) command_free(&stages[i]);
    free(stages);
    return status;
}

int shell_execute_segment(const char *segment) {
    TokenVec tokens;
    char *err = NULL;

    if (shell_lex(segment, &tokens, &err) < 0) {
        print_error("%s", err ? err : "parse error");
        free(err);
        return 2;
    }
    if (tokens.len == 0) {
        tv_free(&tokens);
        return 0;
    }
    int status = execute_pipeline(&tokens);
    tv_free(&tokens);
    return status;
}

int shell_execute_line(const char *line) {
    SeqItem *items = NULL;
    size_t count = 0;
    char *err = NULL;

    if (shell_split_sequence(line, &items, &count, &err) < 0) {
        print_error("%s", err ? err : "parse error");
        free(err);
        return 2;
    }

    int last = g_last_status;
    for (size_t i = 0; i < count; i++) {
        int run = 1;
        if (items[i].join == JOIN_AND && last != 0) run = 0;
        else if (items[i].join == JOIN_OR && last == 0) run = 0;

        if (run) {
            last = shell_execute_segment(items[i].text);
            g_last_status = last;
        }
        if (g_should_exit) break;
    }

    seq_items_free(items, count);
    return last;
}
