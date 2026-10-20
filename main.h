#ifndef MAIN_H
#define MAIN_H

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#define Y       "\033[1;33m"
#define G      "\033[1;32m"
#define C      "\033[1;36m" 
#define RED   "\033[1;31m"
#define RESET   "\033[0m"

// Fucntion prototypes
char *GetCwd(char *buffer, size_t size);
void printBanner(void);
#endif
