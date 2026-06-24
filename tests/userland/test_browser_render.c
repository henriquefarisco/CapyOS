/*
 * tests/userland/test_browser_render.c — host tests for the CapyOS text-mode
 * render backend for the capy-browser-core display list (Etapa 7 / Slice 7.1).
 *
 * Gated by CAPYOS_HAVE_CAPYBROWSER_CORE (set by the Makefile when the CapyBrowser
 * sibling's display_list.h is present): the display-list ABI (struct capy_dl)
 * lives in the sibling, so when it is absent this run function is a no-op and the
 * suite still links (mirrors test_capybrowse_view). Display lists are plain data,
 * so the tests build them by hand and assert the deterministic text rendering.
 * Under -DUNIT_TEST the freestanding userland <string.h> defers to the host libc.
 */

#include <stdio.h>

#ifdef CAPYOS_HAVE_CAPYBROWSER_CORE

#include <string.h>

#include "browser_render.h"

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

static struct capy_dl g_dl;
static char g_out[8192];

static void dl_reset(void) {
  memset(&g_dl, 0, sizeof(g_dl));
  g_dl.version = CAPY_DL_VERSION;
}

/* Append a string to the display-list arena; record its [off,len). */
static void dl_str(const char *s, size_t *off, size_t *len) {
  size_t n = strlen(s);
  *off = g_dl.string_len;
  memcpy(&g_dl.strings[g_dl.string_len], s, n);
  g_dl.string_len += n;
  *len = n;
}

static void dl_text(long x, long y, const char *s) {
  struct capy_dl_node *nd = &g_dl.nodes[g_dl.node_count++];
  nd->kind = CAPY_DL_TEXT;
  nd->x = x;
  nd->y = y;
  nd->width = (long)strlen(s);
  nd->height = 1;
  dl_str(s, &nd->text_off, &nd->text_len);
}

static void dl_image(long x, long y, const char *label) {
  struct capy_dl_node *nd = &g_dl.nodes[g_dl.node_count++];
  nd->kind = CAPY_DL_IMAGE;
  nd->x = x;
  nd->y = y;
  nd->width = 1;
  nd->height = 1;
  dl_str(label, &nd->label_off, &nd->label_len);
}

static void dl_link(long x, long y, const char *url) {
  struct capy_dl_node *nd = &g_dl.nodes[g_dl.node_count++];
  nd->kind = CAPY_DL_LINK;
  nd->x = x;
  nd->y = y;
  nd->width = 1;
  nd->height = 1;
  dl_str(url, &nd->url_off, &nd->url_len);
}

#endif /* CAPYOS_HAVE_CAPYBROWSER_CORE */

int test_browser_render_run(void) {
#ifdef CAPYOS_HAVE_CAPYBROWSER_CORE
  printf("[test_browser_render]\n");
  g_runs = 0;
  g_passes = 0;

  /* Laid-out page: two text runs on row 0 placed at columns 0 and 6, a run on
   * row 1, plus a link -> grid text + a numbered Links: section. */
  {
    size_t n;
    struct capyos_browser_render_stats st;
    dl_reset();
    dl_text(0, 0, "Hello");
    dl_text(6, 0, "World");
    dl_text(0, 1, "Second line");
    dl_link(0, 0, "https://a.example/");
    g_dl.content_width = 80;
    g_dl.content_height = 2;
    n = capyos_browser_render_text(&g_dl, g_out, sizeof(g_out), &st);
    CHECK(n > 0u && n < sizeof(g_out), "returns bounded length");
    CHECK(strlen(g_out) == n, "NUL-terminated at returned length");
    CHECK(strstr(g_out, "Hello World") != NULL, "row 0 column-placed");
    CHECK(strstr(g_out, "Second line") != NULL, "row 1 present");
    CHECK(strstr(g_out, "Links:") != NULL, "links header present");
    CHECK(strstr(g_out, "[1] https://a.example/") != NULL, "link resolved");
    CHECK(st.text_nodes == 3u, "counts text nodes");
    CHECK(st.link_nodes == 1u, "counts link nodes");
  }

  /* Determinism: identical input -> identical output. */
  {
    char a[2048];
    char b[2048];
    size_t na, nb;
    dl_reset();
    dl_text(2, 0, "abc");
    dl_text(0, 2, "xyz");
    g_dl.content_width = 40;
    g_dl.content_height = 3;
    na = capyos_browser_render_text(&g_dl, a, sizeof(a), NULL);
    nb = capyos_browser_render_text(&g_dl, b, sizeof(b), NULL);
    CHECK(na == nb && memcmp(a, b, na) == 0, "deterministic output");
    CHECK(a[2] == 'a' && a[0] == ' ' && a[1] == ' ', "column padding before run");
  }

  /* Image placeholder shows its alt label. */
  {
    dl_reset();
    dl_image(0, 0, "logo");
    g_dl.content_width = 40;
    g_dl.content_height = 1;
    capyos_browser_render_text(&g_dl, g_out, sizeof(g_out), NULL);
    CHECK(strstr(g_out, "[img:logo]") != NULL, "image placeholder with label");
  }

  /* Untrusted content: control bytes (e.g. ESC) are sanitized to '?', so a
   * page cannot inject terminal escape sequences. UTF-8 (>=0x20) passes. */
  {
    struct capyos_browser_render_stats st;
    dl_reset();
    dl_text(0, 0, "a\x1b[2Jb");
    g_dl.content_width = 40;
    g_dl.content_height = 1;
    capyos_browser_render_text(&g_dl, g_out, sizeof(g_out), &st);
    CHECK(strstr(g_out, "\x1b") == NULL, "ESC byte not emitted");
    CHECK(strstr(g_out, "a?[2Jb") != NULL, "control byte sanitized to '?'");
  }

  /* Fail-closed: NULL display list, NULL out, version mismatch, and a payload
   * range outside the arena must never crash and must report truncation. */
  {
    struct capyos_browser_render_stats st;
    size_t n;
    n = capyos_browser_render_text(NULL, g_out, sizeof(g_out), &st);
    CHECK(n == 0u && st.truncated && g_out[0] == '\0', "NULL dl fail-closed");

    dl_reset();
    dl_text(0, 0, "x");
    n = capyos_browser_render_text(&g_dl, NULL, 0u, &st);
    CHECK(n == 0u && st.truncated, "NULL out fail-closed");

    dl_reset();
    g_dl.version = CAPY_DL_VERSION + 99;
    n = capyos_browser_render_text(&g_dl, g_out, sizeof(g_out), &st);
    CHECK(n == 0u && st.truncated, "version mismatch fail-closed");

    dl_reset();
    g_dl.nodes[0].kind = CAPY_DL_TEXT;
    g_dl.nodes[0].x = 0;
    g_dl.nodes[0].y = 0;
    g_dl.nodes[0].text_off = g_dl.string_len + 1000u; /* outside arena */
    g_dl.nodes[0].text_len = 5u;
    g_dl.node_count = 1u;
    g_dl.content_width = 40;
    g_dl.content_height = 1;
    n = capyos_browser_render_text(&g_dl, g_out, sizeof(g_out), &st);
    CHECK(g_out[strlen(g_out) > 0 ? strlen(g_out) - 1u : 0u] != '\xff',
          "out-of-arena payload does not crash");
  }

  /* Clipping: a run starting past MAX_COLS is dropped and flagged. */
  {
    struct capyos_browser_render_stats st;
    dl_reset();
    dl_text((long)CAPYOS_BROWSER_RENDER_MAX_COLS + 10, 0, "offscreen");
    g_dl.content_width = (long)CAPYOS_BROWSER_RENDER_MAX_COLS + 50;
    g_dl.content_height = 1;
    capyos_browser_render_text(&g_dl, g_out, sizeof(g_out), &st);
    CHECK(st.clipped, "off-grid run flagged clipped");
    CHECK(strstr(g_out, "offscreen") == NULL, "off-grid run not emitted");
  }

  printf("[test_browser_render] %d/%d passed\n", g_passes, g_runs);
  return g_runs - g_passes;
#else
  return 0;
#endif
}
