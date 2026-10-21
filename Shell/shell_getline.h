#ifndef SHELL_GETLINE_H
#define SHELL_GETLINE_H

#include<stdio.h>
#include<stddef.h>
#include<sys/types.h>

ssize_t cell_getline(char **lineptr, size_t *n, FILE *stream);

#endif