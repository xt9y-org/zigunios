#ifndef ZIGUNIOS_SETUP_BRIDGE_H
#define ZIGUNIOS_SETUP_BRIDGE_H

#include "shell.h"

/* Fetch dependencies declared by the git commands in setup.sh. This does
 * NOT execute arbitrary shell code or compile the downloaded dependencies. */
char *zu_setup_execute(void *context, ZUShell *shell, int argc, const char *const argv[]);

#endif
