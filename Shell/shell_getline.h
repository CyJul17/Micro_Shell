#ifndef SHELL_GETLINE_H
#define SHELL_GETLINE_H

#include <stdio.h>
#include <stddef.h>
#include <sys/types.h>

/*
 * Read one line from stream, growing *lineptr as needed.
 * Returns the number of bytes read (excluding the terminating NUL),
 * or -1 on EOF/error.
 */
ssize_t shell_getline(char **lineptr, size_t *n, FILE *stream);

#endif /* SHELL_GETLINE_H */
