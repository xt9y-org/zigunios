#define _POSIX_C_SOURCE 200809L
#include "shell.h"

#include <dirent.h>
#include <errno.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

#define ZU_WORDS 64
#define ZU_OUTPUT_MAX (64 * 1024)

typedef struct {
    char *data;
    size_t length;
} Output;

static void say(Output *out, const char *format, ...)
{
    if (out->length >= ZU_OUTPUT_MAX - 1) return;
    va_list args;
    va_start(args, format);
    int n = vsnprintf(out->data + out->length, ZU_OUTPUT_MAX - out->length, format, args);
    va_end(args);
    if (n > 0) {
        size_t remaining = ZU_OUTPUT_MAX - out->length - 1;
        out->length += (size_t)n < remaining ? (size_t)n : remaining;
    }
}

static int directory(const char *path)
{
    if (mkdir(path, 0755) == 0 || errno == EEXIST) {
        struct stat st;
        return stat(path, &st) == 0 && S_ISDIR(st.st_mode);
    }
    return 0;
}

int zu_shell_init(ZUShell *shell, const char *root)
{
    if (!shell || !root || !root[0] || strlen(root) >= sizeof(shell->root) - 32) return 0;
    memset(shell, 0, sizeof(*shell));
    snprintf(shell->root, sizeof(shell->root), "%s", root);
    size_t len = strlen(shell->root);
    while (len > 1 && shell->root[len - 1] == '/') shell->root[--len] = 0;
    if (!directory(shell->root)) return 0;
    char path[ZU_PATH_MAX];
    strcpy(path, shell->root); strcat(path, "/home");
    if (!directory(path)) return 0;
    strcpy(path, shell->root); strcat(path, "/home/projects");
    if (!directory(path)) return 0;
    strcpy(path, shell->root); strcat(path, "/tmp");
    if (!directory(path)) return 0;
    strcpy(shell->cwd, "/home");
    return 1;
}

const char *zu_shell_cwd(const ZUShell *shell)
{
    return shell->cwd;
}

void zu_shell_set_external(ZUShell *shell, ZUExternalCommand callback, void *context)
{
    if (!shell) return;
    shell->external = callback;
    shell->external_context = context;
}

/* Normalize .. in the virtual filesystem, never allowing escape above /. */
static int resolve(const ZUShell *shell, const char *input, char *guest, char *host)
{
    char combined[ZU_PATH_MAX], temp[ZU_PATH_MAX];
    if (!input || !input[0]) input = ".";
    int n = input[0] == '/' ? snprintf(combined, sizeof(combined), "%s", input)
                             : snprintf(combined, sizeof(combined), "%s/%s", shell->cwd, input);
    if (n < 0 || (size_t)n >= sizeof(combined)) return 0;
    strcpy(temp, combined);
    guest[0] = '/'; guest[1] = 0;
    char *save = NULL;
    for (char *part = strtok_r(temp, "/", &save); part; part = strtok_r(NULL, "/", &save)) {
        if (!strcmp(part, ".")) continue;
        if (!strcmp(part, "..")) {
            size_t len = strlen(guest);
            if (len > 1) {
                char *slash = strrchr(guest, '/');
                if (slash == guest) guest[1] = 0;
                else if (slash) *slash = 0;
            }
            continue;
        }
        size_t len = strlen(guest), add = strlen(part);
        if (len + add + 2 >= ZU_PATH_MAX) return 0;
        if (len > 1) strcat(guest, "/");
        strcat(guest, part);
    }
    n = snprintf(host, ZU_PATH_MAX, "%s%s", shell->root, guest);
    if (n < 0 || n >= ZU_PATH_MAX) return 0;
    /* Reject all filesystem symlinks, including intermediate components. */
    char partial[ZU_PATH_MAX];
    strcpy(partial, shell->root);
    const char *p = guest;
    while (*p) {
        while (*p == '/') p++;
        if (!*p) break;
        const char *end = strchr(p, '/');
        size_t amount = end ? (size_t)(end - p) : strlen(p);
        size_t len = strlen(partial);
        if (len + amount + 2 >= sizeof(partial)) return 0;
        partial[len++] = '/';
        memcpy(partial + len, p, amount);
        partial[len + amount] = 0;
        struct stat st;
        if (lstat(partial, &st) == 0 && S_ISLNK(st.st_mode)) return 0;
        p += amount;
    }
    return 1;
}

int zu_shell_resolve_path(const ZUShell *shell, const char *input,
                          char *guest, char *host)
{
    return shell && guest && host && resolve(shell, input, guest, host);
}

/* Tokens: whitespace, quotes and simple escapes. No pipes/expansion yet. */
static int tokenize(const char *line, char words[ZU_WORDS][ZU_PATH_MAX], Output *out)
{
    int count = 0;
    while (*line) {
        while (*line == ' ' || *line == '\t') line++;
        if (!*line || *line == '\n') break;
        if (count == ZU_WORDS) { say(out, "too many arguments\n"); return -1; }
        size_t at = 0;
        char quote = 0;
        int active = 0;
        while (*line) {
            char c = *line;
            if (!quote && (c == ' ' || c == '\t' || c == '\n')) break;
            active = 1;
            if (c == '\\' && quote != '\'' && line[1]) { line++; c = *line++; }
            else if (c == '\'' || c == '"') {
                if (!quote) { quote = c; line++; continue; }
                if (quote == c) { quote = 0; line++; continue; }
                line++;
            } else line++;
            if (at + 1 >= ZU_PATH_MAX) { say(out, "argument too long\n"); return -1; }
            words[count][at++] = c;
        }
        if (quote) { say(out, "unterminated quote\n"); return -1; }
        if (active) words[count++][at] = 0;
    }
    return count;
}

static int path_for(ZUShell *shell, const char *arg, char *guest, char *host, Output *out)
{
    if (resolve(shell, arg, guest, host)) return 1;
    say(out, "%s: invalid or unsafe path\n", arg);
    return 0;
}

char *zu_shell_execute(ZUShell *shell, const char *line, int *clear_screen)
{
    if (clear_screen) *clear_screen = 0;
    Output out = {.data = calloc(ZU_OUTPUT_MAX, 1), .length = 0};
    if (!out.data) return NULL;
    if (!shell || !line) return out.data;
    char (*args)[ZU_PATH_MAX] = calloc(ZU_WORDS, ZU_PATH_MAX);
    if (!args) { say(&out, "out of memory\n"); return out.data; }
    int argc = tokenize(line, args, &out);
    if (argc <= 0) { free(args); return out.data; }
    const char *cmd = args[0];
    char guest[ZU_PATH_MAX], host[ZU_PATH_MAX];

    if (!strcmp(cmd, "help")) {
        say(&out, "Zigunios v0.4 — iPad workspace\n"
                  "  pwd cd ls mkdir touch cat echo rm rmdir cp mv clear\n"
                  "  grep head wc uname whoami date help\n"
                  "  git clone --branch/--depth, submodule update, status\n"
                  "  project inspect; setup check/deps; wasm FILE.wasm\n"
                  "  echo text > file   (or >> to append)\n"
                  "Zig CC / CMake / arbitrary POSIX processes are not yet available.\n");
    } else if (!strcmp(cmd, "pwd")) {
        say(&out, "%s\n", shell->cwd);
    } else if (!strcmp(cmd, "cd")) {
        const char *arg = argc > 1 ? args[1] : "/home";
        if (path_for(shell, arg, guest, host, &out)) {
            struct stat st;
            if (stat(host, &st) == 0 && S_ISDIR(st.st_mode)) strcpy(shell->cwd, guest);
            else say(&out, "cd: %s: not a directory\n", arg);
        }
    } else if (!strcmp(cmd, "ls")) {
        const char *arg = ".";
        int all = 0;
        for (int i = 1; i < argc; i++) {
            if (!strcmp(args[i], "-a")) all = 1;
            else arg = args[i];
        }
        if (path_for(shell, arg, guest, host, &out)) {
            DIR *dir = opendir(host);
            if (!dir) say(&out, "ls: %s: %s\n", arg, strerror(errno));
            else {
                struct dirent *entry;
                while ((entry = readdir(dir))) {
                    if (!all && entry->d_name[0] == '.') continue;
                    char item[ZU_PATH_MAX];
                    int n = snprintf(item, sizeof(item), "%s/%s", host, entry->d_name);
                    struct stat st;
                    int is_dir = n > 0 && n < (int)sizeof(item) && lstat(item, &st) == 0 && S_ISDIR(st.st_mode);
                    say(&out, "%s%s\n", entry->d_name, is_dir ? "/" : "");
                }
                closedir(dir);
            }
        }
    } else if (!strcmp(cmd, "mkdir") || !strcmp(cmd, "touch") || !strcmp(cmd, "rm") || !strcmp(cmd, "rmdir")) {
        if (argc == 1) say(&out, "%s: missing path\n", cmd);
        for (int i = 1; i < argc; i++) {
            if (!path_for(shell, args[i], guest, host, &out)) continue;
            if (!strcmp(guest, "/")) { say(&out, "%s: cannot modify /\n", cmd); continue; }
            int rc = 0;
            if (!strcmp(cmd, "mkdir")) rc = mkdir(host, 0755);
            else if (!strcmp(cmd, "touch")) { FILE *file = fopen(host, "ab"); if (file) fclose(file); else rc = -1; }
            else if (!strcmp(cmd, "rm")) rc = unlink(host);
            else rc = rmdir(host);
            if (rc != 0) say(&out, "%s: %s: %s\n", cmd, args[i], strerror(errno));
        }
    } else if (!strcmp(cmd, "cat")) {
        if (argc == 1) say(&out, "cat: missing path\n");
        for (int i = 1; i < argc; i++) {
            if (!path_for(shell, args[i], guest, host, &out)) continue;
            FILE *file = fopen(host, "rb");
            if (!file) { say(&out, "cat: %s: %s\n", args[i], strerror(errno)); continue; }
            char buffer[1025];
            size_t n;
            while ((n = fread(buffer, 1, sizeof(buffer) - 1, file)) > 0 && out.length < ZU_OUTPUT_MAX - 1025) {
                buffer[n] = 0;
                say(&out, "%s", buffer);
            }
            fclose(file);
            if (out.length && out.data[out.length - 1] != '\n') say(&out, "\n");
        }
    } else if (!strcmp(cmd, "echo")) {
        int redirect = -1, append = 0;
        for (int i = 1; i < argc; i++) {
            if (!strcmp(args[i], ">") || !strcmp(args[i], ">>")) {
                redirect = i; append = args[i][1] == '>'; break;
            }
        }
        char message[ZU_OUTPUT_MAX]; size_t at = 0;
        int end = redirect == -1 ? argc : redirect;
        for (int i = 1; i < end; i++) {
            int n = snprintf(message + at, sizeof(message) - at, "%s%s", i == 1 ? "" : " ", args[i]);
            if (n < 0 || (size_t)n >= sizeof(message) - at) break;
            at += (size_t)n;
        }
        message[at] = 0;
        if (redirect == -1) say(&out, "%s\n", message);
        else if (redirect + 1 >= argc) say(&out, "echo: missing redirection path\n");
        else if (path_for(shell, args[redirect + 1], guest, host, &out)) {
            FILE *file = fopen(host, append ? "ab" : "wb");
            if (!file) say(&out, "echo: %s\n", strerror(errno));
            else { fprintf(file, "%s\n", message); fclose(file); }
        }
    } else if (!strcmp(cmd, "cp") || !strcmp(cmd, "mv")) {
        if (argc != 3) say(&out, "%s: expected source and destination\n", cmd);
        else {
            char from_guest[ZU_PATH_MAX], from_host[ZU_PATH_MAX];
            if (path_for(shell, args[1], from_guest, from_host, &out) &&
                path_for(shell, args[2], guest, host, &out)) {
                struct stat st;
                if (lstat(from_host, &st) != 0 || !S_ISREG(st.st_mode)) {
                    say(&out, "%s: source must be a regular file\n", cmd);
                } else if (!strcmp(cmd, "mv")) {
                    if (rename(from_host, host) != 0) say(&out, "mv: %s\n", strerror(errno));
                } else {
                    FILE *source = fopen(from_host, "rb");
                    FILE *dest = source ? fopen(host, "wb") : NULL;
                    if (!dest) say(&out, "cp: %s\n", strerror(errno));
                    else {
                        char buf[8192]; size_t size; int ok = 1;
                        while ((size = fread(buf, 1, sizeof(buf), source)) > 0)
                            if (fwrite(buf, 1, size, dest) != size) { ok = 0; break; }
                        if (ferror(source)) ok = 0;
                        if (fclose(dest) != 0) ok = 0;
                        if (!ok) say(&out, "cp: I/O error\n");
                    }
                    if (source) fclose(source);
                }
            }
        }
    } else if (!strcmp(cmd, "head") || !strcmp(cmd, "grep") || !strcmp(cmd, "wc")) {
        const char *path = NULL;
        const char *needle = NULL;
        int max_lines = 10;
        if (!strcmp(cmd, "grep")) {
            if (argc != 3) say(&out, "usage: grep PATTERN FILE\n");
            else { needle = args[1]; path = args[2]; }
        } else if (!strcmp(cmd, "head")) {
            if (argc == 2) path = args[1];
            else if (argc == 4 && !strcmp(args[1], "-n")) {
                max_lines = atoi(args[2]);
                path = args[3];
            } else say(&out, "usage: head [-n NUMBER] FILE\n");
            if (max_lines < 0 || max_lines > 10000) path = NULL;
        } else {
            if (argc == 2) path = args[1];
            else say(&out, "usage: wc FILE\n");
        }
        if (path && path_for(shell, path, guest, host, &out)) {
            FILE *file = fopen(host, "rb");
            if (!file) say(&out, "%s: %s\n", cmd, strerror(errno));
            else {
                if (!strcmp(cmd, "wc")) {
                    unsigned long long bytes = 0, lines = 0, words = 0;
                    int prev_space = 1, c;
                    while ((c = fgetc(file)) != EOF) {
                        bytes++; if (c == '\n') lines++;
                        int space = c == ' ' || c == '\n' || c == '\t' || c == '\r';
                        if (!space && prev_space) words++;
                        prev_space = space;
                    }
                    say(&out, "%llu %llu %llu %s\n", lines, words, bytes, path);
                } else {
                    char *line_buf = NULL;
                    size_t capacity = 0;
                    ssize_t length; int shown = 0;
                    while ((length = getline(&line_buf, &capacity, file)) != -1) {
                        if (!strcmp(cmd, "grep") && !strstr(line_buf, needle)) continue;
                        if (!strcmp(cmd, "head") && shown >= max_lines) break;
                        if (out.length + (size_t)length >= ZU_OUTPUT_MAX - 128) {
                            say(&out, "\n(output truncated)\n"); break;
                        }
                        say(&out, "%s", line_buf);
                        shown++;
                    }
                    free(line_buf);
                }
                fclose(file);
            }
        }
    } else if (!strcmp(cmd, "clear")) {
        if (clear_screen) *clear_screen = 1;
    } else if (!strcmp(cmd, "uname")) {
        say(&out, "Zigunios (iPadOS hosted shell)\n");
    } else if (!strcmp(cmd, "whoami")) {
        say(&out, "user\n");
    } else if (!strcmp(cmd, "date")) {
        time_t now = time(NULL);
        char formatted[64];
        struct tm tm_value;
        if (localtime_r(&now, &tm_value) && strftime(formatted, sizeof(formatted), "%Y-%m-%d %H:%M:%S %Z", &tm_value))
            say(&out, "%s\n", formatted);
    } else if ((!strcmp(cmd, "git") || !strcmp(cmd, "wasm") || !strcmp(cmd, "project") || !strcmp(cmd, "setup")) && shell->external) {
        const char *argv[ZU_WORDS];
        for (int i = 0; i < argc; i++) argv[i] = args[i];
        char *answer = shell->external(shell->external_context, shell, argc, argv);
        free(args);
        free(out.data);
        return answer ? answer : strdup("tool: out of memory or internal error\n");
    } else if (!strcmp(cmd, "zig") || !strcmp(cmd, "c") || !strcmp(cmd, "make") || !strcmp(cmd, "cmake")) {
        say(&out, "%s: local compiler/process runtime is not implemented\n", cmd);
    } else {
        say(&out, "%s: command not found\n", cmd);
    }
    free(args);
    return out.data;
}
