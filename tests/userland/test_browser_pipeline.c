/*
 * tests/userland/test_browser_pipeline.c — host test for the CapyOS-side driver
 * of the capy-browser-core static-render pipeline (Etapa 7 / Slice 7.3).
 *
 * Built into a FOCUSED standalone binary (`make test-browser-pipeline`), NOT the
 * aggregate unit_tests: the pipeline links the sibling's url_parse.c, whose
 * `capy_url_parse` would collide with capylibc-net's same-named symbol in the
 * aggregate. The focused binary links the browser core WITHOUT capylibc-net, so
 * there is no collision (mirrors the isolation the text core uses via a rename).
 *
 * It drives a real embedded HTML(+CSS) page through all five stages
 * (capyos_browser_build_display_list) and asserts the resulting display list,
 * then rasterizes it (capyos_browser_render_pixels) to prove HTML -> pixels
 * end-to-end on the host.
 */
#include "browser_pipeline.h"
#include "browser_render_pixel.h"

#include <stdint.h>
#include <stdio.h>
#include <string.h>

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

/* Robust substring search over the display-list string arena: avoids coupling
 * the assertions to exactly how the core splits text runs / whitespace. */
static int arena_has(const struct capy_dl *dl, const char *needle) {
  size_t nl = strlen(needle);
  size_t i;
  if (nl == 0u || dl->string_len < nl) return 0;
  for (i = 0u; i + nl <= dl->string_len; ++i) {
    if (memcmp(dl->strings + i, needle, nl) == 0) return 1;
  }
  return 0;
}

static size_t count_kind(const struct capy_dl *dl, enum capy_dl_node_kind k) {
  size_t n, c = 0u;
  for (n = 0u; n < dl->node_count; ++n)
    if (dl->nodes[n].kind == k) c++;
  return c;
}

static uint32_t g_px[200u * 120u];

int run_browser_pipeline_tests(void) {
  struct capyos_browser_pipeline_stats st;
  const struct capy_dl *dl;
  const char *html =
      "<html><body><h1>CapyOS</h1>"
      "<p>Ola <a href=\"sobre.html\">sobre</a> mundo</p></body></html>";

  printf("[test_browser_pipeline]\n");
  g_runs = 0;
  g_passes = 0;

  /* 1. Full pipeline: HTML(+empty CSS) -> display list. */
  dl = capyos_browser_build_display_list(html, strlen(html), "", 0u,
                                         "https://capy.example/", 40, &st);
  CHECK(dl != NULL, "pipeline returns a display list");
  CHECK(st.stage_failed == 0, "no stage failed");
  if (dl != NULL) {
    CHECK(dl->version == CAPY_DL_VERSION, "display list version matches");
    CHECK(dl->node_count > 0u, "display list has nodes");
    CHECK(count_kind(dl, CAPY_DL_TEXT) > 0u, "has TEXT nodes");
    CHECK(count_kind(dl, CAPY_DL_LINK) > 0u, "has a LINK node");
    CHECK(arena_has(dl, "CapyOS"), "title text extracted into the arena");
    CHECK(arena_has(dl, "https://capy.example/sobre.html"),
          "relative href resolved to an absolute URL against base");

    /* 2. Rasterize the real page to pixels (HTML -> pixels end-to-end). */
    {
      struct capyos_browser_pixel_opts o;
      struct capyos_browser_pixel_stats ps;
      int rc;
      o.cell_w = 8u;
      o.cell_h = 16u;
      o.bg = 0xFFFFFFFFu;
      o.fg = 0xFF111111u;
      o.link = 0xFF1A4FD0u;
      rc = capyos_browser_render_pixels(dl, g_px, 200u, 120u, &o, &ps);
      CHECK(rc == 0, "rasterize the real display list");
      CHECK(ps.glyphs_drawn > 0u, "glyphs drawn from the real page");
      CHECK(ps.text_nodes > 0u, "rasterizer saw the page's TEXT nodes");
    }
  }

  /* 3. Fail-closed: NULL html. */
  dl = capyos_browser_build_display_list(NULL, 0u, "", 0u, NULL, 40, &st);
  CHECK(dl == NULL && st.stage_failed == 1, "NULL html -> NULL + stage 1");

  /* 4. CSS is honored without crashing (background color on body). */
  dl = capyos_browser_build_display_list(
      "<html><body><p>hi</p></body></html>",
      strlen("<html><body><p>hi</p></body></html>"),
      "p { color: #ff0000; }", strlen("p { color: #ff0000; }"), NULL, 40, &st);
  CHECK(dl != NULL && st.stage_failed == 0, "pipeline with author CSS ok");

  printf("  %d/%d checks passed\n", g_passes, g_runs);
  return g_passes == g_runs ? 0 : 1;
}
