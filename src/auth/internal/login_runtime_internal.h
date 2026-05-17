#ifndef AUTH_INTERNAL_LOGIN_RUNTIME_INTERNAL_H
#define AUTH_INTERNAL_LOGIN_RUNTIME_INTERNAL_H

/*
 * src/auth/internal/login_runtime_internal.h
 *
 * Internal helpers shared across the split translation units of
 * `src/auth/login_runtime.c` (PR C.0 of the Estagio C dedicated
 * plan).  Marked `static inline` so each consumer translation unit
 * gets its own private copy with file-scope linkage, exactly
 * matching the `static` storage class of the original definitions.
 *
 *   - dbg_login_putc          : raw E9 debug-port byte emission
 *                               (no-op under UNIT_TEST).
 *   - dbg_login_puts          : null-terminated string emission via
 *                               dbg_login_putc.
 *   - ops_ready               : full readiness predicate for a
 *                               `login_runtime_ops` vtable.
 *   - login_service_poll      : optional service poll hook trampoline.
 *   - login_maintenance_mode_active
 *                             : maintenance-mode predicate with
 *                               legacy fallback.
 *
 * Byte-for-byte extraction from `src/auth/login_runtime.c` lines
 * 4-45 of the pre-Estagio-C tree; see
 * `docs/plans/active/monolith-residual-dedicated-plan.md`.
 */

#include "auth/login_runtime.h"

static inline void dbg_login_putc(char ch) {
#ifdef UNIT_TEST
  (void)ch;
#else
  __asm__ volatile("outb %0, %1" : : "a"((unsigned char)ch), "Nd"((unsigned short)0xE9));
#endif
}

static inline void dbg_login_puts(const char *text) {
  while (text && *text) {
    dbg_login_putc(*text++);
  }
}

static inline int ops_ready(const struct login_runtime_ops *ops) {
  return ops && ops->shell_ctx && ops->session_ctx && ops->settings &&
         ops->prepare_shell_runtime &&
         (!ops->maintenance_mode || ops->maintenance_session_start) &&
         ops->init_shell_context_user &&
         ops->dispatch_shell_command && ops->run_shell_alias && ops->is_equal &&
         ops->readline && ops->session_reset && ops->session_set_active &&
         ops->shell_context_init && ops->system_login && ops->session_user &&
         ops->session_cwd && ops->shell_context_should_logout && ops->print &&
         ops->putc && ops->clear_view && ops->show_splash && ops->ui_banner &&
         ops->cmd_info;
}

static inline void login_service_poll(struct login_runtime_ops *ops) {
  if (ops && ops->service_poll) {
    ops->service_poll();
  }
}

static inline int login_maintenance_mode_active(const struct login_runtime_ops *ops) {
  if (!ops) {
    return 0;
  }
  if (ops->maintenance_mode_active) {
    return ops->maintenance_mode_active() ? 1 : 0;
  }
  return ops->maintenance_mode ? 1 : 0;
}

#endif /* AUTH_INTERNAL_LOGIN_RUNTIME_INTERNAL_H */
