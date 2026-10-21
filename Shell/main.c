
#include "main.h"
#include "shell_getline.h"

t_builtin g_builtin[] = {
    {.builtin_name = "echo", .foo = shell_echo};
    {.builtin_name = "env", .foo = shell_env};
    {.builtin_name = "exit", .foo = shell_exit},
    {.buildin_name = NULL};
    
};

int status = 0;

void shell_launch(char **args) {

    if (Fork() == SHELL_CHILD) {
        Execvp(args[0], args);
    } else {
        Wait(&status);
    }
}

void shell_exec(char **args) {
    int i;
    const char *curr;
     while ((curr = g_builtin[i].builtin_name)) {
        if (!strcmp(curr,args[0])) {
             status = g_builtin[i].foo(args);
             return;
        } ++i;
     }
     shell_launch(args);
}

char **shell_split_line(char *line) {
    char **tokens;
    unsigned int position;
    size_t bufsize = BUFSIZ;

    tokens = Malloc(bufsize * sizeof  *tokens); 
    position = 0;

    for (char *token = strtok(line, DEL); token; token = strtok(NULL, DEL)) {
        tokens[position++] = token; 
        if (position >= bufsize) {
            bufsize *= 2;
            tokens = Realloc(tokens, bufsize * sizeof(*tokens));
        }
    }
    tokens[position] = NULL;
    return tokens;
}

char *shell_read_line(void) {
    char *buffer = NULL;
    size_t bufferSize = 0;
    char cwd[BUFSIZ];

    GetCwd(cwd, sizeof(cwd));

    printf(C"%s" RESET "Talaba $> ", cwd);

    if (shell_getline(&buffer, &bufferSize, stdin) == -1) {
        free(buffer);
        buffer = NULL;

        if (feof(stdin)) {
            printf(RED "[End of File]" RESET "\n");
        } else {
            printf(RED "Getline failed" RESET  "\n");
        }
    }
    return buffer;
}

int main() {
    char *line;
    char **args;
    printBanner();

    //REPL -> Read Evaluate Print -> Loop
    while((line = shell_read_line())) {
       args = shell_split_line(line); 
       cell_exec(args); 
       free(args);
       free(line);
    }
    return EXIT_SUCCESS; 
}