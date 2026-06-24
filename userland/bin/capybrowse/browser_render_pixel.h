/*
 * userland/bin/capybrowse/browser_render_pixel.h — CapyOS pixel render backend
 * for the decoupled capy-browser-core display list (Etapa 7 / Slice 7.2).
 *
 * The graphical companion to Slice 7.1's text backend (browser_render.h). It
 * walks the SAME pure, versioned display list (struct capy_dl, geometry in
 * layout cells) and rasterizes it into a caller-provided 32-bit ARGB surface,
 * which a ring-3 app then hands to the kernel via SYS_SURFACE_BLIT. Per Etapa 7
 * acceptance criterion 6 and the browser-core integration contract, the
 * parser/layout/display-list stay in CapyBrowser and this render backend lives
 * in CapyOS, consuming the display list without the core ever depending on the
 * compositor.
 *
 * Each layout cell maps to a cell_w x cell_h pixel box. Node kinds render as:
 *   RECT  -> filled rectangle in the node's CSS color (the page background and
 *            block backgrounds);
 *   TEXT  -> glyphs from the shared 8x8 font (font8x8_basic), in the node's
 *            color (default near-black), one glyph per cell;
 *   IMAGE -> a bordered placeholder box with the alt label (no decode here; the
 *            CapyCodecs image adapter lands in a later slice);
 *   LINK  -> an underline across the link bounds in the link color (the anchor
 *            text itself arrives as TEXT nodes).
 *
 * Pure, allocation-free, deterministic and fail-closed: NULL/invalid args or a
 * display-list version mismatch leave the surface untouched and report
 * truncation. Host-testable (make test) and ring-3 linkable unchanged.
 */
#ifndef CAPYOS_BROWSER_RENDER_PIXEL_H
#define CAPYOS_BROWSER_RENDER_PIXEL_H

#include <stddef.h>
#include <stdint.h>

#include "display_list.h" /* capy-browser-core: struct capy_dl (Fase M3b) */

/* Default palette (opaque ARGB 0xAARRGGBB). Used when opts is NULL or a field
 * is 0 (except bg/fg/link which are taken verbatim when opts is provided). */
#define CAPYOS_BROWSER_PX_DEFAULT_CELL_W 8u
#define CAPYOS_BROWSER_PX_DEFAULT_CELL_H 16u
#define CAPYOS_BROWSER_PX_BG 0xFFFFFFFFu   /* page background: white */
#define CAPYOS_BROWSER_PX_FG 0xFF111111u   /* body text: near-black */
#define CAPYOS_BROWSER_PX_LINK 0xFF1A4FD0u /* links: blue */

struct capyos_browser_pixel_opts {
  uint32_t cell_w; /* pixels per cell column (0 -> default) */
  uint32_t cell_h; /* pixels per cell row (0 -> default) */
  uint32_t bg;     /* page background fill */
  uint32_t fg;     /* default text color */
  uint32_t link;   /* link underline color */
};

struct capyos_browser_pixel_stats {
  size_t text_nodes;   /* TEXT nodes placed */
  size_t rect_nodes;   /* RECT nodes filled */
  size_t image_nodes;  /* IMAGE placeholders drawn */
  size_t link_nodes;   /* LINK underlines drawn */
  size_t glyphs_drawn; /* glyph cells actually rasterized */
  int clipped;         /* a node fell partly/fully outside the surface */
  int truncated;       /* NULL/invalid args or version mismatch (fail-closed) */
};

/*
 * Rasterize a capy-browser-core display list into `out` (out_w x out_h ARGB32,
 * row stride == out_w). `opts` may be NULL (defaults used). `stats` may be NULL.
 *
 * Returns 0 on success, -1 fail-closed on a NULL out / zero geometry / NULL or
 * version-mismatched display list (in which case the surface is left untouched
 * and stats->truncated is set). Pixels outside the surface are clipped and set
 * stats->clipped.
 */
int capyos_browser_render_pixels(const struct capy_dl *dl, uint32_t *out,
                                 uint32_t out_w, uint32_t out_h,
                                 const struct capyos_browser_pixel_opts *opts,
                                 struct capyos_browser_pixel_stats *stats);

#endif /* CAPYOS_BROWSER_RENDER_PIXEL_H */
