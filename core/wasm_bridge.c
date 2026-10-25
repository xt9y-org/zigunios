#define _POSIX_C_SOURCE 200809L
#include "wasm_bridge.h"

#include <wasm3.h>
#include <m3_api_wasi.h>

#include <errno.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#define ZU_WASM_MAX_FILE (64 * 1024 * 1024)
#define ZU_WASM_MAX_OUTPUT (64 * 1024)

/* wasm3 is an interpreter, not a JIT. Serialise stdout capture: iOS stdout is process-wide. */
static pthread_mutex_t wasm_output_lock = PTHREAD_MUTEX_INITIALIZER;

static char *message(const char *text)
{
    return strdup(text ? text : "wasm: error\n");
}

char *zu_wasm_execute(void *context, ZUShell *shell, int argc, const char *const argv[])
{
    (void)context;
    if (argc != 2) return message("usage: wasm FILE.wasm\n");
    char guest[ZU_PATH_MAX], host[ZU_PATH_MAX];
    if (!zu_shell_resolve_path(shell, argv[1], guest, host))
        return message("wasm: invalid or unsafe path\n");

    struct stat st;
    if (stat(host, &st) != 0 || !S_ISREG(st.st_mode) ||
        st.st_size <= 0 || st.st_size > ZU_WASM_MAX_FILE)
        return message("wasm: file missing, empty, or exceeds 64 MiB\n");

    FILE *file = fopen(host, "rb");
    if (!file) return message("wasm: could not open file\n");
    size_t size = (size_t)st.st_size;
    uint8_t *bytes = malloc(size);
    if (!bytes) { fclose(file); return message("wasm: out of memory\n"); }
    int valid = fread(bytes, 1, size, file) == size;
    fclose(file);
    if (!valid) { free(bytes); return message("wasm: file read failed\n"); }

    // Run one guest at a time. This also prevents the process-global stdout redirect
    // from interfering with another interpreted module.
    pthread_mutex_lock(&wasm_output_lock);
    char *output = calloc(ZU_WASM_MAX_OUTPUT, 1);
    if (!output) { pthread_mutex_unlock(&wasm_output_lock); free(bytes); return NULL; }
    size_t at = 0;

    IM3Environment env = m3_NewEnvironment();
    IM3Runtime runtime = env ? m3_NewRuntime(env, 128 * 1024, NULL) : NULL;
    IM3Module module = NULL;
    IM3Function start = NULL;
    M3Result result = env && runtime ? NULL : "wasm3 allocation failed";
    int loaded = 0;

    if (!result) result = m3_ParseModule(env, &module, bytes, (uint32_t)size);
    if (!result) {
        result = m3_LoadModule(runtime, module);
        if (!result) loaded = 1;
    }
    if (!result) result = m3_LinkWASI(module);
    if (!result) result = m3_FindFunction(&start, runtime, "_start");

    // WASI writes stdout through libc on the host. Capture to a temporary file
    // while executing rather than printing it into the Xcode console.
    int saved = -1;
    FILE *capture = NULL;
    if (!result) {
        capture = tmpfile();
        if (!capture) result = "temporary output file unavailable";
        else {
            fflush(stdout);
            saved = dup(STDOUT_FILENO);
            if (saved < 0 || dup2(fileno(capture), STDOUT_FILENO) < 0)
                result = "could not capture WASI output";
        }
    }
    if (!result) result = m3_CallV(start);
    if (saved >= 0) {
        fflush(stdout);
        dup2(saved, STDOUT_FILENO);
        close(saved);
    }
    if (capture) {
        rewind(capture);
        at = fread(output, 1, ZU_WASM_MAX_OUTPUT - 256, capture);
        fclose(capture);
    }
    if (result && strcmp(result, m3Err_trapExit)) {
        snprintf(output + at, ZU_WASM_MAX_OUTPUT - at, "\nwasm: %s\n", result);
    } else {
        int exit_code = 0;
        m3_wasi_context_t *wasi = m3_GetWasiContext();
        if (wasi) exit_code = wasi->exit_code;
        snprintf(output + at, ZU_WASM_MAX_OUTPUT - at, "\n[wasm exit %d]\n", exit_code);
    }

    if (module && !loaded) m3_FreeModule(module);
    if (runtime) m3_FreeRuntime(runtime);
    if (env) m3_FreeEnvironment(env);
    free(bytes);
    pthread_mutex_unlock(&wasm_output_lock);
    return output;
}
