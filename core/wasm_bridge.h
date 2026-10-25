#ifndef ZIGUNIOS_WASM_BRIDGE_H
#define ZIGUNIOS_WASM_BRIDGE_H
#include "shell.h"
char *zu_wasm_execute(void *context, ZUShell *shell, int argc, const char *const argv[]);
#endif
