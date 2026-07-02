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
struct session_context *kernel_desktop_shell_session(void);
int kernel_desktop_shell_should_stop(void);
int kernel_desktop_shell_should_logout(void);

/* Etapa 7 / Slice 7.5: stable hook for the desktop launcher's "Navegador"
 * entry. Spawns the ring-3 graphical browser (capygfx) alongside the running
 * desktop session when the blob is embedded (CAPYOS_DESKTOP_GRAPHICAL_BROWSER,
 * default in the full profile). Returns 0 on spawn success, -1 when the
 * browser is unavailable in this kernel or the spawn failed -- the caller
 * (CapyUI) surfaces the failure to the user; never fails silently. Defined
 * unconditionally (all profiles) so the desktop code links against a single
 * symbol regardless of build flags. */
int kernel_desktop_open_browser_graphical(void);

#endif /* GUI_DESKTOP_RUNTIME_H */
