#ifndef KERNEL_SYSCALL_GFX_ABI_H
#define KERNEL_SYSCALL_GFX_ABI_H

/* Etapa 7 / Slice 7.2 — shared ring-3 <-> kernel ABI for the graphical
 * surface syscalls (SYS_WINDOW_CREATE..SYS_WINDOW_DESTROY).
 *
 * Pure data: only <stdint.h>, no kernel/userland-private types. Included by
 * BOTH the kernel handler module (include/kernel/syscall_gfx.h) AND the ring-3
 * wrapper (userland/include/capylibc/capy_gfx.h), so the two sides agree by
 * construction -- the same single-source-of-truth discipline that
 * include/kernel/syscall_numbers.h uses for the asm stubs. Additive only:
 * append event kinds / fields; never renumber or repurpose.
 *
 * Isolation model (Option A): a ring-3 app NEVER receives a pointer into a
 * kernel window surface. It composes pixels in its OWN address space and hands
 * the buffer to SYS_SURFACE_BLIT, which the kernel bounds-checks and copies in.
 * The app refers to its window only by an opaque small-integer handle the
 * kernel tracks as owned by the calling process. */

#include <stdint.h>

/* Pixel format for every surface syscall: 32-bit ARGB (0xAARRGGBB), one
 * uint32_t per pixel, rows top-to-bottom, tightly packed (row stride == width).
 * The kernel ignores the alpha byte for opaque blits but preserves the layout. */

/* Fail-closed upper bound for a single window/surface dimension requested from
 * ring 3. Mirrors the compositor's own COMPOSITOR_MAX_SURFACE_DIM so a hostile
 * app cannot drive an oversized allocation; the kernel re-checks regardless. */
#define CAPY_GFX_MAX_DIM 32768u

/* Maximum pixels accepted in a single SYS_SURFACE_BLIT. Bounds the in-kernel
 * copy from the caller's buffer so one syscall cannot turn into an unbounded
 * kernel-side read/write; larger surfaces must be blitted in tiles. 16 Mpx. */
#define CAPY_GFX_BLIT_MAX_PIXELS (16u * 1024u * 1024u)

/* Window event kinds delivered by SYS_WINDOW_POLL_EVENT. Additive. */
enum capy_gfx_event_kind {
  CAPY_GFX_EV_NONE = 0,
  CAPY_GFX_EV_KEY_DOWN = 1,
  CAPY_GFX_EV_KEY_UP = 2,
  CAPY_GFX_EV_MOUSE_MOVE = 3,
  CAPY_GFX_EV_MOUSE_DOWN = 4,
  CAPY_GFX_EV_MOUSE_UP = 5,
  CAPY_GFX_EV_SCROLL = 6,
  CAPY_GFX_EV_CLOSE = 7,
  CAPY_GFX_EV_RESIZE = 8
};

/* One window/input event for a ring-3 window. Fixed layout (ABI). Geometry is
 * window-local (relative to the window's top-left content origin).
 *
 *   KEY_DOWN / KEY_UP   : code = keycode, mods = modifier bitmask.
 *   MOUSE_MOVE/DOWN/UP  : x,y = local cursor; code = button bitmask.
 *   SCROLL              : dy = wheel delta (lines, signed).
 *   RESIZE              : x = new width, y = new height.
 *   CLOSE               : no payload (user requested window close).
 *
 * `reserved` keeps the struct a round 32 bytes and must be written 0 by the
 * kernel; future fields claim it additively. */
struct capy_gfx_event {
  uint32_t kind; /* enum capy_gfx_event_kind */
  uint32_t code; /* keycode or button mask */
  int32_t x;
  int32_t y;
  int32_t dx;
  int32_t dy;
  uint32_t mods;
  uint32_t reserved;
};

#endif /* KERNEL_SYSCALL_GFX_ABI_H */
