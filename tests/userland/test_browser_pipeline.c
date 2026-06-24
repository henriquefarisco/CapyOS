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
#ifdef CAPYOS_HAVE_CAPYCODECS_IMAGE
#include "browser_image.h" /* Slice 7.4: image decode adapter (CapyCodecs) */
#endif

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

#ifdef CAPYOS_HAVE_CAPYCODECS_IMAGE
/* 1x1 24-bit BMP -> ARGB 0xFF112233 (no inflater needed). 58 bytes:
 * 14-byte file header + 40-byte info header + a 4-byte (padded) pixel row. */
static const uint8_t test_bmp_1x1[] = {
    /* -- BITMAPFILEHEADER (14) -- */
    0x42u, 0x4Du,               /* "BM" */
    0x3Au, 0x00u, 0x00u, 0x00u, /* file size = 58 */
    0x00u, 0x00u, 0x00u, 0x00u, /* reserved */
    0x36u, 0x00u, 0x00u, 0x00u, /* pixel data offset = 54 */
    /* -- BITMAPINFOHEADER (40) -- */
    0x28u, 0x00u, 0x00u, 0x00u, /* header size = 40 */
    0x01u, 0x00u, 0x00u, 0x00u, /* width = 1 */
    0x01u, 0x00u, 0x00u, 0x00u, /* height = 1 */
    0x01u, 0x00u,               /* planes = 1 */
    0x18u, 0x00u,               /* bpp = 24 */
    0x00u, 0x00u, 0x00u, 0x00u, /* compression = BI_RGB */
    0x04u, 0x00u, 0x00u, 0x00u, /* image size = 4 */
    0x00u, 0x00u, 0x00u, 0x00u, /* x pixels/meter */
    0x00u, 0x00u, 0x00u, 0x00u, /* y pixels/meter */
    0x00u, 0x00u, 0x00u, 0x00u, /* colors used */
    0x00u, 0x00u, 0x00u, 0x00u, /* important colors */
    /* -- pixel row (B,G,R,pad) -> ARGB 0xFF112233 -- */
    0x33u, 0x22u, 0x11u, 0x00u};

/* Real 2x2 RGB PNG (real zlib IDAT -> exercises the tinf inflater). Pixels:
 * (0,0)=red (1,0)=green / (0,1)=blue (1,1)=white. */
static const uint8_t test_png_2x2_rgb[] = {
    0x89u, 0x50u, 0x4eu, 0x47u, 0x0du, 0x0au, 0x1au, 0x0au, 0x00u, 0x00u, 0x00u,
    0x0du, 0x49u, 0x48u, 0x44u, 0x52u, 0x00u, 0x00u, 0x00u, 0x02u, 0x00u, 0x00u,
    0x00u, 0x02u, 0x08u, 0x02u, 0x00u, 0x00u, 0x00u, 0xfdu, 0xd4u, 0x9au, 0x73u,
    0x00u, 0x00u, 0x00u, 0x12u, 0x49u, 0x44u, 0x41u, 0x54u, 0x78u, 0xdau, 0x63u,
    0xf8u, 0xcfu, 0xc0u, 0xc0u, 0x00u, 0xc2u, 0x0cu, 0xffu, 0x81u, 0x00u, 0x00u,
    0x1fu, 0xeeu, 0x05u, 0xfbu, 0xf1u, 0xabu, 0xbau, 0x77u, 0x00u, 0x00u, 0x00u,
    0x00u, 0x49u, 0x45u, 0x4eu, 0x44u, 0xaeu, 0x42u, 0x60u, 0x82u};

/* Image resolver for the rasterize-with-image test: decode the embedded PNG
 * (ignoring src) and hand back its ARGB32 pixels. */
static int test_resolve_image(void *ctx, const char *src, size_t src_len,
                              const uint32_t **px, uint32_t *w, uint32_t *h) {
  struct capyos_image img;
  (void)ctx;
  (void)src;
  (void)src_len;
  if (capyos_image_decode(test_png_2x2_rgb, sizeof(test_png_2x2_rgb), &img) != 0)
    return 0;
  *px = img.pixels;
  *w = img.width;
  *h = img.height;
  return 1;
}
#endif /* CAPYOS_HAVE_CAPYCODECS_IMAGE */

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
      o.resolve_image = 0;
      o.image_ctx = 0;
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

#ifdef CAPYOS_HAVE_CAPYCODECS_IMAGE
  /* 5. Image decode adapter (CapyCodecs): BMP (no inflater) + PNG (via tinf). */
  {
    struct capyos_image img;
    CHECK(capyos_image_decode(test_bmp_1x1, sizeof(test_bmp_1x1), &img) == 0 &&
              img.width == 1u && img.height == 1u && img.pixels != 0 &&
              img.pixels[0] == 0xFF112233u,
          "BMP 1x1 decodes to ARGB 0xFF112233");
    CHECK(capyos_image_decode(test_png_2x2_rgb, sizeof(test_png_2x2_rgb),
                              &img) == 0 &&
              img.width == 2u && img.height == 2u && img.pixels != 0,
          "PNG 2x2 decodes via the tinf inflater");
    if (img.pixels != 0 && img.width == 2u && img.height == 2u) {
      CHECK(img.pixels[0] == 0xFFFF0000u && img.pixels[1] == 0xFF00FF00u &&
                img.pixels[2] == 0xFF0000FFu && img.pixels[3] == 0xFFFFFFFFu,
            "PNG pixels decode to red/green/blue/white");
    }
    CHECK(capyos_image_decode((const uint8_t *)"not an image", 12u, &img) == -1,
          "unrecognized bytes -> decode fail-closed");
  }

  /* 6. Rasterize a page with <img>: the resolver decodes -> the image is drawn
   *    (images_decoded), not a placeholder. */
  {
    struct capyos_browser_pixel_opts o;
    struct capyos_browser_pixel_stats ps;
    const char *html = "<html><body><img src=\"logo.png\"></body></html>";
    dl = capyos_browser_build_display_list(html, strlen(html), "", 0u,
                                           "https://capy.example/", 40, &st);
    CHECK(dl != NULL && count_kind(dl, CAPY_DL_IMAGE) > 0u,
          "pipeline emits an IMAGE node for <img>");
    if (dl != NULL) {
      o.cell_w = 8u;
      o.cell_h = 16u;
      o.bg = 0xFFFFFFFFu;
      o.fg = 0xFF111111u;
      o.link = 0xFF1A4FD0u;
      o.resolve_image = test_resolve_image;
      o.image_ctx = 0;
      CHECK(capyos_browser_render_pixels(dl, g_px, 200u, 120u, &o, &ps) == 0 &&
                ps.images_decoded >= 1u,
            "IMAGE node drawn from a decoded image (not placeholder)");
    }
  }
#endif /* CAPYOS_HAVE_CAPYCODECS_IMAGE */

  printf("  %d/%d checks passed\n", g_passes, g_runs);
  return g_passes == g_runs ? 0 : 1;
}
