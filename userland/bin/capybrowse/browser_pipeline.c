/*
 * userland/bin/capybrowse/browser_pipeline.c — CapyOS-side driver for the
 * capy-browser-core static-render pipeline (Etapa 7 / Slice 7.3). See
 * browser_pipeline.h for the contract. This is intentionally a thin adapter
 * over CapyBrowser's production `capy_page_render` facade, so host tests,
 * CapyOS and other integrations execute the same stage ordering and policy.
 */
#include "browser_pipeline.h"

#include "page_render.h"

/* The owned production page arena is large, so it belongs in .bss. */
static struct capy_page g_page;

static void stats_reset(struct capyos_browser_pipeline_stats *s) {
  if (!s) return;
  s->stage_failed = 0;
  s->dom_truncated = 0;
  s->css_truncated = 0;
  s->layout_truncated = 0;
  s->dl_truncated = 0;
  s->dom_nodes = 0u;
  s->layout_boxes = 0u;
  s->dl_nodes = 0u;
}

const struct capy_dl *capyos_browser_build_display_list(
    const char *html, size_t html_len, const char *css, size_t css_len,
    const char *base_url, long viewport_width,
    struct capyos_browser_pipeline_stats *stats) {
  int rc;
  long vw = (viewport_width > 0) ? viewport_width : 80;

  stats_reset(stats);

  if (html == NULL) {
    if (stats) stats->stage_failed = 1;
    return NULL;
  }
  rc = capy_page_render(html, html_len, css, css_len, base_url, vw, &g_page);
  if (rc != CAPY_PAGE_OK) {
    if (stats) stats->stage_failed = (int)g_page.completed_stage + 1;
    return NULL;
  }

  if (stats) {
    stats->dom_truncated = g_page.dom.truncated;
    stats->css_truncated = g_page.stylesheet.truncated;
    stats->layout_truncated = g_page.layout.truncated;
    stats->dl_truncated = g_page.display_list.truncated;
    stats->dom_nodes = g_page.dom.node_count;
    stats->layout_boxes = g_page.layout.box_count;
    stats->dl_nodes = g_page.display_list.node_count;
  }
  return &g_page.display_list;
}
