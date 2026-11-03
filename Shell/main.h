#ifndef MINISHELL_MAIN_H
#define MINISHELL_MAIN_H

/*
 * MiniShell - CmpSc 123 final project
 *
 * Shared declarations for the shell, scheduler, and deadlock simulator.
 * The project is deliberately split into small modules:
 *   main.c       - REPL and entry point
 *   parser.c     - quoting, expansion, redirection, pipes, &&/||/;
 *   builtin.c    - shell built-ins and grep
 *   scheduler.c  - six CPU scheduling algorithms
 *   deadlock.c   - deadlock detection and Banker's algorithm
 *   utils.c      - allocation, path, and filesystem helpers
 */

#ifndef _POSIX_C_SOURCE
#define _POSIX_C_SOURCE 200809L
#endif
#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif

#include <ctype.h>
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <grp.h>
#include <limits.h>
#include <pwd.h>
#include <regex.h>
#include <signal.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>
#include <utime.h>

#define MINISHELL_PROMPT "minishell> "
#define MINISHELL_NAME "minishell"
#define EXIT_CMD_NOT_FOUND 127
#define EXIT_USAGE 2

/* A small growable vector of owned strings. */
typedef struct {
    char **items;
    size_t len;
    size_t cap;
} StrVec;

/* One redirection attached to a command. */
typedef struct {
    int fd;             /* 0 = stdin, 1 = stdout, 2 = stderr */
    int append;         /* nonzero for >> */
    int dup_stdout;     /* nonzero for 2>&1 */
    char *path;         /* borrowed from the token vector */
} Redirect;

/* One command in a pipeline. */
typedef struct {
    char **argv;        /* NULL-terminated, borrowed from token vector */
    int argc;
    Redirect *redirs;
    size_t nredirs;
    size_t cap_redirs;
} Command;

/* One command in a ;/&&/|| sequence. */
typedef struct {
    char *text;
    int join;           /* JOIN_ALWAYS, JOIN_AND, or JOIN_OR */
} SeqItem;

enum {
    JOIN_ALWAYS = 0,
    JOIN_AND = 1,
    JOIN_OR = 2
};

/* Built-in command table entry. */
typedef int (*BuiltinFn)(int argc, char **argv);

typedef struct {
    const char *name;
    BuiltinFn fn;
} Builtin;

extern int g_last_status;
extern volatile sig_atomic_t g_should_exit;

/* ---------- utils.c ---------- */
void *xmalloc(size_t size);
void *xrealloc(void *ptr, size_t size);
char *xstrdup(const char *s);
char *xstrndup(const char *s, size_t n);
char *xstrfmt(const char *fmt, ...);
void strvec_init(StrVec *v);
void strvec_push(StrVec *v, char *s);
void strvec_free(StrVec *v);
char *trim(char *s);
int parse_int(const char *s, int *out);
int is_assignment(const char *s, char **name, char **value);
void print_error(const char *fmt, ...);
void print_usage_error(const char *usage);
char *path_join(const char *dir, const char *name);
char *path_basename(const char *path);
int path_exists(const char *path);
int is_directory(const char *path);
int copy_file(const char *src, const char *dst);
int copy_path(const char *src, const char *dst, int recursive);
int remove_path(const char *path, int recursive, int force);
int mkdir_p(const char *path);
int expand_tilde(const char *arg, char **out);
void format_mode(mode_t mode, char *buf);

/* ---------- parser.c ---------- */
int shell_execute_line(const char *line);
int shell_execute_segment(const char *segment);
int shell_lex(const char *input, StrVec *tokens, char **err);
int shell_split_sequence(const char *line, SeqItem **items, size_t *count,
                         char **err);
void seq_items_free(SeqItem *items, size_t count);
const Builtin *builtin_find(const char *name);

/* ---------- builtin.c ---------- */
int builtin_pwd(int argc, char **argv);
int builtin_cd(int argc, char **argv);
int builtin_ls(int argc, char **argv);
int builtin_mkdir(int argc, char **argv);
int builtin_rmdir(int argc, char **argv);
int builtin_touch(int argc, char **argv);
int builtin_cp(int argc, char **argv);
int builtin_mv(int argc, char **argv);
int builtin_rm(int argc, char **argv);
int builtin_cat(int argc, char **argv);
int builtin_chmod(int argc, char **argv);
int builtin_grep(int argc, char **argv);
int builtin_echo(int argc, char **argv);
int builtin_env(int argc, char **argv);
int builtin_exit(int argc, char **argv);
int builtin_help(int argc, char **argv);
int builtin_true(int argc, char **argv);
int builtin_false(int argc, char **argv);
int builtin_sched(int argc, char **argv);
int builtin_deadlock(int argc, char **argv);

/* ---------- scheduler.c / deadlock.c ---------- */
int sched_main(int argc, char **argv);
int deadlock_main(int argc, char **argv);

/* ---------- shell_getline.c ---------- */
ssize_t shell_getline(char **lineptr, size_t *n, FILE *stream);

#endif /* MINISHELL_MAIN_H */
