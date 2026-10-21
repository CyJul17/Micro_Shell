#include "main.h"

// Wrappers

char *GetCwd(char *buffer, size_t size) {
    if (NULL == getcwd(buffer,size )) {
        perror(RED"getcwd() error"RESET);
    }
    return buffer;
}

void *Malloc(size_t size) {
    void *ptr; 

    if (size == 0) return (NULL);
    ptr = malloc(size);
    if (!ptr) {
        perror(RED"Malloc Failed" RESET);
        exit(EXIT_FAILURE);
    }
    return ptr;  
    
}

void *Realloc(void *ptr, size_t size) {
    void *new_ptr;
    
    new_ptr = realloc(ptr, size);
    if (!new_ptr) {
        perror(RED"Realloc Failed" RESET);
        exit(EXIT_FAILURE);
    }
    return (new_ptr);
}

pid_t Fork(void) {
    pid_t pid;
    pid = fork();
    if (pid < 0) {
        perror(RED"Fork failed" RESET);
        exit(EX_OSERR);
    }
    return pid;
}

void Execv(const char *file, char *const argv[]) {

    if (!file || !argv) {
        fprintf(stderr, RED"Execvp: invalid argument\n"RESET);
        exit(EXIT_FAILURE);
    }
    if (execvp(file, argv) == -1) {
        perror(RED"Child failed"RESET);
        exit(EX_UNAVAILABLE);
    }
}

pid_t Wait(int *status) {
    pid_t result; 
    if (!status) {
        fprintf(stderr, RED"Wait: status argument required\n"RESET);
        return(-1);
    }
    result = wait(status);
    if (result == -1) {
        perror(RED"Wait failed"RESET); 
    }
    if (WIFEXITED(*status)) {
        *status = WEXITSTATUS(*status);
    }
    return(result);
}

void printBanner(void) {
    printf(C" ____                                   __\n"
            "/\\  _`\\               __               /\\ \\__     \n"
            "\\ \\ \\L\\ \\_ __   ___  /\\_\\     __    ___\\ \\ ,_\\    \n"
            " \\ \\ ,__/\\`'__\\/ __`\\\\/\\ \\  /'__`\\ /'___\\ \\ \\/    \n"
                "\\ \\ \\/\\ \\ \\//\\ \\L\\ \\\\ \\ \\/\\  __//\\ \\__/\\ \\ \\_   \n"
                 "\\ \\_\\ \\ \\_\\\\ \\____/_\\ \\ \\ \\____\\ \\____\\\\ \\__\\  \n"
                " \\/_/  \\/_/ \\/___//\\ \\_\\ \\/____/\\/____/ \\/__/  \n"
                                "\\ \\____/  \n"
                                "\\/___/  \n"
            "______          ___             __        \n"
            "/\\__  _\\        /\\_ \\           /\\ \\     \n"
            "\\/_/\\ \\/    __  \\//\\ \\      __  \\ \\ \\____     __   \n"
            "\\ \\ \\  /'__`\\  \\ \\ \\   /'__`\\ \\ \\ '__`\\  /'__`\\  \n"
                "\\ \\ \\/\\ \\L\\.\\_ \\_\\ \\_/\\ \\L\\.\\_\\ \\ \\L\\ \\/\\ \\L\\.\\_ \n"
                "\\ \\_\\ \\__/\\.\\_\\/\\____\\ \\__/\\.\\_\\\\ \\_,__/\\ \\__/\\.\\_\\ \n"
                "\\/_/\\/_/\\/_/\\/____/\\/__/\\/_/ \\/___/  \\/__/\\/_/ \n"
                 "                                               \n" RESET);
}