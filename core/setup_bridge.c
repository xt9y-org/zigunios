#define _POSIX_C_SOURCE 200809L
#include "setup_bridge.h"
#include "git_bridge.h"

#include <ctype.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#define MAX_COMMAND 8192
#define MAX_OUTPUT 65536
#define MAX_TOKENS 48
#define MAX_SCRIPT 262144

typedef struct {
    char *text;
    size_t used;
} Report;

static void report(Report *r, const char *text)
{
    size_t len = strlen(text);
    if (r->used + len >= MAX_OUTPUT - 1) {
        len = MAX_OUTPUT - 1 - r->used;
    }
    memcpy(r->text + r->used, text, len);
    r->used += len;
    r->text[r->used] = 0;
}

static char *trim(char *text)
{
    while (*text && isspace((unsigned char)*text)) text++;
    char *end = text + strlen(text);
    while (end > text && isspace((unsigned char)end[-1])) *--end = 0;
    return text;
}

static int split(char *line, const char **args, int cap)
{
    int count = 0;
    char *save = NULL;
    for (char *p = strtok_r(line, " \t\r\n", &save);
         p && count < cap;
         p = strtok_r(NULL, " \t\r\n", &save)) {
        args[count++] = p;
    }
    return count;
}

static int ensure_parent_dirs(const ZUShell *shell, const char *guest)
{
    // Guest path has already been normalized and checked for symlink traversal.
    // Create parents from inside Documents/root only, never outside it.
    size_t root_len = strlen(shell->root);
    char path[ZU_PATH_MAX];
    if (snprintf(path, sizeof(path), "%s%s", shell->root, guest) >= (int)sizeof(path))
        return -1;
    for (char *p = path + root_len + 1; *p; p++) {
        if (*p != '/') continue;
        *p = 0;
        struct stat st;
        if (lstat(path, &st) == 0) {
            if (!S_ISDIR(st.st_mode) || S_ISLNK(st.st_mode)) {
                *p = '/';
                return -1;
            }
        } else if (mkdir(path, 0755) && errno != EEXIST) {
            *p = '/';
            return -1;
        }
        *p = '/';
    }
    return 0;
}

static const char *clone_destination(int count, const char *const args[])
{
    const char *url = NULL, *destination = NULL;
    for (int i = 2; i < count; ++i) {
        if ((!strcmp(args[i], "--branch") || !strcmp(args[i], "-b") ||
             !strcmp(args[i], "--depth")) && i + 1 < count) {
            i++;
        } else if (!strncmp(args[i], "--branch=", 9) ||
                   !strncmp(args[i], "--depth=", 8) ||
                   !strcmp(args[i], "--recurse-submodules") ||
                   !strcmp(args[i], "--recursive")) {
            continue;
        } else if (args[i][0] != '-') {
            if (!url) url = args[i];
            else if (!destination) destination = args[i];
            else return NULL;
        } else {
            return NULL;
        }
    }
    return destination;
}

char *zu_setup_execute(void *context, ZUShell *shell, int argc, const char *const argv[])
{
    (void)context;
    Report out = {.text = calloc(MAX_OUTPUT, 1), .used = 0};
    if (!out.text) return NULL;
    if (argc < 2 || (strcmp(argv[1], "deps") && strcmp(argv[1], "check"))) {
        report(&out, "usage: setup deps [setup.sh] | setup check [setup.sh]\n"
                     "deps fetches pinned Git dependencies from the original script.\n"
                     "check lists detected commands without performing network activity.\n"
                     "CMake, clang, and arbitrary Bash execution are not supported yet.\n");
        return out.text;
    }
    const int dry_run = !strcmp(argv[1], "check");
    const char *filename = argc >= 3 ? argv[2] : "setup.sh";
    if (argc > 3) {
        report(&out, "setup: too many arguments\n");
        return out.text;
    }
    char guest[ZU_PATH_MAX], host[ZU_PATH_MAX];
    if (!zu_shell_resolve_path(shell, filename, guest, host)) {
        report(&out, "setup: invalid or unsafe script path\n");
        return out.text;
    }
    struct stat st;
    if (stat(host, &st) || !S_ISREG(st.st_mode) || st.st_size > MAX_SCRIPT) {
        report(&out, "setup: missing or oversized setup.sh\n");
        return out.text;
    }
    FILE *fp = fopen(host, "r");
    if (!fp) {
        report(&out, "setup: unable to open script\n");
        return out.text;
    }
    char buffer[MAX_COMMAND] = {0};
    char line[MAX_COMMAND];
    int clone_count = 0, update_count = 0, cmake_count = 0, errors = 0;
    size_t total = 0;
    while (fgets(line, sizeof(line), fp)) {
        char *part = trim(line);
        if (!*part || *part == '#') continue;
        size_t length = strlen(part);
        int continuation = length && part[length - 1] == '\\';
        if (continuation) part[--length] = 0;
        if (total + length + 2 >= sizeof(buffer)) {
            report(&out, "setup: a command exceeds the parser limit\n");
            errors++;
            break;
        }
        memcpy(buffer + total, part, length);
        total += length;
        buffer[total++] = ' ';
        buffer[total] = 0;
        if (continuation) continue;

        char statement[MAX_COMMAND];
        memcpy(statement, buffer, total + 1);
        char *command = trim(statement);
        buffer[0] = 0;
        total = 0;

        if (!strncmp(command, "cmake ", 6)) cmake_count++;
        if (strncmp(command, "git clone ", 10) && strncmp(command, "git -C ", 7)) continue;

        const char *args[MAX_TOKENS];
        int n = split(command, args, MAX_TOKENS);
        if (n < 3) continue;

        if (!strcmp(args[1], "clone")) {
            clone_count++;
            const char *destination = clone_destination(n, args);
            if (!destination) {
                report(&out, "setup: git clone needs an explicit destination\n");
                errors++;
                continue;
            }
            char clone_guest[ZU_PATH_MAX], clone_host[ZU_PATH_MAX];
            if (!zu_shell_resolve_path(shell, destination, clone_guest, clone_host)) {
                report(&out, "setup: unsafe Git dependency destination\n");
                errors++;
                continue;
            }
            if (dry_run) {
                report(&out, "Git clone: ");
                report(&out, clone_guest);
                report(&out, "\n");
                continue;
            }
            char dotgit[ZU_PATH_MAX];
            if (snprintf(dotgit, sizeof(dotgit), "%s/.git", clone_host) >= (int)sizeof(dotgit)) {
                report(&out, "setup: dependency path too long\n");
                errors++;
                continue;
            }
            struct stat gitdir;
            if (!lstat(dotgit, &gitdir)) {
                report(&out, "Already cloned: ");
                report(&out, clone_guest);
                report(&out, "\n");
                continue;
            }
            if (!lstat(clone_host, &gitdir)) {
                report(&out, "setup: destination exists without .git: ");
                report(&out, clone_guest);
                report(&out, "\n");
                errors++;
                continue;
            }
            if (ensure_parent_dirs(shell, clone_guest)) {
                report(&out, "setup: unable to create dependency parent folder\n");
                errors++;
                continue;
            }
        } else {
            update_count++;
            if (dry_run) {
                report(&out, "Recursive submodule update: ");
                report(&out, args[2]);
                report(&out, "\n");
                continue;
            }
        }
        char *message = zu_git_execute(NULL, shell, n, args);
        if (message) {
            report(&out, message);
            if (strstr(message, "git clone:") || strstr(message, "git submodule:") ||
                strstr(message, "Clone incomplete") || strstr(message, "git -C:") ||
                strstr(message, "fatal:")) errors++;
            free(message);
        } else {
            report(&out, "setup: unable to run Git command\n");
            errors++;
        }
    }
    fclose(fp);

    char summary[384];
    snprintf(summary, sizeof(summary),
             "setup: %d clone command(s), %d submodule update(s), %d CMake commands skipped, %d error(s).\n",
             clone_count, update_count, cmake_count, errors);
    report(&out, summary);
    report(&out, "Note: this fetch-only adapter does not run Bash or build native libraries.\n"
                 "HorseEngine.cpp still requires CMake, a C/C++ compiler, and a compatible graphics backend.\n");
    return out.text;
}
