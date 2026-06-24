/*
 * tests/userland/test_browser_render_pixel.c — host tests for the CapyOS pixel
 * render backend for the capy-browser-core display list (Etapa 7 / Slice 7.2).
 *
 * Gated by CAPYOS_HAVE_CAPYBROWSER_CORE (set by the Makefile when the sibling's
 * display_list.h is present); a no-op otherwise so the suite still links
 * (mirrors test_browser_render). Display lists are plain data, so the tests
 * build them by hand and assert deterministic ARGB pixels. Cell size is forced
 * to 8x8 so the shared 8x8 font maps 1:1 and individual glyph pixels are
 * predictable.
 */

#include <stdio.h>

#ifdef CAPYOS_HAVE_CAPYBROWSER_CORE

#include <string.h>

#include "browser_render_pixel.h"

/* The shared console font, linked from src/gui/core/font8x8_data.o; used here
 * to derive the expected 'A' glyph pixels independently of the renderer. */
extern const uint8_t font8x8_basic[128][8];

static int g_runs;
static int g_passes;

#define CHECK(cond, msg)                                                       \
  do {                                                                         \
    g_runs++;                                                                  \
    if (cond) {                                                                \
      g_passes++;                                                              \
    } else {                                                                   \
      printf("  FAIL: %s\n", (msg));                                           \
    }                                                                          \
  } while (0)

#define PW 64u
#define PH 32u

static struct capy_dl g_dl;
static uint32_t g_px[PW * PH];

static void dl_reset(void) {
  memset(&g_dl, 0, sizeof(g_dl));
  g_dl.version = CAPY_DL_VERSION;
}

static void dl_str(const char *s, size_t *off, size_t *len) {
  size_t n = strlen(s);
  *off = g_dl.string_len;
  memcpy(&g_dl.strings[g_dl.string_len], s, n);
  g_dl.string_len += n;
  *len = n;
}

static void dl_text(long x, long y, const char *s, const char *color) {
  struct capy_dl_node *nd = &g_dl.nodes[g_dl.node_count++];
  nd->kind = CAPY_DL_TEXT;
  nd->x = x;
  nd->y = y;
  nd->width = (long)strlen(s);
  nd->height = 1;
  dl_str(s, &nd->text_off, &nd->text_len);
  if (color) dl_str(color, &nd->color_off, &nd->color_len);
}

static void dl_rect(long x, long y, long w, long h, const char *color) {
  struct capy_dl_node *nd = &g_dl.nodes[g_dl.node_count++];
  nd->kind = CAPY_DL_RECT;
  nd->x = x;
  nd->y = y;
  nd->width = w;
  nd->height = h;
  if (color) dl_str(color, &nd->color_off, &nd->color_len);
}

static void dl_image(long x, long y, long w, long h, const char *label) {
  struct capy_dl_node *nd = &g_dl.nodes[g_dl.node_count++];
  nd->kind = CAPY_DL_IMAGE;
  nd->x = x;
  nd->y = y;
  nd->width = w;
  nd->height = h;
  if (label) dl_str(label, &nd->label_off, &nd->label_len);
}

static void dl_link(long x, long y, long w, long h, const char *url) {
  struct capy_dl_node *nd = &g_dl.nodes[g_dl.node_count++];
  nd->kind = CAPY_DL_LINK;
  nd->x = x;
  nd->y = y;
  nd->width = w;
  nd->height = h;
  if (url) dl_str(url, &nd->url_off, &nd->url_len);
}

static struct capyos_browser_pixel_opts opts88(void) {
  struct capyos_browser_pixel_opts o;
  o.cell_w = 8u;
  o.cell_h = 8u;
  o.bg = 0xFFFFFFFFu;
  o.fg = 0xFF112233u;
  o.link = 0xFF1A4FD0u;
  o.resolve_image = 0;
  o.image_ctx = 0;
  return o;
}

#endif /* CAPYOS_HAVE_CAPYBROWSER_CORE */

int test_browser_render_pixel_run(void) {
#ifdef CAPYOS_HAVE_CAPYBROWSER_CORE
  struct capyos_browser_pixel_opts o = opts88();
  struct capyos_browser_pixel_stats st;
  int rc;

  printf("[test_browser_render_pixel]\n");
  g_runs = 0;
  g_passes = 0;

  /* 1. Empty display list -> the whole surface is the background color. */
  dl_reset();
  rc = capyos_browser_render_pixels(&g_dl, g_px, PW, PH, &o, &st);
  CHECK(rc == 0, "empty dl renders ok");
  CHECK(g_px[0] == 0xFFFFFFFFu && g_px[PW * PH - 1u] == 0xFFFFFFFFu,
        "background filled with bg color");

  /* 2. RECT with a hex color fills its cell box. */
  dl_reset();
  dl_rect(0, 0, 1, 1, "#00FF00");
  capyos_browser_render_pixels(&g_dl, g_px, PW, PH, &o, &st);
  CHECK(g_px[0] == 0xFF00FF00u, "rect #00FF00 fills (0,0)");
  CHECK(g_px[7 * PW + 7] == 0xFF00FF00u, "rect covers the 8x8 cell");
  CHECK(g_px[8] == 0xFFFFFFFFu, "rect does not bleed past its cell");
  CHECK(st.rect_nodes == 1u, "rect node counted");

  /* 3. TEXT 'A' at (0,0): the glyph's set bits become fg, gaps stay bg.
   *    'A' row0 = 0x38 = 00111000 (MSB=left) -> cols 2,3,4 set. */
  dl_reset();
  dl_text(0, 0, "A", NULL);
  capyos_browser_render_pixels(&g_dl, g_px, PW, PH, &o, &st);
  CHECK((font8x8_basic['A'][0] & (0x80u >> 2)) != 0, "fixture: A row0 col2 set");
  CHECK(g_px[0 * PW + 2] == 0xFF112233u, "glyph 'A' pixel (2,0) is fg");
  CHECK(g_px[0 * PW + 0] == 0xFFFFFFFFu, "glyph 'A' gap (0,0) stays bg");
  CHECK(st.text_nodes == 1u && st.glyphs_drawn == 1u, "text node + glyph drawn");

  /* 4. TEXT color override is honored. */
  dl_reset();
  dl_text(0, 0, "A", "#FF0000");
  capyos_browser_render_pixels(&g_dl, g_px, PW, PH, &o, &st);
  CHECK(g_px[0 * PW + 2] == 0xFFFF0000u, "text color override applied");

  /* 5. LINK underline at the bottom of its cell band, in the link color. */
  dl_reset();
  dl_link(0, 0, 2, 1, "https://example.com/");
  capyos_browser_render_pixels(&g_dl, g_px, PW, PH, &o, &st);
  CHECK(g_px[7 * PW + 0] == 0xFF1A4FD0u, "link underline at row 7 col 0");
  CHECK(g_px[7 * PW + 15] == 0xFF1A4FD0u, "link underline spans 2 cells");
  CHECK(g_px[0] == 0xFFFFFFFFu, "link does not fill its whole box");
  CHECK(st.link_nodes == 1u, "link node counted");

  /* 6. IMAGE placeholder: bordered light box. */
  dl_reset();
  dl_image(2, 1, 3, 2, "x"); /* box origin (16,8), size 24x16 */
  capyos_browser_render_pixels(&g_dl, g_px, PW, PH, &o, &st);
  CHECK(g_px[8 * PW + 16] == 0xFF888888u, "image border top-left");
  CHECK(g_px[21 * PW + 34] == 0xFFE8E8E8u, "image interior fill");
  CHECK(st.image_nodes == 1u, "image node counted");

  /* 7. Fail-closed: a version mismatch leaves the surface untouched. */
  dl_reset();
  g_dl.version = 999; /* not CAPY_DL_VERSION */
  g_px[0] = 0xDEADBEEFu;
  rc = capyos_browser_render_pixels(&g_dl, g_px, PW, PH, &o, &st);
  CHECK(rc == -1 && st.truncated == 1, "version mismatch -> -1 + truncated");
  CHECK(g_px[0] == 0xDEADBEEFu, "version mismatch leaves surface untouched");

  /* 8. NULL surface -> -1. */
  dl_reset();
  rc = capyos_browser_render_pixels(&g_dl, NULL, PW, PH, &o, &st);
  CHECK(rc == -1, "NULL out -> -1");

  /* 9. NULL opts uses defaults (white bg) and still renders. */
  dl_reset();
  dl_rect(0, 0, 1, 1, "blue");
  rc = capyos_browser_render_pixels(&g_dl, g_px, PW, PH, NULL, &st);
  CHECK(rc == 0 && g_px[0] == 0xFF0000FFu, "named color 'blue' with default opts");

  printf("  %d/%d checks passed\n", g_passes, g_runs);
  return g_passes == g_runs ? 0 : 1;
#else
  return 0;
#endif
}
