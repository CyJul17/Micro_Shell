#include "main.h"
#include "shell_getline.h"

int g_last_status = 0;
volatile sig_atomic_t g_should_exit = 0;

static void print_banner(void) {
    printf("==============================================\n");
    printf(" MiniShell - CmpSc 123 Final Project\n");
    printf(" Shell + CPU Scheduling + Deadlock Simulator\n");
    printf(" Type 'help' for commands, 'exit' to quit.\n");
    printf("==============================================\n");
}

static int run_stream(FILE *fp, int interactive) {
    char *line = NULL;
    size_t cap = 0;
    int status = g_last_status;

    for (;;) {
        if (interactive) {
            fputs(MINISHELL_PROMPT, stdout);
            fflush(stdout);
        }

        ssize_t n = shell_getline(&line, &cap, fp);
        if (n < 0) {
            if (interactive) putchar('\n');
            break;
        }

        status = shell_execute_line(line);
        g_last_status = status;
        if (g_should_exit) break;
    }

    free(line);
    return status;
}

int main(int argc, char **argv) {
    /* The shell survives Ctrl+C; foreground children reset these handlers. */
    signal(SIGINT, SIG_IGN);
    signal(SIGQUIT, SIG_IGN);

    if (argc == 3 && !strcmp(argv[1], "-c")) {
        int status = shell_execute_line(argv[2]);
        g_last_status = status;
        return status & 0xff;
    }

    if (argc == 2 && argv[1][0] != '-') {
        FILE *fp = fopen(argv[1], "r");
        if (!fp) {
            print_error("%s: %s", argv[1], strerror(errno));
            return 1;
        }
        int status = run_stream(fp, 0);
        fclose(fp);
        return status & 0xff;
    }

    if (argc != 1) {
        fprintf(stderr, "usage: %s [-c command] [script]\n", MINISHELL_NAME);
        return EXIT_USAGE;
    }

    print_banner();
    return run_stream(stdin, 1) & 0xff;
}
