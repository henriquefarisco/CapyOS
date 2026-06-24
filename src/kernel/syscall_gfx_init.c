/*
 * src/kernel/syscall_gfx_init.c — Etapa 7 / Slice 7.2 production backend for the
 * graphical surface syscalls.
 *
 * Implements the `struct syscall_gfx_ops` vtable on top of the kernel
 * compositor (include/gui/compositor.h) and installs it via
 * syscall_gfx_install_default_ops(). Kept in a separate TU from
 * src/kernel/syscall_gfx.c (the policy/handler layer) so host unit tests can
 * link the handlers with a fake backend WITHOUT dragging in the compositor +
 * framebuffer + event-queue transitive dependencies -- exactly the split
 * syscall_net.c / syscall_net_init.c uses.
 *
 * The backend window id is simply the compositor window id (uint32_t, always
 * >0 for a live window). All surface writes are clipped to the surface bounds
 * here; the handler layer has already rejected oversized / overflowing
 * requests, so this is defence in depth. A ring-3 app never sees these pointers.
 */

#include "kernel/syscall_gfx.h"
#include "kernel/process.h"

#include "gui/compositor.h"
#include "gui/event.h"

#include <stddef.h>
#include <stdint.h>

/* Create a centered WxH window, show + focus it, and return its compositor id
 * as the backend id. Returns -1 fail-closed when the compositor refuses (table
 * full / surface allocation failed). */
static int32_t gfx_backend_win_create(const char *title, uint32_t w,
                                      uint32_t h) {
  uint32_t screen_w = 0u, screen_h = 0u;
  int32_t x = 0, y = 0;
  struct gui_window *win;

  compositor_screen_size(&screen_w, &screen_h);
  if (screen_w > w) x = (int32_t)((screen_w - w) / 2u);
  if (screen_h > h) y = (int32_t)((screen_h - h) / 2u);

  win = compositor_create_window(title, x, y, w, h);
  if (!win) return -1;
  compositor_show_window(win->id);
  compositor_focus_window(win->id);
  return (int32_t)win->id;
}

static void gfx_backend_win_destroy(int32_t backend_id) {
  if (backend_id > 0) compositor_destroy_window((uint32_t)backend_id);
}

static int gfx_backend_surface_info(int32_t backend_id, uint32_t *w,
                                    uint32_t *h) {
  struct gui_window *win = compositor_get_window((uint32_t)backend_id);
  if (!win || !win->surface.pixels) return -1;
  if (w) *w = win->surface.width;
  if (h) *h = win->surface.height;
  return 0;
}

static int gfx_backend_surface_fill(int32_t backend_id, uint32_t x, uint32_t y,
                                    uint32_t w, uint32_t h, uint32_t argb) {
  struct gui_window *win = compositor_get_window((uint32_t)backend_id);
  uint32_t sw, sh, row, col, x1, y1;
  struct gui_rect dirty;
  if (!win || !win->surface.pixels) return -1;
  sw = win->surface.width;
  sh = win->surface.height;
  if (x >= sw || y >= sh) return 0; /* fully outside: nothing to do */
  x1 = (x + w < sw) ? (x + w) : sw; /* clip right/bottom to surface */
  y1 = (y + h < sh) ? (y + h) : sh;
  for (row = y; row < y1; ++row) {
    uint32_t *line = win->surface.pixels + (size_t)row * sw;
    for (col = x; col < x1; ++col) line[col] = argb;
  }
  dirty.x = (int32_t)x;
  dirty.y = (int32_t)y;
  dirty.width = x1 - x;
  dirty.height = y1 - y;
  compositor_invalidate_rect((uint32_t)backend_id, &dirty);
  return 0;
}

static int gfx_backend_surface_blit(int32_t backend_id, const uint32_t *src,
                                    uint32_t sw, uint32_t sh, uint32_t dx,
                                    uint32_t dy) {
  struct gui_window *win = compositor_get_window((uint32_t)backend_id);
  uint32_t dst_w, dst_h, row, col, cols, rows;
  struct gui_rect dirty;
  if (!win || !win->surface.pixels || !src) return -1;
  dst_w = win->surface.width;
  dst_h = win->surface.height;
  if (dx >= dst_w || dy >= dst_h) return 0; /* origin outside surface */
  cols = (dx + sw < dst_w) ? sw : (dst_w - dx); /* clip to surface */
  rows = (dy + sh < dst_h) ? sh : (dst_h - dy);
  for (row = 0u; row < rows; ++row) {
    uint32_t *dline = win->surface.pixels + (size_t)(dy + row) * dst_w + dx;
    const uint32_t *sline = src + (size_t)row * sw; /* CR3 is caller's */
    for (col = 0u; col < cols; ++col) dline[col] = sline[col];
  }
  dirty.x = (int32_t)dx;
  dirty.y = (int32_t)dy;
  dirty.width = cols;
  dirty.height = rows;
  compositor_invalidate_rect((uint32_t)backend_id, &dirty);
  return 0;
}

static void gfx_backend_win_present(int32_t backend_id) {
  /* Mark the whole window dirty; the desktop runtime loop composites it on its
   * next frame. (The dedicated graphical boot smoke, which has no desktop loop,
   * drives an explicit compositor_render itself.) */
  compositor_invalidate((uint32_t)backend_id);
}

static int gfx_backend_poll_event(int32_t backend_id,
                                  struct capy_gfx_event *out) {
  struct gui_event ev;
  struct gui_window *win;
  int32_t lx, ly;
  if (!out) return -1;
  if (gui_event_poll(&ev) != 1) return 0; /* queue empty */

  /* Deliver only events for this window; global pointer events (window_id 0)
   * are localized against this window's frame. Anything else is dropped (not
   * this app's business). */
  if (ev.window_id != 0u && ev.window_id != (uint32_t)backend_id) return 0;

  win = compositor_get_window((uint32_t)backend_id);
  lx = ev.mouse.x - (win ? win->frame.x : 0);
  ly = ev.mouse.y - (win ? win->frame.y : 0);

  switch (ev.type) {
  case GUI_EVENT_KEY_DOWN:
    out->kind = CAPY_GFX_EV_KEY_DOWN;
    out->code = ev.key.keycode;
    out->mods = ev.key.modifiers;
    return 1;
  case GUI_EVENT_KEY_UP:
    out->kind = CAPY_GFX_EV_KEY_UP;
    out->code = ev.key.keycode;
    out->mods = ev.key.modifiers;
    return 1;
  case GUI_EVENT_MOUSE_MOVE:
    out->kind = CAPY_GFX_EV_MOUSE_MOVE;
    out->x = lx;
    out->y = ly;
    out->dx = ev.mouse.dx;
    out->dy = ev.mouse.dy;
    out->code = ev.mouse.buttons;
    return 1;
  case GUI_EVENT_MOUSE_DOWN:
    out->kind = CAPY_GFX_EV_MOUSE_DOWN;
    out->x = lx;
    out->y = ly;
    out->code = ev.mouse.buttons;
    return 1;
  case GUI_EVENT_MOUSE_UP:
    out->kind = CAPY_GFX_EV_MOUSE_UP;
    out->x = lx;
    out->y = ly;
    out->code = ev.mouse.buttons;
    return 1;
  case GUI_EVENT_MOUSE_SCROLL:
    out->kind = CAPY_GFX_EV_SCROLL;
    out->x = lx;
    out->y = ly;
    out->dy = ev.mouse.dy;
    return 1;
  case GUI_EVENT_WINDOW_CLOSE:
    out->kind = CAPY_GFX_EV_CLOSE;
    return 1;
  case GUI_EVENT_WINDOW_RESIZE:
    out->kind = CAPY_GFX_EV_RESIZE;
    out->x = ev.resize.width;
    out->y = ev.resize.height;
    return 1;
  default:
    /* FOCUS / BLUR / PAINT / TIMER / NONE are not part of the ring-3 ABI. */
    return 0;
  }
}

static const struct syscall_gfx_ops g_compositor_gfx_ops = {
    gfx_backend_win_create,   gfx_backend_win_destroy,
    gfx_backend_surface_info, gfx_backend_surface_fill,
    gfx_backend_surface_blit, gfx_backend_win_present,
    gfx_backend_poll_event,
};

void syscall_gfx_install_default_ops(void) {
  syscall_gfx_install_ops(&g_compositor_gfx_ops);
  /* Wire the process-teardown observer so a process that exits or is reaped
   * has its owned compositor windows destroyed (no dangling window / handle). */
  process_register_teardown_observer(syscall_gfx_release_owner);
}
