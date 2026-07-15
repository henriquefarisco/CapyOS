#ifndef GUI_DESKTOP_RUNTIME_H
#define GUI_DESKTOP_RUNTIME_H

#include "shell/core.h"

/* Start the desktop main loop. Returns when the user exits. */
int desktop_runtime_start(struct shell_context *ctx);

/* Returns 1 if the desktop is currently running, 0 otherwise. */
int desktop_is_active(void);

/* Stop only the graphical desktop loop. This does not end the authenticated
 * session and is reserved for TTY fallback and system power transitions. */
void desktop_stop(void);

/* End the authenticated desktop session and return through login_runtime to
 * the graphical login screen. Fails closed when no live desktop/shell context
 * is available, so a broken logout request can never expose the text shell. */
int desktop_request_logout(void);

/* Foreground-only adapter used by the governed application registry.  It
 * opens or focuses the one desktop Terminal window and reports whether the
 * window became visible. */
int desktop_open_terminal_window(void);

/* Dispatch a shell command from the desktop terminal. */
int kernel_desktop_dispatch_shell_command(char *line);
/* Same dispatch with the handler's real return code. `out_rc` is written only
 * when a command was found. Used by deferred in-desktop workers such as
 * CapyAI; callers must not invoke it from a window callback. */
int kernel_desktop_dispatch_shell_command_result(char *line, int *out_rc);

enum desktop_shell_dispatch_status {
  DESKTOP_SHELL_DISPATCH_BUSY = -1,
  DESKTOP_SHELL_DISPATCH_UNKNOWN = 0,
  DESKTOP_SHELL_DISPATCH_HANDLED = 1
};

/* Serialize the process-global shell output/session hooks around one command.
 * A NULL `ctx` selects the live desktop context. UI callers use wait=0 so a
 * background command cannot freeze a frame; the dedicated CapyAI worker uses
 * wait=1 and only writes to bounded, non-graphical callbacks. */
int kernel_desktop_dispatch_shell_command_scoped(
    struct shell_context *ctx, char *line, int *out_rc, int wait_if_busy,
    shell_output_write_fn write_cb, shell_output_putc_fn putc_cb,
    shell_output_clear_fn clear_cb);

/* Run one typed, non-graphical operation with the same serialized session
 * binding used by shell dispatch. This lets background services call VFS and
 * other permission-aware adapters without racing the process-global active
 * session or converting typed arguments into command text. */
typedef int (*desktop_session_operation_fn)(void *operation_ctx);
int kernel_desktop_run_session_operation_scoped(
    struct shell_context *ctx, desktop_session_operation_fn operation,
    void *operation_ctx, int wait_if_busy, int *out_rc);

/* Copy the current desktop shell/session into caller-owned storage. This is
 * the lifetime boundary used by workers: closing the desktop cannot leave a
 * queued operation holding a pointer into the foreground shell stack. */
int kernel_desktop_shell_snapshot(struct shell_context *out_ctx,
                                  struct session_context *out_session);
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

/* Request/open CapyAI through the desktop lifecycle. When the desktop is not
 * active yet, the request is consumed immediately after compositor init.
 * Returns -1 only for an immediate surface-allocation failure. */
int desktop_launch_capyai(void);

#ifdef CAPYOS_CAPYAI_GUI_ASYNC_SMOKE
int desktop_capyai_gui_async_smoke_run(void);
#endif

#endif /* GUI_DESKTOP_RUNTIME_H */
