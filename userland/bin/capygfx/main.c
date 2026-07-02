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

/* Etapa 7 / Slice 7.3 (runtime proof): when built with the CapyBrowser graphical
 * core present, capygfx renders a REAL embedded HTML page through the decoupled
 * pipeline (HTML -> DOM -> CSS -> cascade -> layout -> display-list) and the 7.2
 * pixel rasterizer, then blits it — proving HTML -> pixels -> window in ring-3
 * against the real compositor. Without the core, it falls back to a
 * self-composed pattern (still exercises the full graphical syscall path).
 *
 * Slice 7.4b: when CapyCodecs is ALSO present, the embedded page carries an
 * <img> and a resolver decodes an embedded PNG IN RING-3 (via the CapyCodecs
 * core + the in-tree tinf inflater) into ARGB32, which the rasterizer blits at
 * the IMAGE node's box — proving inline image decode end-to-end in ring-3 (the
 * smoke fails closed if images_decoded < 1). */
#ifdef CAPYOS_HAVE_CAPYBROWSER_CORE
#include "browser_pipeline.h"
#include "browser_render_pixel.h"
#ifdef CAPYOS_HAVE_CAPYCODECS_IMAGE
#include "browser_image.h"
/* A real 2x2 RGB PNG (pixels red/green/blue/white) embedded so the resolver can
 * decode it IN RING-3 via the CapyCodecs core + the in-tree tinf inflater,
 * proving inline image decode end-to-end (bytes -> ARGB32 -> scaled blit). */
static const unsigned char g_logo_png[] = {
    0x89, 0x50, 0x4e, 0x47, 0x0d, 0x0a, 0x1a, 0x0a, 0x00, 0x00, 0x00,
    0x0d, 0x49, 0x48, 0x44, 0x52, 0x00, 0x00, 0x00, 0x02, 0x00, 0x00,
    0x00, 0x02, 0x08, 0x02, 0x00, 0x00, 0x00, 0xfd, 0xd4, 0x9a, 0x73,
    0x00, 0x00, 0x00, 0x12, 0x49, 0x44, 0x41, 0x54, 0x78, 0xda, 0x63,
    0xf8, 0xcf, 0xc0, 0xc0, 0x00, 0xc2, 0x0c, 0xff, 0x81, 0x00, 0x00,
    0x1f, 0xee, 0x05, 0xfb, 0xf1, 0xab, 0xba, 0x77, 0x00, 0x00, 0x00,
    0x00, 0x49, 0x45, 0x4e, 0x44, 0xae, 0x42, 0x60, 0x82};
/* Image resolver: decode the embedded PNG (ignoring the src) and hand back its
 * ARGB32 pixels for the rasterizer to blit (scaled) at the IMAGE node's box. */
static int cb_resolve_image(void *ctx, const char *src, size_t src_len,
                            const uint32_t **px, uint32_t *w, uint32_t *h) {
  struct capyos_image img;
  (void)ctx;
  (void)src;
  (void)src_len;
  if (capyos_image_decode(g_logo_png, sizeof(g_logo_png), &img) != 0) return 0;
  *px = img.pixels;
  *w = img.width;
  *h = img.height;
  return 1;
}
#endif /* CAPYOS_HAVE_CAPYCODECS_IMAGE */
/* Etapa 7 / Slice 7.5 (alpha.303): OPT-IN sub-resource-over-the-network smoke.
 * OFF by default (a new, separate compile define -- CAPYGFX_NET_IMAGE_SMOKE --
 * never implied by CAPYOS_HAVE_CAPYGFX_NET alone), so the proven alpha.294
 * embedded-image smoke (CAPYOS_GFX_SMOKE, no networking) is byte-for-byte
 * unchanged unless a NEW dedicated smoke target opts in. When active, the
 * resolver fetches the <img> src for real over the network through a
 * persistent browser_fetch_ctx, gated by the mixed-content policy
 * (browser_fetch_subresource_allowed) exactly like a real page load would be,
 * then decodes the fetched bytes with the SAME CapyCodecs adapter used for the
 * embedded image. Fails closed (returns 0, no fallback) on any blocked/failed
 * fetch/decode so the smoke's pass/fail genuinely reflects this path. */
#if defined(CAPYOS_HAVE_CAPYGFX_NET) && defined(CAPYGFX_NET_IMAGE_SMOKE)
#include "browser_fetch.h"
#ifndef CAPYGFX_IMAGE_URL
#define CAPYGFX_IMAGE_URL "http://10.0.2.2:18082/logo.png"
#endif
/* Bulk state in .bss: the fetch context holds the ~0.5 MiB cache, far too
 * large for the stack. Persistent across the (single) resolve call the smoke
 * makes, mirroring the multi-fetch runtime's own ownership convention. */
static struct browser_fetch_ctx g_net_ctx;
static struct http_response g_net_resp;

static int cb_resolve_image_net(void *ctx, const char *src, size_t src_len,
                                const uint32_t **px, uint32_t *w,
                                uint32_t *h) {
  struct capyos_image img;
  char url[BROWSER_FETCH_URL_MAX];
  size_t i;
  static int s_ctx_ready;
  (void)ctx;
  if (!s_ctx_ready) {
    browser_fetch_init(&g_net_ctx);
    s_ctx_ready = 1;
  }
  if (!src || src_len == 0u || src_len >= sizeof(url)) return 0;
  for (i = 0u; i < src_len; ++i) url[i] = src[i];
  url[src_len] = '\0';

  /* Mixed-content gate: this smoke's own page is served over plain HTTP (see
   * the hermetic QEMU endpoint), so page_secure=0 -- an http sub-resource on
   * an http page is same-scheme and allowed. The BLOCK path (http image on an
   * https page) is exhaustively host-tested in tests/net/test_browser_fetch.c
   * and needs no network to verify; this smoke's job is proving the ALLOW +
   * real-fetch + decode path end-to-end against a real server. */
  if (!browser_fetch_subresource_allowed(0, url)) return 0;

  if (browser_fetch_get(&g_net_ctx, url, &g_net_resp, 0) < 0) return 0;
  if (g_net_resp.status_code < 200 || g_net_resp.status_code >= 300) return 0;
  if (capyos_image_decode(g_net_resp.body, g_net_resp.body_len, &img) != 0)
    return 0;

  *px = img.pixels;
  *w = img.width;
  *h = img.height;
  return 1;
}
#endif /* CAPYOS_HAVE_CAPYGFX_NET && CAPYGFX_NET_IMAGE_SMOKE */
#if defined(CAPYOS_HAVE_CAPYGFX_NET) && defined(CAPYGFX_NET_IMAGE_SMOKE)
static const char g_html[] =
    "<html><body>"
    "<h1>CapyOS</h1>"
    "<p>Navegador grafico: pipeline HTML para pixels (imagem pela rede).</p>"
    "<img src=\"" CAPYGFX_IMAGE_URL "\">"
    "<p>Veja <a href=\"sobre.html\">sobre</a>.</p>"
    "</body></html>";
#else
static const char g_html[] =
    "<html><body>"
    "<h1>CapyOS</h1>"
    "<p>Navegador grafico: pipeline HTML para pixels.</p>"
    "<img src=\"logo.png\">"
    "<p>Veja <a href=\"sobre.html\">sobre</a>.</p>"
    "</body></html>";
#endif
#endif

#define CAPYGFX_W 320u
#define CAPYGFX_H 240u

/* Etapa 7 / Slice 7.5 (alpha.304): OPT-IN interactive-desktop-launch mode. OFF
 * by default (never implied by CAPYOS_HAVE_CAPYGFX_NET or any other existing
 * define), so every proven boot-smoke behavior (alpha.290/292/294/303: single
 * poll then exit) stays byte-for-byte unchanged. When active (built for
 * kernel_spawn_capygfx_desktop, i.e. launched from a running desktop session
 * instead of a boot-exclusive smoke), the app stays open, cooperatively
 * yielding between polls, until it observes a WINDOW_CLOSE event (the
 * title-bar X -- a compositor lifecycle event already pushed to
 * gui_event_poll in production, see src/gui/core/compositor.c) or a
 * generous iteration cap is hit (fail-safe: must never wedge the desktop's
 * cooperative scheduler forever). Mouse/keyboard are NOT yet bridged to this
 * event queue in production (only window lifecycle events are), so this loop
 * cannot yet react to clicks/typing -- see docs/releases for the tracked gap. */
#ifdef CAPYGFX_DESKTOP_INTERACTIVE
#ifndef CAPYGFX_DESKTOP_POLL_MAX_ITERS
#define CAPYGFX_DESKTOP_POLL_MAX_ITERS 12000u
#endif
#ifndef CAPYGFX_DESKTOP_POLL_SLEEP_TICKS
#define CAPYGFX_DESKTOP_POLL_SLEEP_TICKS 5u
#endif
#endif

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

#ifndef CAPYOS_HAVE_CAPYBROWSER_CORE
/* Paint a deterministic, recognizable image into the app's own buffer: a
 * vertical gradient with an accent header band and a centered solid block.
 * Fallback when the CapyBrowser core is absent (no real page to render). */
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
#endif /* !CAPYOS_HAVE_CAPYBROWSER_CORE */

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

  /* Compose into our own buffer: a real HTML page via the pipeline when the
   * CapyBrowser core is present, else a self-composed pattern. Then blit it in
   * (exercises SYS_SURFACE_BLIT). */
#ifdef CAPYOS_HAVE_CAPYBROWSER_CORE
  {
    const struct capy_dl *dl;
    struct capyos_browser_pipeline_stats ps;
    struct capyos_browser_pixel_opts o;
    struct capyos_browser_pixel_stats rs;
    /* Drive HTML+CSS through the five decoupled core stages -> display list. */
    dl = capyos_browser_build_display_list(g_html, cb_strlen(g_html), "", 0u,
                                           "https://capy.example/",
                                           (long)(CAPYGFX_W / 8u), &ps);
    if (dl == 0) fail("pipeline");
    /* Rasterize the real display list into our buffer (cell 8x16). */
    o.cell_w = 8u;
    o.cell_h = 16u;
    o.bg = 0xFFFFFFFFu;
    o.fg = 0xFF111111u;
    o.link = 0xFF1A4FD0u;
    o.resolve_image = 0;
    o.image_ctx = 0;
#ifdef CAPYOS_HAVE_CAPYCODECS_IMAGE
    o.resolve_image = cb_resolve_image; /* decode the embedded image in ring-3 */
#endif
#if defined(CAPYOS_HAVE_CAPYGFX_NET) && defined(CAPYGFX_NET_IMAGE_SMOKE)
    o.resolve_image = cb_resolve_image_net; /* fetch the image over the network */
#endif
    if (capyos_browser_render_pixels(dl, g_fb, CAPYGFX_W, CAPYGFX_H, &o, &rs) != 0)
      fail("rasterize");
#ifdef CAPYOS_HAVE_CAPYCODECS_IMAGE
    /* Prove the inline image actually decoded + drew in ring-3 (not placeholder). */
    if (rs.images_decoded < 1u) fail("image decode (ring-3)");
#endif
  }
#else
  compose();
#endif
  if (capy_surface_blit((int)win, g_fb, CAPYGFX_W, CAPYGFX_H, 0u, 0u) != 0)
    fail("surface_blit");

  /* Present (SYS_WINDOW_PRESENT) and poll input once, non-blocking
   * (SYS_WINDOW_POLL_EVENT: 0 = no event pending is the expected smoke path). */
  if (capy_window_present((int)win) != 0) fail("window_present");
  if (capy_window_poll_event((int)win, &ev) < 0) fail("window_poll_event");

#ifdef CAPYGFX_DESKTOP_INTERACTIVE
  /* Stay open: cooperatively yield + poll until WINDOW_CLOSE or the fail-safe
   * iteration cap (see the doc comment near CAPYGFX_DESKTOP_POLL_MAX_ITERS). */
  {
    unsigned iters;
    for (iters = 0u; iters < CAPYGFX_DESKTOP_POLL_MAX_ITERS; ++iters) {
      struct capy_gfx_event pev;
      int pr = capy_window_poll_event((int)win, &pev);
      if (pr > 0 && pev.kind == CAPY_GFX_EV_CLOSE) break;
      capy_yield();
      capy_sleep(CAPYGFX_DESKTOP_POLL_SLEEP_TICKS);
    }
  }
  cb_print("capygfx: window closed (or poll cap reached)\n");
#else
  cb_print("capygfx: window+fill+blit+present+poll ok\n");
#endif
  /* Exit 0; the kernel latches the COM1 marker and the teardown observer
   * destroys the owned window. */
  capy_exit(0);
  return 0;
}
