#ifndef GUI_DESKTOP_RUNTIME_H
#define GUI_DESKTOP_RUNTIME_H

#include "shell/core.h"

/* Start the desktop main loop. Returns when the user exits. */
int desktop_runtime_start(struct shell_context *ctx);

/* Returns 1 if the desktop is currently running, 0 otherwise. */
int desktop_is_active(void);

/* Stop the desktop (called from within the desktop terminal). */
void desktop_stop(void);

/* Dispatch a shell command from the desktop terminal. */
int kernel_desktop_dispatch_shell_command(char *line);

#endif /* GUI_DESKTOP_RUNTIME_H */
