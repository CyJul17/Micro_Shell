#include "main.h"

#ifndef PATH_MAX
#define PATH_MAX 4096
#endif

/* ======================================================================
 * pwd / cd
 * ==================================================================== */

int builtin_pwd(int argc, char **argv) {
    (void)argv;
    if (argc != 1) {
        print_usage_error("pwd");
        return 2;
    }
    char buf[PATH_MAX];
    if (!getcwd(buf, sizeof(buf))) {
        print_error("pwd: %s", strerror(errno));
        return 1;
    }
    printf("%s\n", buf);
    return 0;
}

int builtin_cd(int argc, char **argv) {
    if (argc > 2) {
        print_usage_error("cd [directory]");
        return 2;
    }

    char oldcwd[PATH_MAX];
    if (!getcwd(oldcwd, sizeof(oldcwd))) oldcwd[0] = '\0';

    char *expanded = NULL;
    const char *target = NULL;
    int print_target = 0;

    if (argc == 1) {
        target = getenv("HOME");
        if (!target || !*target) {
            print_error("cd: HOME is not set");
            return 1;
        }
    } else if (!strcmp(argv[1], "-")) {
        target = getenv("OLDPWD");
        if (!target || !*target) {
            print_error("cd: OLDPWD is not set");
            return 1;
        }
        print_target = 1;
    } else {
        expand_tilde(argv[1], &expanded);
        target = expanded;
    }

    if (chdir(target) < 0) {
        print_error("cd: %s: %s", target, strerror(errno));
        free(expanded);
        return 1;
    }

    char newcwd[PATH_MAX];
    if (getcwd(newcwd, sizeof(newcwd))) {
        if (oldcwd[0]) setenv("OLDPWD", oldcwd, 1);
        setenv("PWD", newcwd, 1);
        if (print_target) printf("%s\n", newcwd);
    }

    free(expanded);
    return 0;
}

/* ======================================================================
 * ls
 * ==================================================================== */

typedef struct {
    char **items;
    size_t len;
    size_t cap;
} NameVec;

static void nv_push(NameVec *v, const char *name) {
    if (v->len == v->cap) {
        v->cap = v->cap ? v->cap * 2 : 16;
        v->items = xrealloc(v->items, v->cap * sizeof(*v->items));
    }
    v->items[v->len++] = xstrdup(name);
}

static void nv_free(NameVec *v) {
    for (size_t i = 0; i < v->len; i++) free(v->items[i]);
    free(v->items);
    v->items = NULL;
    v->len = v->cap = 0;
}

static int cmp_names(const void *a, const void *b) {
    const char *const *sa = a;
    const char *const *sb = b;
    return strcmp(*sa, *sb);
}

static void print_long_entry(const char *path, const char *name,
                             const struct stat *st) {
    char mode[11];
    char timebuf[64] = "?";
    format_mode(st->st_mode, mode);

    struct passwd *pw = getpwuid(st->st_uid);
    struct group *gr = getgrgid(st->st_gid);
    const char *user = pw ? pw->pw_name : "?";
    const char *group = gr ? gr->gr_name : "?";

    struct tm *tm = localtime(&st->st_mtime);
    if (tm) strftime(timebuf, sizeof(timebuf), "%b %e %H:%M", tm);

    printf("%s %3lu %-8s %-8s %8lld %s %s",
           mode, (unsigned long)st->st_nlink, user, group,
           (long long)st->st_size, timebuf, name);

    if (S_ISLNK(st->st_mode)) {
        char linkbuf[PATH_MAX];
        ssize_t n = readlink(path, linkbuf, sizeof(linkbuf) - 1);
        if (n >= 0) {
            linkbuf[n] = '\0';
            printf(" -> %s", linkbuf);
        }
    }
    putchar('\n');
}

static int list_one(const char *path, int long_format, int show_all) {
    struct stat st;

    if (lstat(path, &st) < 0) {
        print_error("ls: %s: %s", path, strerror(errno));
        return 1;
    }

    if (!S_ISDIR(st.st_mode)) {
        if (long_format) print_long_entry(path, path, &st);
        else printf("%s\n", path);
        return 0;
    }

    DIR *dir = opendir(path);
    if (!dir) {
        print_error("ls: %s: %s", path, strerror(errno));
        return 1;
    }

    NameVec names = {0};
    struct dirent *ent;
    while ((ent = readdir(dir)) != NULL) {
        if (!show_all &&
            (!strcmp(ent->d_name, ".") || !strcmp(ent->d_name, "..")))
            continue;
        nv_push(&names, ent->d_name);
    }
    closedir(dir);
    qsort(names.items, names.len, sizeof(*names.items), cmp_names);

    int status = 0;
    for (size_t i = 0; i < names.len; i++) {
        char *child = path_join(path, names.items[i]);
        struct stat cst;
        if (lstat(child, &cst) < 0) {
            print_error("ls: %s: %s", child, strerror(errno));
            status = 1;
        } else if (long_format) {
            print_long_entry(child, names.items[i], &cst);
        } else {
            printf("%s\n", names.items[i]);
        }
        free(child);
    }
    nv_free(&names);
    return status;
}

int builtin_ls(int argc, char **argv) {
    int long_format = 0;
    int show_all = 0;
    int i = 1;

    while (i < argc && argv[i][0] == '-' && argv[i][1]) {
        if (!strcmp(argv[i], "--")) {
            i++;
            break;
        }
        for (const char *p = argv[i] + 1; *p; p++) {
            if (*p == 'l') long_format = 1;
            else if (*p == 'a') show_all = 1;
            else if (*p == '1') { /* accepted, default output is one per line */ }
            else {
                print_error("ls: invalid option -- '%c'", *p);
                return 2;
            }
        }
        i++;
    }

    int paths = argc - i;
    int status = 0;

    if (paths == 0) {
        return list_one(".", long_format, show_all);
    }

    for (int n = 0; n < paths; n++) {
        if (paths > 1) {
            if (n > 0) putchar('\n');
            printf("%s:\n", argv[i + n]);
        }
        if (list_one(argv[i + n], long_format, show_all) != 0) status = 1;
    }
    return status;
}

/* ======================================================================
 * mkdir / rmdir / touch
 * ==================================================================== */

int builtin_mkdir(int argc, char **argv) {
    int parents = 0;
    int i = 1;

    while (i < argc && argv[i][0] == '-' && argv[i][1]) {
        if (!strcmp(argv[i], "-p")) {
            parents = 1;
            i++;
        } else {
            print_error("mkdir: invalid option -- '%s'", argv[i]);
            return 2;
        }
    }
    if (i >= argc) {
        print_usage_error("mkdir [-p] directory...");
        return 2;
    }

    int status = 0;
    for (; i < argc; i++) {
        int rc = parents ? mkdir_p(argv[i]) : mkdir(argv[i], 0777);
        if (rc != 0) {
            if (!parents) print_error("mkdir: %s: %s", argv[i], strerror(errno));
            status = 1;
        }
    }
    return status;
}

int builtin_rmdir(int argc, char **argv) {
    if (argc < 2) {
        print_usage_error("rmdir directory...");
        return 2;
    }
    int status = 0;
    for (int i = 1; i < argc; i++) {
        if (rmdir(argv[i]) < 0) {
            print_error("rmdir: %s: %s", argv[i], strerror(errno));
            status = 1;
        }
    }
    return status;
}

int builtin_touch(int argc, char **argv) {
    if (argc < 2) {
        print_usage_error("touch file...");
        return 2;
    }
    int status = 0;
    for (int i = 1; i < argc; i++) {
        /* Updating an existing file does not require opening it for writing. */
        if (utime(argv[i], NULL) == 0) continue;

        int fd = open(argv[i], O_WRONLY | O_CREAT, 0666);
        if (fd < 0) {
            print_error("touch: %s: %s", argv[i], strerror(errno));
            status = 1;
            continue;
        }
        close(fd);
        if (utime(argv[i], NULL) < 0) {
            print_error("touch: %s: %s", argv[i], strerror(errno));
            status = 1;
        }
    }
    return status;
}

/* ======================================================================
 * cp / mv / rm
 * ==================================================================== */

int builtin_cp(int argc, char **argv) {
    int recursive = 0;
    int i = 1;

    while (i < argc && argv[i][0] == '-' && argv[i][1]) {
        if (!strcmp(argv[i], "-r") || !strcmp(argv[i], "-R")) {
            recursive = 1;
            i++;
        } else if (!strcmp(argv[i], "--")) {
            i++;
            break;
        } else {
            print_error("cp: invalid option -- '%s'", argv[i]);
            return 2;
        }
    }

    if (argc - i != 2) {
        print_usage_error("cp [-r] source destination");
        return 2;
    }
    return copy_path(argv[i], argv[i + 1], recursive);
}

int builtin_mv(int argc, char **argv) {
    if (argc != 3) {
        print_usage_error("mv source destination");
        return 2;
    }

    const char *src = argv[1];
    char *target = NULL;
    if (is_directory(argv[2])) {
        char *base = path_basename(src);
        target = path_join(argv[2], base);
        free(base);
    } else {
        target = xstrdup(argv[2]);
    }

    int rc = 0;
    if (rename(src, target) < 0) {
        if (errno == EXDEV) {
            if (copy_path(src, target, 1) != 0) {
                rc = 1;
            } else if (remove_path(src, 1, 0) != 0) {
                rc = 1;
            }
        } else {
            print_error("mv: %s: %s", src, strerror(errno));
            rc = 1;
        }
    }

    free(target);
    return rc;
}

int builtin_rm(int argc, char **argv) {
    int recursive = 0;
    int force = 0;
    int i = 1;

    while (i < argc && argv[i][0] == '-' && argv[i][1]) {
        if (!strcmp(argv[i], "--")) {
            i++;
            break;
        }
        for (const char *p = argv[i] + 1; *p; p++) {
            if (*p == 'r' || *p == 'R') recursive = 1;
            else if (*p == 'f') force = 1;
            else {
                print_error("rm: invalid option -- '%c'", *p);
                return 2;
            }
        }
        i++;
    }

    if (i >= argc) {
        if (force) return 0;
        print_usage_error("rm [-r] [-f] file...");
        return 2;
    }

    int status = 0;
    for (; i < argc; i++) {
        if (remove_path(argv[i], recursive, force) != 0) status = 1;
    }
    return status;
}

/* ======================================================================
 * cat
 * ==================================================================== */

static int cat_stream(FILE *fp) {
    char buf[8192];
    size_t n;
    while ((n = fread(buf, 1, sizeof(buf), fp)) > 0) {
        if (fwrite(buf, 1, n, stdout) != n) return 1;
    }
    return ferror(fp) ? 1 : 0;
}

int builtin_cat(int argc, char **argv) {
    if (argc == 1) return cat_stream(stdin);

    int status = 0;
    for (int i = 1; i < argc; i++) {
        FILE *fp;
        if (!strcmp(argv[i], "-")) {
            fp = stdin;
        } else {
            fp = fopen(argv[i], "rb");
            if (!fp) {
                print_error("cat: %s: %s", argv[i], strerror(errno));
                status = 1;
                continue;
            }
        }
        if (cat_stream(fp) != 0) {
            print_error("cat: %s: read/write error", argv[i]);
            status = 1;
        }
        if (fp != stdin) fclose(fp);
    }
    return status;
}

/* ======================================================================
 * chmod
 * ==================================================================== */

int builtin_chmod(int argc, char **argv) {
    if (argc < 3) {
        print_usage_error("chmod MODE file...");
        return 2;
    }

    errno = 0;
    char *end = NULL;
    long mode = strtol(argv[1], &end, 8);
    if (errno != 0 || end == argv[1] || *end != '\0' || mode < 0 || mode > 07777) {
        print_error("chmod: invalid mode '%s' (use an octal mode such as 755)",
                    argv[1]);
        return 2;
    }

    int status = 0;
    for (int i = 2; i < argc; i++) {
        if (chmod(argv[i], (mode_t)mode) < 0) {
            print_error("chmod: %s: %s", argv[i], strerror(errno));
            status = 1;
        }
    }
    return status;
}

/* ======================================================================
 * grep
 * ==================================================================== */

static int contains_case_insensitive(const char *haystack, const char *needle) {
    size_t n = strlen(needle);
    if (n == 0) return 1;
    for (const char *p = haystack; *p; p++) {
        if (strncasecmp(p, needle, n) == 0) return 1;
    }
    return 0;
}

static int grep_stream(FILE *fp, const char *filename, const char *pattern,
                       regex_t *re, int use_regex, int ignore_case,
                       int show_numbers, int show_filename,
                       int *matched_any, int *had_error) {
    char *line = NULL;
    size_t cap = 0;
    long line_no = 0;
    ssize_t n;

    while ((n = getline(&line, &cap, fp)) != -1) {
        (void)n;
        line_no++;
        size_t len = strlen(line);
        if (len > 0 && line[len - 1] == '\n') line[--len] = '\0';
        if (len > 0 && line[len - 1] == '\r') line[--len] = '\0';

        int matched;
        if (use_regex) {
            matched = regexec(re, line, 0, NULL, 0) == 0;
        } else if (ignore_case) {
            matched = contains_case_insensitive(line, pattern);
        } else {
            matched = strstr(line, pattern) != NULL;
        }

        if (matched) {
            *matched_any = 1;
            if (show_filename) printf("%s:", filename);
            if (show_numbers) printf("%ld:", line_no);
            printf("%s\n", line);
        }
    }

    if (ferror(fp)) {
        print_error("grep: %s: read error", filename ? filename : "(stdin)");
        *had_error = 1;
    }
    free(line);
    return 0;
}

int builtin_grep(int argc, char **argv) {
    int ignore_case = 0;
    int show_numbers = 0;
    int use_regex = 0;
    int i = 1;

    while (i < argc && argv[i][0] == '-' && argv[i][1]) {
        if (!strcmp(argv[i], "--")) {
            i++;
            break;
        }
        for (const char *p = argv[i] + 1; *p; p++) {
            if (*p == 'i') ignore_case = 1;
            else if (*p == 'n') show_numbers = 1;
            else if (*p == 'E') use_regex = 1;
            else {
                print_error("grep: invalid option -- '%c'", *p);
                return 2;
            }
        }
        i++;
    }

    if (i >= argc) {
        print_usage_error("grep [-i] [-n] [-E] pattern [file...]");
        return 2;
    }

    const char *pattern = argv[i++];
    int file_count = argc - i;
    int matched_any = 0;
    int had_error = 0;
    regex_t re;
    int compiled = 0;

    if (use_regex) {
        int flags = REG_EXTENDED | (ignore_case ? REG_ICASE : 0);
        int rc = regcomp(&re, pattern, flags);
        if (rc != 0) {
            char errbuf[256];
            regerror(rc, &re, errbuf, sizeof(errbuf));
            print_error("grep: %s", errbuf);
            return 2;
        }
        compiled = 1;
    }

    if (file_count == 0) {
        grep_stream(stdin, "(stdin)", pattern, &re, use_regex, ignore_case,
                    show_numbers, 0, &matched_any, &had_error);
    } else {
        for (; i < argc; i++) {
            FILE *fp = fopen(argv[i], "rb");
            if (!fp) {
                print_error("grep: %s: %s", argv[i], strerror(errno));
                had_error = 1;
                continue;
            }
            grep_stream(fp, argv[i], pattern, &re, use_regex, ignore_case,
                        show_numbers, file_count > 1, &matched_any, &had_error);
            fclose(fp);
        }
    }

    if (compiled) regfree(&re);
    if (had_error) return 2;
    return matched_any ? 0 : 1;
}

/* ======================================================================
 * echo / env / exit / help / true / false
 * ==================================================================== */

int builtin_echo(int argc, char **argv) {
    int newline = 1;
    int i = 1;
    if (i < argc && !strcmp(argv[i], "-n")) {
        newline = 0;
        i++;
    }
    for (; i < argc; i++) {
        if (i > 1 && !(i == 2 && !newline)) putchar(' ');
        fputs(argv[i], stdout);
    }
    if (newline) putchar('\n');
    return 0;
}

int builtin_env(int argc, char **argv) {
    (void)argv;
    if (argc != 1) {
        print_usage_error("env");
        return 2;
    }
    extern char **environ;
    if (!environ) return 0;
    for (char **p = environ; *p; p++) printf("%s\n", *p);
    return 0;
}

int builtin_exit(int argc, char **argv) {
    int status = g_last_status;
    if (argc > 2) {
        print_usage_error("exit [status]");
        return 2;
    }
    if (argc == 2) {
        if (!parse_int(argv[1], &status)) {
            print_error("exit: %s: numeric argument required", argv[1]);
            status = 2;
        }
    }
    g_should_exit = 1;
    return status & 0xff;
}

int builtin_true(int argc, char **argv) {
    (void)argc;
    (void)argv;
    return 0;
}

int builtin_false(int argc, char **argv) {
    (void)argc;
    (void)argv;
    return 1;
}

int builtin_help(int argc, char **argv) {
    (void)argc;
    (void)argv;
    printf("MiniShell built-ins\n");
    printf("  File/directory: pwd cd ls mkdir rmdir touch cp mv rm cat\n");
    printf("  Permissions:    chmod MODE file...   ls -l\n");
    printf("  Text:           grep [-i] [-n] [-E] pattern [file...]\n");
    printf("  Variables:      NAME=value, $NAME, ${NAME}, $?, env\n");
    printf("  Control:        cmd1 ; cmd2, cmd1 && cmd2, cmd1 || cmd2\n");
    printf("  Streams:        >  >>  <  2>  2>&1  |\n");
    printf("  Simulators:     sched <algo> [--quantum N] processes.txt\n");
    printf("                  deadlock detect|banker system.txt\n");
    printf("  Other:          echo help true false exit\n");
    printf("\nExamples:\n");
    printf("  ls -l | grep txt\n");
    printf("  echo hello > out.txt\n");
    printf("  sched rr --quantum 4 tests/processes/basic.txt\n");
    printf("  deadlock banker tests/deadlock/safe.txt\n");
    return 0;
}

/* ======================================================================
 * simulator dispatch and built-in table
 * ==================================================================== */

int builtin_sched(int argc, char **argv) {
    return sched_main(argc, argv);
}

int builtin_deadlock(int argc, char **argv) {
    return deadlock_main(argc, argv);
}

static const Builtin g_builtins[] = {
    {"pwd", builtin_pwd},
    {"cd", builtin_cd},
    {"ls", builtin_ls},
    {"mkdir", builtin_mkdir},
    {"rmdir", builtin_rmdir},
    {"touch", builtin_touch},
    {"cp", builtin_cp},
    {"mv", builtin_mv},
    {"rm", builtin_rm},
    {"cat", builtin_cat},
    {"chmod", builtin_chmod},
    {"grep", builtin_grep},
    {"echo", builtin_echo},
    {"env", builtin_env},
    {"exit", builtin_exit},
    {"help", builtin_help},
    {"true", builtin_true},
    {"false", builtin_false},
    {"sched", builtin_sched},
    {"deadlock", builtin_deadlock},
    {NULL, NULL}
};

const Builtin *builtin_find(const char *name) {
    for (const Builtin *b = g_builtins; b->name; b++) {
        if (!strcmp(b->name, name)) return b;
    }
    return NULL;
}
