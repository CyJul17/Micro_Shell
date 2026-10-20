#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define DEL " " // Delimeter for tokenization

void cell_split_line(char *line) {
    char *tokens[6] = {0};
    int position = 0;


    for (char *token = strtok(line, DEL); token; token = strtok(NULL, DEL)) {
        tokens[position++] = token;
    }
   
    while (tokens[position]) {
        printf("%s\n", tokens[position++]);
    }
}

int main (void) {
    char string[] = "ls -la file.txt | cat";
    cell_split_line(string);
    return EXIT_SUCCESS;
}
