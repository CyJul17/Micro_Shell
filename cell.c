#include "cell.h"

char *cell_read_line(void) { 
    char *buffer;
    size_t bufferSize;

    buffer = NULL;

    printf("$>"); // Prompt for user input

    if (getline(&buffer, &bufferSize, stdin) == -1) {
        buffer = NULL;
        if (feof(stdin)) printf(RED"[EOF]"RESET);
        else printf(RED"Getline failed"RESET);

        printf("(%s\n", buffer);
    }
    return buffer;
}

int main (int ac, char **av) {

    char *line;
    //REPL -> Read, Evaulate, Print/Execute -> loop
    while(0xCE77) {

        line = cell_read_line();
        // Get line 



        // Get tokens
    }
    
    return EXIT_SUCCESS;
} 