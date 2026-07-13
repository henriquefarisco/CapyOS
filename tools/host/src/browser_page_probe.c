/* Host diagnostic for real-world HTML. Reads one document from stdin and
 * drives the same bounded static pipeline and pixel renderer as capygfx. */
#include "browser_pipeline.h"
#include "browser_render_pixel.h"

#include <stdint.h>
#include <stdio.h>

#define PROBE_HTML_MAX (512u * 1024u)
#define PROBE_W 800u
#define PROBE_H 600u

static char g_html[PROBE_HTML_MAX];
static uint32_t g_pixels[PROBE_W * PROBE_H];

int main(int argc, char **argv) {
  struct capyos_browser_pipeline_stats pipeline;
  struct capyos_browser_pixel_stats pixels;
  struct capyos_browser_pixel_opts opts;
  const struct capy_dl *display_list;
  size_t len = fread(g_html, 1u, sizeof(g_html), stdin);
  const char *base = argc > 1 ? argv[1] : "https://example.invalid/";
  if (ferror(stdin) || len == 0u) return 2;
  display_list = capyos_browser_build_display_list(
      g_html, len, "", 0u, base, (long)(PROBE_W / 8u), &pipeline);
  if (!display_list) {
    printf("pipeline-failed stage=%d bytes=%zu\n", pipeline.stage_failed, len);
    return 3;
  }
  opts.cell_w = 8u;
  opts.cell_h = 16u;
  opts.bg = 0xffffffffu;
  opts.fg = 0xff111111u;
  opts.link = 0xff1a4fd0u;
  opts.resolve_image = 0;
  opts.image_ctx = 0;
  if (capyos_browser_render_pixels(display_list, g_pixels, PROBE_W, PROBE_H,
                                   &opts, &pixels) != 0)
    return 4;
  printf("ok bytes=%zu dom=%zu boxes=%zu nodes=%zu trunc=%d/%d/%d/%d "
         "drawn=%zu\n",
         len, pipeline.dom_nodes, pipeline.layout_boxes, pipeline.dl_nodes,
         pipeline.dom_truncated, pipeline.css_truncated,
         pipeline.layout_truncated, pipeline.dl_truncated,
         pixels.glyphs_drawn);
  return 0;
}
