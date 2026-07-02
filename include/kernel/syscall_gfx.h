#ifndef KERNEL_SYSCALL_GFX_H
#define KERNEL_SYSCALL_GFX_H

#include <stdint.h>
#include <stddef.h>

#include "kernel/syscall.h"
#include "kernel/syscall_gfx_abi.h"

/* Etapa 7 / Slice 7.2 — userland graphical surface syscall handlers.
 *
 * The six syscalls SYS_WINDOW_CREATE (45) .. SYS_WINDOW_DESTROY (50) are wired
 * here, mirroring the structure of src/kernel/syscall_net.c. Each handler:
 *
 *   1. Resolves the calling process via `process_current()` and identifies the
 *      window only by an opaque per-process HANDLE. A separate ownership table
 *      (in syscall_gfx.c) maps handle -> {owner pid, backend window id}; a
 *      handle owned by another process is rejected, so one app can never draw
 *      into, present, or destroy another app's window.
 *   2. Validates dimensions / rectangles / buffer sizes fail-closed (no integer
 *      overflow can reach the backend) before deferring to the registered
 *      `syscall_gfx_ops` table.
 *   3. The kernel runs on the caller's CR3 at syscall time, so user pointers
 *      (title, blit source, event-out) are dereferenced directly while bounded;
 *      this mirrors the sockaddr handling in syscall_net.c.
 *
 * Production wiring (syscall_gfx_init.c) installs a compositor-backed backend;
 * host unit tests install a fake backend with an in-RAM surface so the handler
 * logic is testable without dragging in the compositor. Failure mode for the
 * whole family is a clean -1 (or 0 "no event" for poll). */

/* Backend vtable. A backend window is identified by a backend id > 0. The
 * handler layer owns the public handle <-> (pid, backend id) mapping; the
 * backend is stateless w.r.t. ownership. */
struct syscall_gfx_ops {
  /* Create a WxH window titled `title` (already copied/sanitized by the
   * handler), owned by `pid`. Returns a backend id > 0 on success, <= 0 on
   * failure. Etapa 7 / Slice 7.5 (alpha.305): `pid` lets the production
   * backend mark the compositor window as ring-3-owned
   * (struct gui_window.gfx_owner_pid) so the desktop's input dispatcher can
   * route mouse/keyboard for that window through the event queue
   * (gui_event_push_*) instead of the direct on_mouse/on_key callback path --
   * see src/kernel/syscall_gfx_init.c. */
  int32_t (*win_create)(const char *title, uint32_t w, uint32_t h,
                        uint32_t pid);
  /* Destroy a backend window. Idempotent for an unknown id. */
  void (*win_destroy)(int32_t backend_id);
  /* Report the surface dimensions (pixels). Returns 0 on success and writes
   * *w,*h; -1 on an unknown id. */
  int (*surface_info)(int32_t backend_id, uint32_t *w, uint32_t *h);
  /* Fill rect [x,y,w,h] with `argb`. The backend clips to the surface. Returns
   * 0 on success, -1 on an unknown id. */
  int (*surface_fill)(int32_t backend_id, uint32_t x, uint32_t y, uint32_t w,
                      uint32_t h, uint32_t argb);
  /* Blit `src` (sw*sh ARGB32, row stride == sw) to (dx,dy). `src` is valid
   * under the caller's CR3 for the duration of the call; the backend clips to
   * the surface. Returns 0 on success, -1 on an unknown id / NULL src. */
  int (*surface_blit)(int32_t backend_id, const uint32_t *src, uint32_t sw,
                      uint32_t sh, uint32_t dx, uint32_t dy);
  /* Mark the window for presentation (compositor invalidate / flush). */
  void (*win_present)(int32_t backend_id);
  /* Pop one pending event for the window into *out. Returns 1 if an event was
   * written, 0 if none pending, -1 on an unknown id. */
  int (*poll_event)(int32_t backend_id, struct capy_gfx_event *out);
};

/* Install a vtable. Passing NULL clears the registration AND resets all handle
 * state, so a fresh test starts from a known empty state. The pointer is stored
 * as-is; the caller must keep the struct alive for the registration lifetime. */
void syscall_gfx_install_ops(const struct syscall_gfx_ops *ops);
const struct syscall_gfx_ops *syscall_gfx_get_ops(void);

/* Wire SYS_WINDOW_CREATE..SYS_WINDOW_DESTROY (45..50) into the kernel syscall
 * table. Called once from syscall_init(). */
void syscall_gfx_register_handlers(void);

/* Production-only wiring: install the compositor-backed backend. Lives in
 * syscall_gfx_init.c so unit tests linking syscall_gfx.c standalone don't pull
 * in the compositor's transitive deps. Must run after compositor_init. */
void syscall_gfx_install_default_ops(void);

/* Destroy every window owned by `pid` and free its handles. Idempotent. Called
 * from the process teardown path (process_exit / process_destroy) so a dying
 * app cannot leak compositor windows or leave a handle owned by a reused pid. */
void syscall_gfx_release_owner(uint32_t pid);

/* Drop all handle state without touching the backend (test reset helper). */
void syscall_gfx_reset(void);

/* Individual handlers. Exposed (non-static) so host tests can drive them with a
 * synthetic frame + a faked process_current(), matching syscall_net.c. */
int64_t sys_window_create(struct syscall_frame *f);
int64_t sys_surface_fill(struct syscall_frame *f);
int64_t sys_surface_blit(struct syscall_frame *f);
int64_t sys_window_poll_event(struct syscall_frame *f);
int64_t sys_window_present(struct syscall_frame *f);
int64_t sys_window_destroy(struct syscall_frame *f);

#endif /* KERNEL_SYSCALL_GFX_H */
