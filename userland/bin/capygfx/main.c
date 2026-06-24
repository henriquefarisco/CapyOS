/*
 * userland/bin/capygfx/main.c — CapyGFX ring-3 graphical surface smoke (Etapa 7
 * / Slice 7.2.2).
 *
 * The first ring-3 consumer of the Slice 7.2 graphical surface ABI. It proves
 * the whole path end-to-end against the real kernel compositor: create a window
 * (SYS_WINDOW_CREATE), fill it (SYS_SURFACE_FILL), compose an ARGB32 image in
 * its OWN buffer and blit it in (SYS_SURFACE_BLIT), present it
 * (SYS_WINDOW_PRESENT) and poll input once (SYS_WINDOW_POLL_EVENT). It exits 0
 * only if every graphical syscall succeeded; the kernel latches that success
 * into the COM1 marker `[smoke] capygfx ready`. On exit the kernel's
 * process-teardown observer destroys the owned window (exercising the cleanup
 * path). Any failure prints a diagnostic on stdout (debugcon) and exits 1.
 *
 * Deliberately self-contained: it composes pixels directly (no display-list /
 * CapyBrowser dependency), so the smoke isolates the ring-3 graphical ABI. The
 * display-list -> pixels rasterizer (browser_render_pixel) is proven separately
 * by host tests. Ring-3 conventions mirror hello/capybrowse: no host libc, bulk
 * state in .bss (the ELF loader zeroes it).
 */

#include <capylibc/capylibc.h>
#include <capylibc/capy_gfx.h>

#define CAPYGFX_W 320u
#define CAPYGFX_H 240u

/* Composition buffer in .bss (zeroed by the loader): the app owns these pixels
 * and hands them to the kernel via a bounds-checked blit. */
static unsigned int g_fb[CAPYGFX_W * CAPYGFX_H];

static unsigned long cb_strlen(const char *s) {
  unsigned long n = 0ul;
  while (s[n]) n++;
  return n;
}
static void cb_print(const char *s) { capy_write(1, s, cb_strlen(s)); }

static void fail(const char *why) {
  cb_print("capygfx: FAIL ");
  cb_print(why);
  cb_print("\n");
  capy_exit(1);
}

/* Paint a deterministic, recognizable image into the app's own buffer: a
 * vertical gradient with an accent header band and a centered solid block. */
static void compose(void) {
  unsigned int x, y;
  for (y = 0u; y < CAPYGFX_H; ++y) {
    unsigned int g = (y * 255u) / (CAPYGFX_H - 1u);
    unsigned int row = 0xFF000000u | (g << 8) | (0x40u + (g >> 2)); /* teal-ish */
    for (x = 0u; x < CAPYGFX_W; ++x) {
      unsigned int px = row;
      if (y < 24u) px = 0xFF1A4FD0u;                 /* header band (blue) */
      else if (x >= 120u && x < 200u && y >= 100u && y < 140u)
        px = 0xFFF5C542u;                            /* center block (amber) */
      g_fb[y * CAPYGFX_W + x] = px;
    }
  }
}

int main(int rank) {
  int win;
  struct capy_gfx_event ev;
  (void)rank;

  win = capy_window_create("CapyGFX 7.2.2", CAPYGFX_W, CAPYGFX_H);
  if (win <= 0) fail("window_create");

  /* Clear to a dark background first (exercises SYS_SURFACE_FILL), then a small
   * accent rect (a second fill). */
  if (capy_surface_fill((int)win, 0u, 0u, CAPYGFX_W, CAPYGFX_H, 0xFF101820u) != 0)
    fail("surface_fill bg");
  if (capy_surface_fill((int)win, 0u, 0u, CAPYGFX_W, 4u, 0xFF1A4FD0u) != 0)
    fail("surface_fill bar");

  /* Compose in our own buffer and blit it in (exercises SYS_SURFACE_BLIT). */
  compose();
  if (capy_surface_blit((int)win, g_fb, CAPYGFX_W, CAPYGFX_H, 0u, 0u) != 0)
    fail("surface_blit");

  /* Present (SYS_WINDOW_PRESENT) and poll input once, non-blocking
   * (SYS_WINDOW_POLL_EVENT: 0 = no event pending is the expected smoke path). */
  if (capy_window_present((int)win) != 0) fail("window_present");
  if (capy_window_poll_event((int)win, &ev) < 0) fail("window_poll_event");

  cb_print("capygfx: window+fill+blit+present+poll ok\n");
  /* Exit 0; the kernel latches the COM1 marker and the teardown observer
   * destroys the owned window. */
  capy_exit(0);
  return 0;
}
