/*
 * userland/bin/capybrowse/browser_render.h — CapyOS-side render backend for the
 * decoupled capy-browser-core display list (Etapa 7 / Slice 7.1).
 *
 * The CapyBrowser core (M1..M3) turns HTML+CSS into a pure, versioned display
 * list (`struct capy_dl`, src/displaylist/display_list.h): an ordered, flat list
 * of TEXT / RECT / IMAGE / LINK nodes with geometry in LAYOUT CELLS (top-left
 * origin, 1 column x 1 row per cell) and string payloads in an arena. Per the
 * integration contract (browser-core-integration-contract.md) and Etapa 7
 * acceptance criterion 6, the parser/layout/display-list stays in CapyBrowser
 * and the *render backend* lives in CapyOS, consuming the display list without
 * the core ever depending on the compositor.
 *
 * This is the FIRST such backend: a text-mode rasterizer. Because the display
 * list geometry is already in cells, cell (x, y) maps directly to column x /
 * row y of a character grid -- a faithful, deterministic text rendering of the
 * laid-out page (and the graphical/pixel backend in a later slice walks the
 * same display list into a surface). It is pure, allocation-free, deterministic
 * and fail-closed, so it is host-testable (make test) and links into the ring-3
 * capybrowse app unchanged.
 */
#ifndef CAPYOS_BROWSER_RENDER_H
#define CAPYOS_BROWSER_RENDER_H

#include <stddef.h>

#include "display_list.h" /* capy-browser-core: struct capy_dl (Fase M3b) */

/* Output bounds. The page's content extent is untrusted (a remote document can
 * declare a huge laid-out height), so the text backend clips to a bounded grid
 * and reports truncation rather than emitting unbounded output. */
#define CAPYOS_BROWSER_RENDER_MAX_COLS 200u
#define CAPYOS_BROWSER_RENDER_MAX_ROWS 2000u
#define CAPYOS_BROWSER_RENDER_MAX_LINKS 256u

struct capyos_browser_render_stats {
  size_t rows_emitted;  /* grid rows written to the output */
  size_t text_nodes;    /* TEXT nodes placed */
  size_t link_nodes;    /* LINK nodes referenced */
  size_t image_nodes;   /* IMAGE placeholders placed */
  size_t rect_nodes;    /* RECT (background) nodes seen (skipped in text mode) */
  int clipped;          /* a node fell partly/fully outside the bounded grid */
  int truncated;        /* output buffer or row/col/link budget exhausted */
};

/*
 * Render a capy-browser-core display list to a deterministic text view.
 *
 * TEXT runs are placed at their cell (x, y); overlapping runs are placed in
 * display-list order (later wins on a conflict, matching paint order). IMAGE
 * nodes render their alt label as "[img:<label>]" (or "[img]" when unlabeled).
 * LINK nodes are collected and appended as a numbered reference list after the
 * grid (like the html-to-text view), with a "[n]" marker dropped at the link's
 * top-left cell. RECT (background) nodes are counted but not drawn in text mode.
 *
 * `out` is always NUL-terminated when out_cap >= 1. Returns the number of bytes
 * written (excluding the terminating NUL). Pure and fail-closed: a NULL/invalid
 * argument or a version mismatch writes an empty string, sets stats->truncated,
 * and returns 0. `stats` may be NULL.
 */
size_t capyos_browser_render_text(const struct capy_dl *dl, char *out,
                                  size_t out_cap,
                                  struct capyos_browser_render_stats *stats);

#endif /* CAPYOS_BROWSER_RENDER_H */
