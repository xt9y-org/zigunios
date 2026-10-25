#ifndef ZIGUNIOS_COMPILER_BRIDGE_H
#define ZIGUNIOS_COMPILER_BRIDGE_H
#include "shell.h"
#ifdef __cplusplus
extern "C" {
#endif
char *zu_compiler_execute(void *context, ZUShell *shell, int argc, const char *const argv[]);
#ifdef __cplusplus
}
#endif
#endif
