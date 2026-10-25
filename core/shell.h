#ifndef ZIGUNIOS_SHELL_H
#define ZIGUNIOS_SHELL_H

#define ZU_PATH_MAX 4096

typedef struct {
    char root[ZU_PATH_MAX];
    char cwd[ZU_PATH_MAX];
} ZUShell;

/* The root is a trusted host directory belonging to the app. */
int zu_shell_init(ZUShell *shell, const char *root);

/* Returns an owned string: caller must free it. clear_screen is set for `clear`. */
char *zu_shell_execute(ZUShell *shell, const char *line, int *clear_screen);
const char *zu_shell_cwd(const ZUShell *shell);

#endif
