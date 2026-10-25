#define _POSIX_C_SOURCE 200809L
#include "project.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#define ZU_REPORT_MAX (32 * 1024)

typedef struct {
    char *data;
    size_t used;
} Report;

static void note(Report *report, const char *format, ...)
{
    if (report->used >= ZU_REPORT_MAX - 1) return;
    va_list args;
    va_start(args, format);
    int n = vsnprintf(report->data + report->used,
                      ZU_REPORT_MAX - report->used, format, args);
    va_end(args);
    if (n > 0) {
        size_t remaining = ZU_REPORT_MAX - report->used - 1;
        report->used += (size_t)n < remaining ? (size_t)n : remaining;
    }
}

static int inspect_script(ZUShell *shell, Report *report)
{
    char guest[ZU_PATH_MAX], host[ZU_PATH_MAX];
    if (!zu_shell_resolve_path(shell, "setup.sh", guest, host)) return 0;
    FILE *file = fopen(host, "r");
    if (!file) return 0;

    unsigned clone_count = 0, submodules = 0, cmake = 0;
    unsigned clang = 0, bash = 0;
    char line[8192];
    while (fgets(line, sizeof(line), file)) {
        if (strstr(line, "#!/") && strstr(line, "bash")) bash++;
        if (strstr(line, "git clone")) clone_count++;
        if (strstr(line, "submodule update")) submodules++;
        if (strstr(line, "cmake -S") || strstr(line, "cmake --build")) cmake++;
        if (strstr(line, "for cmd in") && strstr(line, "clang")) clang++;
    }
    fclose(file);
    note(report, "setup.sh: found (%s)\n", bash ? "Bash" : "shell");
    if (clone_count)
        note(report, "  Git clones: %u   [clone flags supported: --branch, --depth, --recurse-submodules]\n",
             clone_count);
    if (submodules)
        note(report, "  Submodule updates: %u   [git -C DIR submodule update --init --recursive supported]\n",
             submodules);
    if (cmake)
        note(report, "  CMake operations: %u   [BLOCKED: CMake and native tool execution unavailable]\n",
             cmake);
    if (clang)
        note(report, "  Clang prerequisite: yes   [BLOCKED: Clang/Zig cc not embedded]\n");
    note(report, "  Script execution: BLOCKED (no POSIX Bash/process runtime)\n");
    return 1;
}

static int inspect_cbuild(ZUShell *shell, Report *report)
{
    char guest[ZU_PATH_MAX], host[ZU_PATH_MAX];
    if (!zu_shell_resolve_path(shell, "build.c", guest, host)) return 0;
    FILE *file = fopen(host, "r");
    if (!file) return 0;
    unsigned sources = 0, includes = 0, flags = 0;
    unsigned libs = 0, frameworks = 0;
    int desktop_gl = 0, macos_only = 0;
    char line[8192];
    char target[160] = {0};
    while (fgets(line, sizeof(line), file)) {
        const char *p = strstr(line, "c_executable(");
        if (p && !target[0]) {
            const char *quote = strchr(p, '"');
            if (quote) {
                const char *end = strchr(quote + 1, '"');
                size_t n = end ? (size_t)(end - quote - 1) : 0;
                if (n && n < sizeof(target)) {
                    memcpy(target, quote + 1, n);
                    target[n] = 0;
                }
            }
        }
        if (strstr(line, "c_sources(")) sources++;
        if (strstr(line, "c_include(")) includes++;
        if (strstr(line, "c_flag(")) flags++;
        if (strstr(line, "c_link_flag(") || strstr(line, "c_link_system(")) libs++;
        if (strstr(line, "c_framework(")) frameworks++;
        if (strstr(line, "c_framework(") && strstr(line, "OpenGL")) desktop_gl = 1;
        if (strstr(line, "c_framework(") && strstr(line, "Cocoa")) macos_only = 1;
    }
    fclose(file);
    note(report, "build.c: found (xt9y/build.c API)\n");
    if (target[0]) note(report, "  Target: %s\n", target);
    note(report, "  Declared sources: %u, include paths: %u, flags: %u, links: %u, frameworks: %u\n",
         sources, includes, flags, libs, frameworks);
    note(report, "  Running build(C_Build*): BLOCKED (needs a C compiler + compatible build-module ABI)\n");
    note(report, "  Building and launching: BLOCKED (needs subprocess or interpreted target support)\n");
    if (desktop_gl || macos_only) {
        note(report, "  Platform: %s%s — macOS desktop APIs are not iPadOS backends\n",
             desktop_gl ? "desktop OpenGL " : "",
             macos_only ? "Cocoa" : "");
    }
    note(report, "  Note: this is source inspection, not evaluation of build.c logic.\n");
    return 1;
}

char *zu_project_execute(void *context, ZUShell *shell,
                         int argc, const char *const argv[])
{
    (void)context;
    Report report = {.data = calloc(ZU_REPORT_MAX, 1), .used = 0};
    if (!report.data) return NULL;
    if (argc != 2 || (strcmp(argv[1], "inspect") && strcmp(argv[1], "doctor"))) {
        note(&report, "usage: project inspect\n");
        return report.data;
    }
    note(&report, "Zigunios project compatibility report\n");
    note(&report, "Current directory: %s\n", zu_shell_cwd(shell));
    int setup = inspect_script(shell, &report);
    int build = inspect_cbuild(shell, &report);
    if (!setup && !build) {
        note(&report, "No setup.sh or build.c found in this directory.\n");
    }
    note(&report, "Compiler: Zig cc is not available in this app build.\n");
    note(&report, "Execution: WASI programs work via 'wasm FILE.wasm'; native app binaries do not.\n");
    note(&report, "Display: 'metal demo' works; running desktop OpenGL games is unsupported.\n");
    return report.data;
}
