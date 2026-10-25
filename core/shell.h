#ifndef ZIGUNIOS_SHELL_H
#define ZIGUNIOS_SHELL_H

#define ZU_PATH_MAX 4096

typedef struct ZUShell ZUShell;
typedef char *(*ZUExternalCommand)(void *context, ZUShell *shell, int argc, const char *const argv[]);

struct ZUShell {
    char root[ZU_PATH_MAX];
    char cwd[ZU_PATH_MAX];
    ZUExternalCommand external;
    void *external_context;
};

/* root is the trusted app Documents/root path. */
int zu_shell_init(ZUShell *shell, const char *root);
const char *zu_shell_cwd(const ZUShell *shell);

/* Resolve guest paths inside the app sandbox; rejects symlink traversals. */
int zu_shell_resolve_path(const ZUShell *shell, const char *input,
                          char *guest, char *host);

void zu_shell_set_external(ZUShell *shell, ZUExternalCommand callback, void *context);

/* Returns heap-allocated UTF-8; caller frees. */
char *zu_shell_execute(ZUShell *shell, const char *line, int *clear_screen);

#endif
