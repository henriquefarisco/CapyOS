/*
 * userland/bin/capybrowse/browser_pipeline.c — CapyOS-side driver for the
 * capy-browser-core static-render pipeline (Etapa 7 / Slice 7.3). See
 * browser_pipeline.h for the contract. Sequences the five pure core stages;
 * single-shot (static .bss arenas), deterministic, fail-closed. Freestanding
 * (no libc beyond the core's own <string.h>): host-testable and ring-3 linkable.
 */
#include "browser_pipeline.h"

#include "cascade.h"
#include "css_parse.h"
#include "dom.h"
#include "layout.h"

/* Intermediate arenas + output: ~1 MiB total, kept off the caller stack. */
static struct capy_dom_doc g_dom;
static struct capy_css_stylesheet g_sheet;
static struct capy_css_cascade g_casc;
static struct capy_layout_tree g_layout;
static struct capy_dl g_dl;

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
  long vw = (viewport_width > 0) ? viewport_width : 80;

  stats_reset(stats);

  if (html == NULL) {
    if (stats) stats->stage_failed = 1;
    return NULL;
  }
  if (css == NULL) {
    css = "";
    css_len = 0u;
  }

  /* 1. HTML -> DOM. */
  if (capy_html_parse(html, html_len, &g_dom) != CAPY_DOM_OK) {
    if (stats) stats->stage_failed = 1;
    return NULL;
  }
  /* 2. CSS -> stylesheet (an empty sheet is valid -> UA defaults only). */
  if (capy_css_parse(css, css_len, &g_sheet) != CAPY_CSS_OK) {
    if (stats) stats->stage_failed = 2;
    return NULL;
  }
  /* 3. cascade -> computed styles. */
  if (capy_css_cascade(&g_dom, &g_sheet, &g_casc) != CAPY_CSS_CASCADE_OK) {
    if (stats) stats->stage_failed = 3;
    return NULL;
  }
  /* 4. layout -> box tree. */
  if (capy_layout(&g_dom, &g_sheet, &g_casc, vw, &g_layout) != CAPY_LAYOUT_OK) {
    if (stats) stats->stage_failed = 4;
    return NULL;
  }
  /* 5. display list (resolves link hrefs against base_url). */
  if (capy_displaylist(&g_dom, &g_sheet, &g_casc, &g_layout, base_url, &g_dl) !=
      CAPY_DL_OK) {
    if (stats) stats->stage_failed = 5;
    return NULL;
  }

  if (stats) {
    stats->dom_truncated = g_dom.truncated;
    stats->css_truncated = g_sheet.truncated;
    stats->layout_truncated = g_layout.truncated;
    stats->dl_truncated = g_dl.truncated;
    stats->dom_nodes = g_dom.node_count;
    stats->layout_boxes = g_layout.box_count;
    stats->dl_nodes = g_dl.node_count;
  }
  return &g_dl;
}
