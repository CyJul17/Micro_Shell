#include "main.h"

/* ---------- allocation helpers ---------- */

void *xmalloc(size_t size) {
    void *p = malloc(size ? size : 1);
    if (!p) {
        fprintf(stderr, "minishell: out of memory\n");
        exit(EXIT_FAILURE);
    }
    return p;
}

void *xrealloc(void *ptr, size_t size) {
    void *p = realloc(ptr, size ? size : 1);
    if (!p) {
        fprintf(stderr, "minishell: out of memory\n");
        exit(EXIT_FAILURE);
    }
    return p;
}

char *xstrdup(const char *s) {
    if (!s) return NULL;
    size_t n = strlen(s) + 1;
    char *p = xmalloc(n);
    memcpy(p, s, n);
    return p;
}

char *xstrndup(const char *s, size_t n) {
    char *p = xmalloc(n + 1);
    memcpy(p, s, n);
    p[n] = '\0';
    return p;
}

char *xstrfmt(const char *fmt, ...) {
    va_list ap;
    va_list copy;
    int n;

    va_start(ap, fmt);
    va_copy(copy, ap);
    n = vsnprintf(NULL, 0, fmt, ap);
    va_end(ap);
    if (n < 0) {
        va_end(copy);
        return xstrdup("format error");
    }
    char *buf = xmalloc((size_t)n + 1);
    vsnprintf(buf, (size_t)n + 1, fmt, copy);
    va_end(copy);
    return buf;
}

/* ---------- string vectors ---------- */

void strvec_init(StrVec *v) {
    v->items = NULL;
    v->len = 0;
    v->cap = 0;
}

void strvec_push(StrVec *v, char *s) {
    if (v->len + 2 > v->cap) {
        size_t newcap = v->cap ? v->cap * 2 : 8;
        while (newcap < v->len + 2) newcap *= 2;
        v->items = xrealloc(v->items, newcap * sizeof(*v->items));
        v->cap = newcap;
    }
    v->items[v->len++] = s;
    v->items[v->len] = NULL;
}

void strvec_free(StrVec *v) {
    if (!v) return;
    for (size_t i = 0; i < v->len; i++) free(v->items[i]);
    free(v->items);
    v->items = NULL;
    v->len = 0;
    v->cap = 0;
}

char *trim(char *s) {
    if (!s) return s;
    while (*s && isspace((unsigned char)*s)) s++;
    char *end = s + strlen(s);
    while (end > s && isspace((unsigned char)end[-1])) end--;
    *end = '\0';
    return s;
}

int parse_int(const char *s, int *out) {
    if (!s || !*s || !out) return 0;
    errno = 0;
    char *end = NULL;
    long v = strtol(s, &end, 10);
    if (errno != 0 || end == s || *end != '\0') return 0;
    if (v < INT_MIN || v > INT_MAX) return 0;
    *out = (int)v;
    return 1;
}

int is_assignment(const char *s, char **name, char **value) {
    if (!s || !*s) return 0;
    const char *eq = strchr(s, '=');
    if (!eq || eq == s) return 0;
    if (!(isalpha((unsigned char)s[0]) || s[0] == '_')) return 0;
    for (const char *p = s + 1; p < eq; p++) {
        if (!(isalnum((unsigned char)*p) || *p == '_')) return 0;
    }
    if (name) *name = xstrndup(s, (size_t)(eq - s));
    if (value) *value = xstrdup(eq + 1);
    return 1;
}

void print_error(const char *fmt, ...) {
    va_list ap;
    fputs("minishell: ", stderr);
    va_start(ap, fmt);
    vfprintf(stderr, fmt, ap);
    va_end(ap);
    fputc('\n', stderr);
}

void print_usage_error(const char *usage) {
    fprintf(stderr, "minishell: usage: %s\n", usage);
}

/* ---------- paths ---------- */

char *path_join(const char *dir, const char *name) {
    if (!dir || !*dir) return xstrdup(name ? name : "");
    if (!name || !*name) return xstrdup(dir);
    size_t dl = strlen(dir);
    size_t nl = strlen(name);
    int slash = dir[dl - 1] != '/';
    char *out = xmalloc(dl + nl + (size_t)slash + 1);
    memcpy(out, dir, dl);
    size_t pos = dl;
    if (slash) out[pos++] = '/';
    memcpy(out + pos, name, nl + 1);
    return out;
}

char *path_basename(const char *path) {
    if (!path || !*path) return xstrdup(".");
    char *copy = xstrdup(path);
    size_t len = strlen(copy);
    while (len > 1 && copy[len - 1] == '/') copy[--len] = '\0';
    char *slash = strrchr(copy, '/');
    char *base = slash ? slash + 1 : copy;
    char *out = xstrdup(*base ? base : "/");
    free(copy);
    return out;
}

int path_exists(const char *path) {
    struct stat st;
    return lstat(path, &st) == 0;
}

int is_directory(const char *path) {
    struct stat st;
    return stat(path, &st) == 0 && S_ISDIR(st.st_mode);
}

/* ---------- file operations ---------- */

int copy_file(const char *src, const char *dst) {
    int in = -1;
    int out = -1;
    int rc = 1;
    struct stat st;

    in = open(src, O_RDONLY);
    if (in < 0) {
        print_error("cp: %s: %s", src, strerror(errno));
        return 1;
    }
    if (fstat(in, &st) < 0) {
        print_error("cp: %s: %s", src, strerror(errno));
        goto done;
    }
    if (!S_ISREG(st.st_mode)) {
        print_error("cp: %s: not a regular file", src);
        goto done;
    }

    out = open(dst, O_WRONLY | O_CREAT | O_TRUNC, st.st_mode & 07777);
    if (out < 0) {
        print_error("cp: %s: %s", dst, strerror(errno));
        goto done;
    }

    char buf[8192];
    for (;;) {
        ssize_t n = read(in, buf, sizeof(buf));
        if (n < 0) {
            if (errno == EINTR) continue;
            print_error("cp: %s: %s", src, strerror(errno));
            goto done;
        }
        if (n == 0) break;
        ssize_t written = 0;
        while (written < n) {
            ssize_t w = write(out, buf + written, (size_t)(n - written));
            if (w < 0) {
                if (errno == EINTR) continue;
                print_error("cp: %s: %s", dst, strerror(errno));
                goto done;
            }
            written += w;
        }
    }

    if (fchmod(out, st.st_mode & 07777) < 0) {
        /* Not fatal on filesystems that do not support permissions. */
    }
    rc = 0;

done:
    if (in >= 0) close(in);
    if (out >= 0) close(out);
    return rc;
}

int copy_path(const char *src, const char *dst, int recursive) {
    struct stat st;
    char *target = NULL;
    int rc = 1;

    if (lstat(src, &st) < 0) {
        print_error("cp: %s: %s", src, strerror(errno));
        return 1;
    }

    if (is_directory(dst)) {
        char *base = path_basename(src);
        target = path_join(dst, base);
        free(base);
    } else {
        target = xstrdup(dst);
    }

    if (S_ISDIR(st.st_mode)) {
        if (!recursive) {
            print_error("cp: %s: is a directory (use -r)", src);
            goto done;
        }
        if (mkdir(target, 0777) < 0) {
            if (errno != EEXIST || !is_directory(target)) {
                print_error("cp: %s: %s", target, strerror(errno));
                goto done;
            }
        }

        DIR *dir = opendir(src);
        if (!dir) {
            print_error("cp: %s: %s", src, strerror(errno));
            goto done;
        }
        int status = 0;
        struct dirent *ent;
        while ((ent = readdir(dir)) != NULL) {
            if (!strcmp(ent->d_name, ".") || !strcmp(ent->d_name, ".."))
                continue;
            char *child_src = path_join(src, ent->d_name);
            char *child_dst = path_join(target, ent->d_name);
            if (copy_path(child_src, child_dst, recursive) != 0) status = 1;
            free(child_src);
            free(child_dst);
        }
        closedir(dir);
        rc = status;
    } else if (S_ISLNK(st.st_mode)) {
        char linkbuf[PATH_MAX];
        ssize_t n = readlink(src, linkbuf, sizeof(linkbuf) - 1);
        if (n < 0) {
            print_error("cp: %s: %s", src, strerror(errno));
            goto done;
        }
        linkbuf[n] = '\0';
        unlink(target);
        if (symlink(linkbuf, target) < 0) {
            print_error("cp: %s: %s", target, strerror(errno));
            goto done;
        }
        rc = 0;
    } else if (S_ISREG(st.st_mode)) {
        rc = copy_file(src, target);
    } else {
        print_error("cp: %s: unsupported file type", src);
    }

done:
    free(target);
    return rc;
}

int remove_path(const char *path, int recursive, int force) {
    struct stat st;

    if (!strcmp(path, "/") || !strcmp(path, ".") || !strcmp(path, "..")) {
        print_error("rm: refusing to remove '%s'", path);
        return 1;
    }

    if (lstat(path, &st) < 0) {
        if (errno == ENOENT && force) return 0;
        print_error("rm: %s: %s", path, strerror(errno));
        return 1;
    }

    if (S_ISDIR(st.st_mode)) {
        if (!recursive) {
            print_error("rm: %s: is a directory (use -r)", path);
            return 1;
        }
        DIR *dir = opendir(path);
        if (!dir) {
            print_error("rm: %s: %s", path, strerror(errno));
            return 1;
        }
        int status = 0;
        struct dirent *ent;
        while ((ent = readdir(dir)) != NULL) {
            if (!strcmp(ent->d_name, ".") || !strcmp(ent->d_name, ".."))
                continue;
            char *child = path_join(path, ent->d_name);
            if (remove_path(child, recursive, force) != 0) status = 1;
            free(child);
        }
        closedir(dir);
        if (rmdir(path) < 0) {
            if (!(force && errno == ENOENT)) {
                print_error("rm: %s: %s", path, strerror(errno));
                status = 1;
            }
        }
        return status;
    }

    if (unlink(path) < 0) {
        if (force && errno == ENOENT) return 0;
        print_error("rm: %s: %s", path, strerror(errno));
        return 1;
    }
    return 0;
}

int mkdir_p(const char *path) {
    char *copy = xstrdup(path);
    size_t len = strlen(copy);
    int status = 0;

    while (len > 1 && copy[len - 1] == '/') copy[--len] = '\0';
    if (len == 0) {
        free(copy);
        print_error("mkdir: empty path");
        return 1;
    }

    for (size_t i = 1; i < len; i++) {
        if (copy[i] != '/') continue;
        copy[i] = '\0';
        if (!path_exists(copy)) {
            if (mkdir(copy, 0777) < 0) {
                print_error("mkdir: %s: %s", copy, strerror(errno));
                status = 1;
            }
        } else if (!is_directory(copy)) {
            print_error("mkdir: %s: not a directory", copy);
            status = 1;
        }
        copy[i] = '/';
        if (status) break;
    }

    if (!status) {
        if (!path_exists(copy)) {
            if (mkdir(copy, 0777) < 0) {
                print_error("mkdir: %s: %s", copy, strerror(errno));
                status = 1;
            }
        } else if (!is_directory(copy)) {
            print_error("mkdir: %s: not a directory", copy);
            status = 1;
        }
    }

    free(copy);
    return status;
}

int expand_tilde(const char *arg, char **out) {
    if (!arg || !out) return 0;
    if (arg[0] != '~' || (arg[1] != '\0' && arg[1] != '/')) {
        *out = xstrdup(arg);
        return 0;
    }
    const char *home = getenv("HOME");
    if (!home || !*home) {
        *out = xstrdup(arg);
        return 0;
    }
    if (arg[1] == '\0') {
        *out = xstrdup(home);
        return 1;
    }
    size_t hl = strlen(home);
    size_t al = strlen(arg + 1);
    char *result = xmalloc(hl + al + 1);
    memcpy(result, home, hl);
    memcpy(result + hl, arg + 1, al + 1);
    *out = result;
    return 1;
}

void format_mode(mode_t mode, char *buf) {
    if (S_ISDIR(mode)) buf[0] = 'd';
    else if (S_ISLNK(mode)) buf[0] = 'l';
    else if (S_ISCHR(mode)) buf[0] = 'c';
    else if (S_ISBLK(mode)) buf[0] = 'b';
    else if (S_ISFIFO(mode)) buf[0] = 'p';
    else if (S_ISSOCK(mode)) buf[0] = 's';
    else buf[0] = '-';

    buf[1] = (mode & S_IRUSR) ? 'r' : '-';
    buf[2] = (mode & S_IWUSR) ? 'w' : '-';
    buf[3] = (mode & S_IXUSR) ? ((mode & S_ISUID) ? 's' : 'x')
                              : ((mode & S_ISUID) ? 'S' : '-');
    buf[4] = (mode & S_IRGRP) ? 'r' : '-';
    buf[5] = (mode & S_IWGRP) ? 'w' : '-';
    buf[6] = (mode & S_IXGRP) ? ((mode & S_ISGID) ? 's' : 'x')
                              : ((mode & S_ISGID) ? 'S' : '-');
    buf[7] = (mode & S_IROTH) ? 'r' : '-';
    buf[8] = (mode & S_IWOTH) ? 'w' : '-';
    buf[9] = (mode & S_IXOTH) ? ((mode & S_ISVTX) ? 't' : 'x')
                              : ((mode & S_ISVTX) ? 'T' : '-');
    buf[10] = '\0';
}
