#ifndef MAIN_H
#define MAIN_H

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>

#define Y       "\033[1;33m"
#define G      "\033[1;32m"
#define C      "\033[1;36m" 
#define RED   "\033[1;31m"
#define RESET   "\033[0m"

#define DEL "\n\t \v\f\r"
#define SHELL_CHILD 0 
#define EX_UNAVAILABLE 69
#define EX_OSERR 71

typedef struct s_buildin {
    const char *builtin_name;
    int (*foo)(char **);
} t_builtin;

// Fucntion prototypes
void shell_exit(char **);
char *GetCwd(char *buffer, size_t size);
void *Malloc(size_t size);
void *Realloc(void *ptr, size_t size);
char **shell_split_line(char * line);
char *shell_read_line(void);
void printBanner(void);
#endif
