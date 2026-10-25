#define _POSIX_C_SOURCE 200809L
#include "../core/shell.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int main(int argc, char **argv)
{
    ZUShell shell;
    if (!zu_shell_init(&shell, argc > 1 ? argv[1] : "./zigunios-root")) {
        fprintf(stderr, "failed to initialize shell root\n");
        return 1;
    }
    char *line = NULL;
    size_t capacity = 0;
    while (1) {
        printf("%s $ ", zu_shell_cwd(&shell));
        fflush(stdout);
        if (getline(&line, &capacity, stdin) < 0) break;
        if (!strcmp(line, "exit\n")) break;
        int clear = 0;
        char *output = zu_shell_execute(&shell, line, &clear);
        if (clear) printf("\033[2J\033[H");
        if (output) { fputs(output, stdout); free(output); }
    }
    free(line);
    return 0;
}
