#include "shell_getline.h"
#include <stdlib.h>

ssize_t shell_getline(char **lineptr, size_t *n, FILE *stream) {
    if (!lineptr || !n || !stream) return -1;

    if (*lineptr == NULL || *n == 0) {
        *n = 128;
        *lineptr = malloc(*n);
        if (!*lineptr) return -1; 
    }

    size_t len = 0;
    int c;
    while ((c = fgetc(stream)) != EOF) {
        if (len + 2 > *n) {
            size_t newsize = *n * 2;
            char *tmp = realloc(*lineptr, newsize);
            if (!tmp) return -1;
            *lineptr = tmp;
            *n = newsize;
        }
        (*lineptr)[len++] = (char)c;
        if (c == '\n') break;
    }

    if (len == 0 && c == EOF) return -1;
    (*lineptr)[len] = '\0';
    return (ssize_t)len;
}