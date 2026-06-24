/*
 * userland/bin/capybrowse/browser_pipeline.h — CapyOS-side driver for the
 * decoupled capy-browser-core static-render pipeline (Etapa 7 / Slice 7.3).
 *
 * Slice 7.1 gave a text render backend and 7.2 a pixel rasterizer + the ring-3
 * graphical surface ABI, both consuming a `struct capy_dl` display list. This
 * adapter produces that display list from raw HTML(+CSS): it orchestrates the
 * five pure, allocation-free stages the CapyBrowser core (v0.6.5) exposes —
 *
 *   1. capy_html_parse()  : HTML bytes      -> DOM            (Fase M1)
 *   2. capy_css_parse()   : CSS bytes       -> stylesheet     (Fase M2.1)
 *   3. capy_css_cascade() : DOM + sheet     -> computed styles(Fase M2.2)
 *   4. capy_layout()      : + viewport      -> box tree       (Fase M3a)
 *   5. capy_displaylist() : + base URL      -> display list   (Fase M3b)
 *
 * Per the browser-core integration contract and Etapa 7 criterion 5/6, the
 * parser/layout/display-list stay in CapyBrowser; CapyOS only drives them and
 * owns the render backend. There is no convenience facade in the core, so this
 * adapter is the single place CapyOS sequences the stages.
 *
 * The intermediate arenas (DOM/stylesheet/cascade/layout) and the output
 * display list are large (~1 MiB total), so they live in this TU's .bss (zeroed
 * by the ELF loader / host BSS) rather than on a caller stack. The adapter is
 * therefore SINGLE-SHOT / NOT reentrant: each call overwrites the previous
 * render. Pure (no clock/RNG/IO), deterministic and fail-closed: any stage
 * error returns NULL.
 */
#ifndef CAPYOS_BROWSER_PIPELINE_H
#define CAPYOS_BROWSER_PIPELINE_H

#include <stddef.h>

#include "display_list.h" /* capy-browser-core: struct capy_dl (Fase M3b) */

struct capyos_browser_pipeline_stats {
  int stage_failed;   /* 0 ok; else 1=html 2=css 3=cascade 4=layout 5=displaylist */
  int dom_truncated;  /* a stage's arena/budget was exhausted (output partial) */
  int css_truncated;
  int layout_truncated;
  int dl_truncated;
  size_t dom_nodes;
  size_t layout_boxes;
  size_t dl_nodes;
};

/*
 * Build a display list from HTML (+ optional CSS) by driving the five core
 * stages. `css` may be NULL/empty (no author stylesheet). `base_url` (may be
 * NULL) resolves link hrefs to absolute URLs in LINK nodes. `viewport_width` is
 * the content width in layout cells (clamped to >= 1; <= 0 -> a default).
 *
 * Returns a pointer to the freshly built display list (owned by this TU's
 * static storage; valid until the next call) on success, or NULL fail-closed on
 * a NULL html or any stage failure. `stats` (may be NULL) records which stage
 * failed and per-stage truncation/size for diagnostics.
 */
const struct capy_dl *capyos_browser_build_display_list(
    const char *html, size_t html_len, const char *css, size_t css_len,
    const char *base_url, long viewport_width,
    struct capyos_browser_pipeline_stats *stats);

#endif /* CAPYOS_BROWSER_PIPELINE_H */
