#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/*
 * Standalone tokenizer demo.  This file is not part of the MiniShell build;
 * the real lexer lives in Shell/parser.c.  It is kept as a small, correct
 * example of splitting a command line on whitespace.
 */
static void cell_split_line(char *line) {
    char *token = strtok(line, " \t");
    while (token != NULL) {
        printf("%s\n", token);
        token = strtok(NULL, " \t");
    }
}

int main(void) {
    char string[] = "ls -la file.txt | cat";
    cell_split_line(string);
    return EXIT_SUCCESS;
}
