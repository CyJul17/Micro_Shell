#include "main.h"

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
            /* The & in 2>&1 is part of a redirection, not a background job. */
            if (i > 0 && line[i - 1] == '>' && i + 1 < len && line[i + 1] == '1') {
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

static void flush_token(TokenBuf *b, StrVec *tokens, int *in_token,
                        int *token_quoted) {
    if (*in_token) {
        strvec_push(tokens, tb_take(b));
        *in_token = 0;
        *token_quoted = 0;
    }
}

static int lex_redirect(const char *input, size_t *index, TokenBuf *b,
                        StrVec *tokens, int *in_token, int *token_quoted,
                        char **err) {
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

    if (input[i + 1] == '>') {
        if (prefix) op[oplen++] = prefix;
        op[oplen++] = '>';
        op[oplen++] = '>';
        i += 2;
    } else if (input[i + 1] == '&' && input[i + 1] != '\0' &&
               input[i + 2] == '1') {
        if (prefix) op[oplen++] = prefix;
        op[oplen++] = '>';
        op[oplen++] = '&';
        op[oplen++] = '1';
        i += 3;
    } else {
        if (prefix) op[oplen++] = prefix;
        op[oplen++] = '>';
        i += 1;
    }
    op[oplen] = '\0';
    strvec_push(tokens, xstrdup(op));
    *index = i;
    (void)err;
    return 0;
}

int shell_lex(const char *input, StrVec *tokens, char **err) {
    TokenBuf b;
    size_t i = 0;
    size_t len = strlen(input);
    int in_token = 0;
    int token_quoted = 0;
    int quote = 0;
    int escaped = 0;

    strvec_init(tokens);
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
            strvec_push(tokens, xstrdup(";"));
            i++;
            continue;
        }
        if (c == '|') {
            flush_token(&b, tokens, &in_token, &token_quoted);
            if (input[i + 1] == '|') {
                strvec_push(tokens, xstrdup("||"));
                i += 2;
            } else {
                strvec_push(tokens, xstrdup("|"));
                i++;
            }
            continue;
        }
        if (c == '&') {
            flush_token(&b, tokens, &in_token, &token_quoted);
            if (input[i + 1] == '&') {
                strvec_push(tokens, xstrdup("&&"));
                i += 2;
            } else {
                *err = xstrdup("background jobs ('&') are not supported");
                goto fail;
            }
            continue;
        }
        if (c == '>') {
            lex_redirect(input, &i, &b, tokens, &in_token, &token_quoted, err);
            continue;
        }
        if (c == '<') {
            flush_token(&b, tokens, &in_token, &token_quoted);
            strvec_push(tokens, xstrdup("<"));
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
    strvec_free(tokens);
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

static void command_add_redirect(Command *cmd, int fd, int append, int dup,
                                 char *path) {
    if (cmd->nredirs == cmd->cap_redirs) {
        cmd->cap_redirs = cmd->cap_redirs ? cmd->cap_redirs * 2 : 4;
        cmd->redirs = xrealloc(cmd->redirs,
                               cmd->cap_redirs * sizeof(*cmd->redirs));
    }
    Redirect *r = &cmd->redirs[cmd->nredirs++];
    r->fd = fd;
    r->append = append;
    r->dup_stdout = dup;
    r->path = path;
}

static int is_redirect_token(const char *s) {
    return !strcmp(s, "<") || !strcmp(s, ">") || !strcmp(s, ">>") ||
           !strcmp(s, "1>") || !strcmp(s, "1>>") ||
           !strcmp(s, "2>") || !strcmp(s, "2>>") || !strcmp(s, "2>&1");
}

static int parse_stage(StrVec *tokens, size_t start, size_t end, Command *cmd,
                       char **err) {
    command_init(cmd);

    for (size_t i = start; i < end; i++) {
        char *tok = tokens->items[i];

        if (!strcmp(tok, "2>&1")) {
            command_add_redirect(cmd, 2, 0, 1, NULL);
            continue;
        }
        if (is_redirect_token(tok)) {
            if (i + 1 >= end) {
                *err = xstrfmt("expected a file name after '%s'", tok);
                command_free(cmd);
                return -1;
            }
            int fd = 1;
            int append = 0;
            if (!strcmp(tok, "<")) fd = 0;
            else if (tok[0] == '2') fd = 2;
            else if (tok[0] == '1') fd = 1;
            if (strstr(tok, ">>")) append = 1;
            command_add_redirect(cmd, fd, append, 0, tokens->items[++i]);
            continue;
        }
        command_add_arg(cmd, tok);
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

        if (r->dup_stdout) {
            if (dup2(STDOUT_FILENO, r->fd) < 0) {
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

static int set_leading_assignments(Command *cmd, int *first_arg) {
    int idx = 0;
    while (idx < cmd->argc) {
        char *name = NULL;
        char *value = NULL;
        if (!is_assignment(cmd->argv[idx], &name, &value)) break;
        if (setenv(name, value, 1) < 0) {
            print_error("setenv: %s", strerror(errno));
            free(name);
            free(value);
            return -1;
        }
        free(name);
        free(value);
        idx++;
    }
    *first_arg = idx;
    return 0;
}

/* Never returns. Used by both external commands and pipeline children. */
static void execute_in_child(Command *cmd, int first_arg) {
    if (first_arg >= cmd->argc) _exit(0);

    char **args = cmd->argv + first_arg;
    const Builtin *builtin = builtin_find(args[0]);
    if (builtin) {
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

static int execute_command(Command *cmd) {
    if (cmd->argc == 0) {
        int saved[3];
        if (save_fds(saved) < 0) return 1;
        int rc = apply_redirects(cmd) < 0 ? 1 : 0;
        fflush(NULL);
        restore_fds(saved);
        return rc;
    }

    int first_arg = 0;
    if (set_leading_assignments(cmd, &first_arg) < 0) return 1;
    if (first_arg >= cmd->argc) {
        int saved[3];
        if (save_fds(saved) < 0) return 1;
        int rc = apply_redirects(cmd) < 0 ? 1 : 0;
        fflush(NULL);
        restore_fds(saved);
        return rc;
    }

    char **args = cmd->argv + first_arg;
    int argc = cmd->argc - first_arg;
    const Builtin *builtin = builtin_find(args[0]);

    if (builtin) {
        int saved[3];
        if (save_fds(saved) < 0) return 1;
        int rc = 1;
        if (apply_redirects(cmd) == 0) rc = builtin->fn(argc, args);
        fflush(NULL);
        restore_fds(saved);
        return rc;
    }

    fflush(NULL);
    pid_t pid = fork();
    if (pid < 0) {
        print_error("fork: %s", strerror(errno));
        return 1;
    }
    if (pid == 0) {
        reset_child_signals();
        if (apply_redirects(cmd) < 0) _exit(1);
        execute_in_child(cmd, first_arg);
        _exit(EXIT_CMD_NOT_FOUND);
    }
    return wait_for_pid(pid);
}

static int execute_pipeline(StrVec *tokens) {
    size_t stages_count = 1;
    for (size_t i = 0; i < tokens->len; i++) {
        if (!strcmp(tokens->items[i], "|")) stages_count++;
    }

    Command *stages = xmalloc(stages_count * sizeof(*stages));
    size_t stage = 0;
    size_t start = 0;
    char *err = NULL;

    for (size_t i = 0; i <= tokens->len; i++) {
        if (i == tokens->len || !strcmp(tokens->items[i], "|")) {
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
            int first_arg = 0;
            if (set_leading_assignments(&stages[i], &first_arg) < 0) _exit(1);
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
    StrVec tokens;
    char *err = NULL;

    if (shell_lex(segment, &tokens, &err) < 0) {
        print_error("%s", err ? err : "parse error");
        free(err);
        return 2;
    }
    if (tokens.len == 0) {
        strvec_free(&tokens);
        return 0;
    }
    int status = execute_pipeline(&tokens);
    strvec_free(&tokens);
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
