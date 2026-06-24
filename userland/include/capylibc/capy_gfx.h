#ifndef CAPYLIBC_CAPY_GFX_H
#define CAPYLIBC_CAPY_GFX_H

/* CapyOS minimal C library — ring-3 graphical surface API (Etapa 7 / Slice 7.2).
 *
 * Thin wrappers around SYS_WINDOW_CREATE..SYS_WINDOW_DESTROY (45..50), the first
 * userland-facing window/surface syscalls. A ring-3 app composes pixels in its
 * OWN ARGB32 buffer and hands them to the kernel via capy_surface_blit /
 * capy_surface_fill; it refers to its window only by an opaque handle the kernel
 * tracks as owned by the calling process. The app never receives a pointer into
 * a kernel surface (Option A isolation). All entries return -1 when an argument
 * is invalid, the handle is not owned by the caller, or no compositor backend is
 * installed yet. ABI structs/limits come from the shared header below.
 *
 * The kernel-side handlers live in src/kernel/syscall_gfx.c; the asm stubs in
 * userland/lib/capylibc/syscall_stubs.S; tests/userland/test_capylibc_abi.c
 * statically asserts the syscall numbers agree with the kernel. */

#include <stddef.h>
#include <stdint.h>

#include "kernel/syscall_gfx_abi.h" /* struct capy_gfx_event, CAPY_GFX_* */

#ifdef __cplusplus
extern "C" {
#endif

/* Create a w x h window titled `title` (a NUL-terminated string; NULL -> the
 * backend default). Returns a window handle > 0 on success, or -1 (bad
 * dimensions, window table full, no backend). Dimensions are capped at
 * CAPY_GFX_MAX_DIM. */
int capy_window_create(const char *title, unsigned int w, unsigned int h);

/* Fill the rectangle [x, y, w, h] (pixels, window-local) of the window's
 * surface with `argb` (0xAARRGGBB). Clipped to the surface. Returns 0 | -1. */
int capy_surface_fill(int handle, unsigned int x, unsigned int y,
                      unsigned int w, unsigned int h, unsigned int argb);

/* Blit `src` (sw*sh ARGB32 pixels, row stride == sw, in the caller's address
 * space) into the window surface at (dx, dy). Clipped to the surface. Bounded
 * by CAPY_GFX_BLIT_MAX_PIXELS. Returns 0 | -1. */
int capy_surface_blit(int handle, const unsigned int *src, unsigned int sw,
                      unsigned int sh, unsigned int dx, unsigned int dy);

/* Pop one pending event for the window into *out. Returns 1 if an event was
 * written, 0 if none pending (non-blocking), -1 on error. */
int capy_window_poll_event(int handle, struct capy_gfx_event *out);

/* Mark the window for presentation (the compositor composites it on its next
 * frame). Returns 0 | -1. */
int capy_window_present(int handle);

/* Destroy the window and release its handle. Returns 0 | -1. Windows are also
 * destroyed automatically when the owning process exits. */
int capy_window_destroy(int handle);

#ifdef __cplusplus
}
#endif

#endif /* CAPYLIBC_CAPY_GFX_H */
