/*
 * userland/bin/capybrowse/browser_render.c — CapyOS text-mode render backend
 * for the capy-browser-core display list (Etapa 7 / Slice 7.1). See
 * browser_render.h for the contract. Pure, allocation-free, deterministic,
 * fail-closed, freestanding (no libc): host-testable and ring-3 linkable.
 */
#include "browser_render.h"

/* Append a NUL-terminated string to out[0..cap), advancing *pos. Bounded:
 * stops at cap-1 and sets *full when it could not write everything. */
static void br_puts(char *out, size_t cap, size_t *pos, const char *s,
                    int *full) {
  size_t i = 0u;
  while (s[i] != '\0') {
    if (*pos + 1u >= cap) {
      *full = 1;
      return;
    }
    out[*pos] = s[i];
    (*pos)++;
    i++;
  }
}

/* Append one byte. */
static void br_putc(char *out, size_t cap, size_t *pos, char c, int *full) {
  if (*pos + 1u >= cap) {
    *full = 1;
    return;
  }
  out[*pos] = c;
  (*pos)++;
}

/* Append a bounded, control-sanitized slice of the display-list string arena.
 * Untrusted remote content: bytes < 0x20 or 0x7F (DEL) -> '?' so a page cannot
 * inject terminal escape sequences; bytes >= 0x20 pass through (UTF-8 intact).
 * advance returns the number of columns consumed (one per emitted byte). */
static size_t br_put_payload(char *out, size_t cap, size_t *pos,
                             const struct capy_dl *dl, size_t off, size_t len,
                             size_t max_cols, int *full) {
  size_t i;
  size_t cols = 0u;
  if (off > dl->string_len || len > dl->string_len - off) {
    return 0u; /* fail-closed: payload range outside the arena */
  }
  for (i = 0u; i < len && cols < max_cols; ++i) {
    unsigned char c = (unsigned char)dl->strings[off + i];
    char safe = (c < 0x20u || c == 0x7Fu) ? '?' : (char)c;
    br_putc(out, cap, pos, safe, full);
    if (*full) {
      break;
    }
    cols++;
  }
  return cols;
}

/* Decimal append of a small unsigned value (link numbering). */
static void br_put_uint(char *out, size_t cap, size_t *pos, size_t v,
                        int *full) {
  char tmp[24];
  size_t n = 0u;
  if (v == 0u) {
    br_putc(out, cap, pos, '0', full);
    return;
  }
  while (v > 0u && n < sizeof(tmp)) {
    tmp[n++] = (char)('0' + (int)(v % 10u));
    v /= 10u;
  }
  while (n > 0u) {
    br_putc(out, cap, pos, tmp[--n], full);
    if (*full) {
      return;
    }
  }
}

size_t capyos_browser_render_text(const struct capy_dl *dl, char *out,
                                  size_t out_cap,
                                  struct capyos_browser_render_stats *stats) {
  struct capyos_browser_render_stats sink;
  size_t pos = 0u;
  size_t cols, rows, max_y, y, n, link_no;
  int full = 0;

  if (stats == NULL) {
    stats = &sink;
  }
  stats->rows_emitted = 0u;
  stats->text_nodes = 0u;
  stats->link_nodes = 0u;
  stats->image_nodes = 0u;
  stats->rect_nodes = 0u;
  stats->clipped = 0;
  stats->truncated = 0;

  if (out != NULL && out_cap >= 1u) {
    out[0] = '\0';
  }
  if (dl == NULL || out == NULL || out_cap < 2u ||
      dl->version != CAPY_DL_VERSION) {
    stats->truncated = 1;
    return 0u;
  }

  /* Bounded grid geometry. content_width/height are untrusted extents. */
  cols = (dl->content_width > 0) ? (size_t)dl->content_width : 0u;
  if (cols > CAPYOS_BROWSER_RENDER_MAX_COLS) {
    cols = CAPYOS_BROWSER_RENDER_MAX_COLS;
    stats->clipped = 1;
  }
  rows = (dl->content_height > 0) ? (size_t)dl->content_height : 0u;
  if (rows > CAPYOS_BROWSER_RENDER_MAX_ROWS) {
    rows = CAPYOS_BROWSER_RENDER_MAX_ROWS;
    stats->clipped = 1;
  }

  /* Count node kinds once, and find the last row that actually carries a
   * drawable (TEXT/IMAGE) node so we do not emit a tail of blank rows. */
  max_y = 0u;
  for (n = 0u; n < dl->node_count; ++n) {
    const struct capy_dl_node *nd = &dl->nodes[n];
    switch (nd->kind) {
    case CAPY_DL_TEXT:
      stats->text_nodes++;
      break;
    case CAPY_DL_IMAGE:
      stats->image_nodes++;
      break;
    case CAPY_DL_LINK:
      stats->link_nodes++;
      break;
    case CAPY_DL_RECT:
    default:
      stats->rect_nodes++;
      break;
    }
    if ((nd->kind == CAPY_DL_TEXT || nd->kind == CAPY_DL_IMAGE) && nd->y >= 0) {
      size_t ny = (size_t)nd->y;
      if (ny < rows && ny > max_y) {
        max_y = ny;
      }
    }
  }

  /* Grid: for each row, emit the drawable runs left-to-right (display-list
   * order is paint order). Pad with spaces up to each run's start column. */
  for (y = 0u; y <= max_y && !full; ++y) {
    size_t cur_col = 0u;
    int row_has = 0;
    for (n = 0u; n < dl->node_count && !full; ++n) {
      const struct capy_dl_node *nd = &dl->nodes[n];
      size_t start, used;
      if (nd->kind != CAPY_DL_TEXT && nd->kind != CAPY_DL_IMAGE) {
        continue;
      }
      if (nd->y < 0 || (size_t)nd->y != y) {
        continue;
      }
      start = (nd->x > 0) ? (size_t)nd->x : 0u;
      if (start >= cols) {
        stats->clipped = 1;
        continue; /* starts past the right edge */
      }
      while (cur_col < start && !full) {
        br_putc(out, out_cap, &pos, ' ', &full);
        cur_col++;
      }
      if (nd->kind == CAPY_DL_TEXT) {
        used = br_put_payload(out, out_cap, &pos, dl, nd->text_off,
                              nd->text_len, cols - cur_col, &full);
        cur_col += used;
        if (used > 0u) {
          row_has = 1;
        }
      } else { /* CAPY_DL_IMAGE -> "[img:<label>]" placeholder */
        br_puts(out, out_cap, &pos, "[img", &full);
        cur_col += 4u;
        if (nd->label_len > 0u && cur_col < cols) {
          br_putc(out, out_cap, &pos, ':', &full);
          cur_col++;
          cur_col += br_put_payload(out, out_cap, &pos, dl, nd->label_off,
                                    nd->label_len, cols - cur_col, &full);
        }
        br_putc(out, out_cap, &pos, ']', &full);
        cur_col++;
        row_has = 1;
      }
    }
    br_putc(out, out_cap, &pos, '\n', &full);
    if (row_has) {
      stats->rows_emitted++;
    }
  }

  /* Numbered link references (resolved absolute URLs), like the text view. */
  link_no = 0u;
  for (n = 0u; n < dl->node_count && !full; ++n) {
    const struct capy_dl_node *nd = &dl->nodes[n];
    if (nd->kind != CAPY_DL_LINK || nd->url_len == 0u) {
      continue;
    }
    if (link_no >= CAPYOS_BROWSER_RENDER_MAX_LINKS) {
      stats->truncated = 1;
      break;
    }
    if (link_no == 0u) {
      br_puts(out, out_cap, &pos, "\nLinks:\n", &full);
    }
    link_no++;
    br_putc(out, out_cap, &pos, '[', &full);
    br_put_uint(out, out_cap, &pos, link_no, &full);
    br_puts(out, out_cap, &pos, "] ", &full);
    br_put_payload(out, out_cap, &pos, dl, nd->url_off, nd->url_len,
                   CAPYOS_BROWSER_RENDER_MAX_COLS, &full);
    br_putc(out, out_cap, &pos, '\n', &full);
  }

  if (full || dl->truncated) {
    stats->truncated = 1;
  }
  if (pos < out_cap) {
    out[pos] = '\0';
  } else {
    out[out_cap - 1u] = '\0';
  }
  return pos;
}
