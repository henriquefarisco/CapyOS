/*
 * src/kernel/syscall_gfx.c — Etapa 7 / Slice 7.2 graphical surface syscalls.
 *
 * Wires SYS_WINDOW_CREATE (45) .. SYS_WINDOW_DESTROY (50). Mirrors the
 * structure of syscall_net.c: the handlers own all policy (ownership, bounds,
 * fail-closed validation, user-pointer copies) and defer the actual surface
 * work to an injectable `struct syscall_gfx_ops` backend. Production installs a
 * compositor-backed backend (syscall_gfx_init.c); host tests install a fake
 * in-RAM surface (tests/kernel/test_syscall_gfx.c) so this whole file is
 * testable without the compositor.
 *
 * ISOLATION (Option A): a ring-3 app refers to its window only by an opaque
 * small-integer HANDLE. The handle table below maps handle -> {owner pid,
 * backend window id}. Every handler resolves process_current()->pid and rejects
 * a handle owned by a different process, so an app can neither draw into,
 * present, nor destroy a window it does not own, and it never receives a
 * pointer into a kernel surface. Per-call argument validation guarantees no
 * integer overflow or oversized copy can reach the backend.
 *
 * The kernel runs on the caller's CR3 at syscall time, so user pointers (title,
 * blit source, event-out) are dereferenced directly while bounded -- the same
 * model syscall_net.c uses for sockaddr copies.
 */

#include "kernel/syscall_gfx.h"
#include "kernel/syscall.h"
#include "kernel/process.h"

#include <stddef.h>
#include <stdint.h>

/* System-wide window budget. Matches the compositor's COMPOSITOR_MAX_WINDOWS so
 * the handle table can never describe more windows than the backend supports;
 * kept as a local define to avoid pulling the compositor header into the
 * host-testable handler TU. */
#define GFX_MAX_HANDLES 32u

struct gfx_handle {
  int in_use;
  uint32_t owner_pid;
  int32_t backend_id;
};

static struct gfx_handle g_handles[GFX_MAX_HANDLES];
static const struct syscall_gfx_ops *g_ops;

/* ---- handle table helpers ------------------------------------------------ */

void syscall_gfx_reset(void) {
  size_t i;
  for (i = 0u; i < GFX_MAX_HANDLES; ++i) {
    g_handles[i].in_use = 0;
    g_handles[i].owner_pid = 0u;
    g_handles[i].backend_id = 0;
  }
}

void syscall_gfx_install_ops(const struct syscall_gfx_ops *ops) {
  g_ops = ops;
  if (ops == NULL) {
    /* Dropping the backend: forget every handle so a fresh registration (or a
     * fresh test) starts from a known-empty state. The backend is being torn
     * down by the caller, so we deliberately do NOT call win_destroy here. */
    syscall_gfx_reset();
  }
}

const struct syscall_gfx_ops *syscall_gfx_get_ops(void) { return g_ops; }

/* Resolve a public handle to its table index, enforcing ownership by `pid`.
 * Returns the index on success, or -1 when the handle is out of range, free, or
 * owned by another process. */
static int gfx_resolve_owned(uint64_t handle, uint32_t pid) {
  uint64_t idx;
  if (handle < 1u || handle > GFX_MAX_HANDLES) return -1;
  idx = handle - 1u;
  if (!g_handles[idx].in_use) return -1;
  if (g_handles[idx].owner_pid != pid) return -1;
  return (int)idx;
}

/* Current process pid, or 0 when there is no process context (kernel thread).
 * pid 0 never owns a window, so a context-less caller fails every ownership
 * check fail-closed. */
static uint32_t gfx_current_pid(void) {
  struct process *proc = process_current();
  return proc ? proc->pid : 0u;
}

static void gfx_event_clear(struct capy_gfx_event *ev) {
  ev->kind = 0u;
  ev->code = 0u;
  ev->x = 0;
  ev->y = 0;
  ev->dx = 0;
  ev->dy = 0;
  ev->mods = 0u;
  ev->reserved = 0u;
}

/* Copy/sanitize a user title into a bounded kernel buffer. NUL-terminated;
 * control bytes are replaced so a hostile title cannot inject terminal escapes
 * into kernel logs / the title bar. A NULL user title yields an empty string
 * (the backend then picks its default). */
static void gfx_copy_title(const char *user_title, char *dst, size_t cap) {
  size_t i = 0u;
  if (cap == 0u) return;
  if (user_title != NULL) {
    for (i = 0u; i + 1u < cap; ++i) {
      char c = user_title[i];
      if (c == '\0') break;
      dst[i] = ((unsigned char)c < 0x20u || (unsigned char)c == 0x7Fu) ? '?' : c;
    }
  }
  dst[i] = '\0';
}

/* ---- handlers ------------------------------------------------------------ */

int64_t sys_window_create(struct syscall_frame *f) {
  const char *user_title = (const char *)f->rdi;
  uint32_t w = (uint32_t)f->rsi;
  uint32_t h = (uint32_t)f->rdx;
  uint32_t pid = gfx_current_pid();
  char title[64];
  size_t i;
  int32_t backend_id;

  if (pid == 0u) return -1;
  if (w == 0u || h == 0u || w > CAPY_GFX_MAX_DIM || h > CAPY_GFX_MAX_DIM)
    return -1;
  if (g_ops == NULL || g_ops->win_create == NULL) return -1;

  for (i = 0u; i < GFX_MAX_HANDLES; ++i) {
    if (!g_handles[i].in_use) break;
  }
  if (i == GFX_MAX_HANDLES) return -1; /* table full */

  gfx_copy_title(user_title, title, sizeof(title));
  backend_id = g_ops->win_create(title, w, h, pid);
  if (backend_id <= 0) return -1; /* backend refused; slot untouched */

  g_handles[i].in_use = 1;
  g_handles[i].owner_pid = pid;
  g_handles[i].backend_id = backend_id;
  return (int64_t)(i + 1u); /* public handle = index + 1 */
}

int64_t sys_surface_fill(struct syscall_frame *f) {
  uint64_t handle = f->rdi;
  uint32_t x = (uint32_t)f->rsi;
  uint32_t y = (uint32_t)f->rdx;
  uint32_t w = (uint32_t)f->r10;
  uint32_t hgt = (uint32_t)f->r8;
  uint32_t argb = (uint32_t)f->r9;
  uint32_t pid = gfx_current_pid();
  int idx;

  if (pid == 0u) return -1;
  idx = gfx_resolve_owned(handle, pid);
  if (idx < 0) return -1;
  /* Bound every coordinate so the backend's clip arithmetic stays well inside
   * 32-bit range; the backend still clips precisely against its surface. */
  if (x > CAPY_GFX_MAX_DIM || y > CAPY_GFX_MAX_DIM || w > CAPY_GFX_MAX_DIM ||
      hgt > CAPY_GFX_MAX_DIM)
    return -1;
  if (g_ops == NULL || g_ops->surface_fill == NULL) return -1;
  return g_ops->surface_fill(g_handles[idx].backend_id, x, y, w, hgt, argb) == 0
             ? 0
             : -1;
}

int64_t sys_surface_blit(struct syscall_frame *f) {
  uint64_t handle = f->rdi;
  const uint32_t *src = (const uint32_t *)f->rsi;
  uint32_t sw = (uint32_t)f->rdx;
  uint32_t sh = (uint32_t)f->r10;
  uint32_t dx = (uint32_t)f->r8;
  uint32_t dy = (uint32_t)f->r9;
  uint32_t pid = gfx_current_pid();
  uint64_t pixels;
  int idx;

  if (pid == 0u) return -1;
  idx = gfx_resolve_owned(handle, pid);
  if (idx < 0) return -1;
  if (src == NULL) return -1;
  if (sw == 0u || sh == 0u) return 0; /* nothing to copy */
  if (sw > CAPY_GFX_MAX_DIM || sh > CAPY_GFX_MAX_DIM) return -1;
  if (dx > CAPY_GFX_MAX_DIM || dy > CAPY_GFX_MAX_DIM) return -1;
  pixels = (uint64_t)sw * (uint64_t)sh;
  if (pixels > (uint64_t)CAPY_GFX_BLIT_MAX_PIXELS) return -1;
  if (g_ops == NULL || g_ops->surface_blit == NULL) return -1;
  return g_ops->surface_blit(g_handles[idx].backend_id, src, sw, sh, dx, dy) == 0
             ? 0
             : -1;
}

int64_t sys_window_poll_event(struct syscall_frame *f) {
  uint64_t handle = f->rdi;
  struct capy_gfx_event *user_out = (struct capy_gfx_event *)f->rsi;
  uint32_t pid = gfx_current_pid();
  struct capy_gfx_event ev;
  int idx;
  int r;

  if (pid == 0u) return -1;
  idx = gfx_resolve_owned(handle, pid);
  if (idx < 0) return -1;
  if (user_out == NULL) return -1;
  if (g_ops == NULL || g_ops->poll_event == NULL) return -1;

  gfx_event_clear(&ev);
  r = g_ops->poll_event(g_handles[idx].backend_id, &ev);
  if (r == 1) {
    *user_out = ev; /* CR3 is the caller's; direct copy is sound */
    return 1;
  }
  if (r == 0) return 0;
  return -1;
}

int64_t sys_window_present(struct syscall_frame *f) {
  uint64_t handle = f->rdi;
  uint32_t pid = gfx_current_pid();
  int idx;

  if (pid == 0u) return -1;
  idx = gfx_resolve_owned(handle, pid);
  if (idx < 0) return -1;
  if (g_ops == NULL || g_ops->win_present == NULL) return -1;
  g_ops->win_present(g_handles[idx].backend_id);
  return 0;
}

int64_t sys_window_destroy(struct syscall_frame *f) {
  uint64_t handle = f->rdi;
  uint32_t pid = gfx_current_pid();
  int idx;

  if (pid == 0u) return -1;
  idx = gfx_resolve_owned(handle, pid);
  if (idx < 0) return -1;
  if (g_ops != NULL && g_ops->win_destroy != NULL)
    g_ops->win_destroy(g_handles[idx].backend_id);
  g_handles[idx].in_use = 0;
  g_handles[idx].owner_pid = 0u;
  g_handles[idx].backend_id = 0;
  return 0;
}

/* ---- lifecycle ----------------------------------------------------------- */

void syscall_gfx_release_owner(uint32_t pid) {
  size_t i;
  if (pid == 0u) return; /* pid 0 never owns a window */
  for (i = 0u; i < GFX_MAX_HANDLES; ++i) {
    if (g_handles[i].in_use && g_handles[i].owner_pid == pid) {
      if (g_ops != NULL && g_ops->win_destroy != NULL)
        g_ops->win_destroy(g_handles[i].backend_id);
      g_handles[i].in_use = 0;
      g_handles[i].owner_pid = 0u;
      g_handles[i].backend_id = 0;
    }
  }
}

void syscall_gfx_register_handlers(void) {
  syscall_register(SYS_WINDOW_CREATE, sys_window_create);
  syscall_register(SYS_SURFACE_FILL, sys_surface_fill);
  syscall_register(SYS_SURFACE_BLIT, sys_surface_blit);
  syscall_register(SYS_WINDOW_POLL_EVENT, sys_window_poll_event);
  syscall_register(SYS_WINDOW_PRESENT, sys_window_present);
  syscall_register(SYS_WINDOW_DESTROY, sys_window_destroy);
}
