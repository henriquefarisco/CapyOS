/*
 * userland/bin/capybrowse/browser_render_pixel.c — CapyOS pixel render backend
 * for the capy-browser-core display list (Etapa 7 / Slice 7.2). See
 * browser_render_pixel.h for the contract. Pure, allocation-free, deterministic,
 * fail-closed, freestanding (no libc): host-testable and ring-3 linkable.
 *
 * The 8x8 glyph table is the kernel's single-source console font
 * (src/gui/core/font8x8_data.c, MSB = leftmost column, matching font.c). It is
 * pure const data with no kernel dependency, so the Makefile links the same TU
 * into the ring-3 binary + host test -- no duplicated font, glyphs identical to
 * the framebuffer console.
 */
#include "browser_render_pixel.h"

extern const uint8_t font8x8_basic[128][8];

/* ---- low-level pixel ops (all clip to the surface) ----------------------- */

static void px_set(uint32_t *out, uint32_t ow, uint32_t oh, int64_t x, int64_t y,
                   uint32_t argb) {
  if (x < 0 || y < 0 || (uint64_t)x >= ow || (uint64_t)y >= oh) return;
  out[(size_t)y * ow + (size_t)x] = argb;
}

static void px_fill(uint32_t *out, uint32_t ow, uint32_t oh, int64_t x, int64_t y,
                    int64_t w, int64_t h, uint32_t argb, int *clipped) {
  int64_t x0, y0, x1, y1, xx, yy;
  if (w <= 0 || h <= 0) return;
  if (clipped && (x < 0 || y < 0 || x + w > (int64_t)ow || y + h > (int64_t)oh))
    *clipped = 1;
  x0 = x < 0 ? 0 : x;
  y0 = y < 0 ? 0 : y;
  x1 = x + w;
  y1 = y + h;
  if (x1 > (int64_t)ow) x1 = (int64_t)ow;
  if (y1 > (int64_t)oh) y1 = (int64_t)oh;
  for (yy = y0; yy < y1; ++yy) {
    uint32_t *line = out + (size_t)yy * ow;
    for (xx = x0; xx < x1; ++xx) line[xx] = argb;
  }
}

/* Draw one scaled glyph at pixel origin (px,py) in a cw x ch cell box. Returns
 * 1 if any pixel was set (a visible glyph), 0 for blank (space). */
static int px_glyph(uint32_t *out, uint32_t ow, uint32_t oh, int64_t px,
                    int64_t py, uint32_t cw, uint32_t ch, unsigned char c,
                    uint32_t argb) {
  const uint8_t *g = font8x8_basic[c & 0x7Fu];
  uint32_t gx, gy;
  int any = 0;
  for (gy = 0u; gy < ch; ++gy) {
    uint8_t bits = g[(gy * 8u) / ch]; /* nearest-neighbor row scale */
    for (gx = 0u; gx < cw; ++gx) {
      uint32_t fcol = (gx * 8u) / cw; /* nearest-neighbor col scale */
      if (bits & (uint8_t)(0x80u >> fcol)) {
        px_set(out, ow, oh, px + (int64_t)gx, py + (int64_t)gy, argb);
        any = 1;
      }
    }
  }
  return any;
}

/* Blit a src (sw x sh ARGB32) into the dst box [dx,dy,dw,dh] with nearest-
 * neighbor scaling, clipped to the surface. Used to draw a decoded image at an
 * IMAGE node's box. */
static void px_blit_scaled(uint32_t *out, uint32_t ow, uint32_t oh, int64_t dx,
                           int64_t dy, int64_t dw, int64_t dh,
                           const uint32_t *src, uint32_t sw, uint32_t sh,
                           int *clipped) {
  int64_t x, y;
  if (dw <= 0 || dh <= 0 || sw == 0u || sh == 0u || src == 0) return;
  if (clipped && (dx < 0 || dy < 0 || dx + dw > (int64_t)ow ||
                  dy + dh > (int64_t)oh))
    *clipped = 1;
  for (y = 0; y < dh; ++y) {
    int64_t oy = dy + y;
    uint32_t syc;
    if (oy < 0 || (uint64_t)oy >= oh) continue;
    syc = (uint32_t)((uint64_t)y * sh / (uint64_t)dh);
    if (syc >= sh) syc = sh - 1u;
    for (x = 0; x < dw; ++x) {
      int64_t ox = dx + x;
      uint32_t sxc;
      if (ox < 0 || (uint64_t)ox >= ow) continue;
      sxc = (uint32_t)((uint64_t)x * sw / (uint64_t)dw);
      if (sxc >= sw) sxc = sw - 1u;
      out[(size_t)oy * ow + (size_t)ox] = src[(size_t)syc * sw + sxc];
    }
  }
}

/* ---- CSS color parsing --------------------------------------------------- */

static int hexval(char c) {
  if (c >= '0' && c <= '9') return c - '0';
  if (c >= 'a' && c <= 'f') return c - 'a' + 10;
  if (c >= 'A' && c <= 'F') return c - 'A' + 10;
  return -1;
}

static int name_eq(const char *s, size_t len, const char *name) {
  size_t i = 0u;
  for (i = 0u; i < len; ++i) {
    char a = s[i];
    char b = name[i];
    if (b == '\0') return 0;
    if (a >= 'A' && a <= 'Z') a = (char)(a - 'A' + 'a');
    if (a != b) return 0;
  }
  return name[len] == '\0';
}

struct named_color {
  const char *name;
  uint32_t argb;
};

static const struct named_color g_named[] = {
    {"black", 0xFF000000u},  {"white", 0xFFFFFFFFu},  {"red", 0xFFFF0000u},
    {"green", 0xFF008000u},  {"lime", 0xFF00FF00u},   {"blue", 0xFF0000FFu},
    {"navy", 0xFF000080u},   {"gray", 0xFF808080u},   {"grey", 0xFF808080u},
    {"silver", 0xFFC0C0C0u}, {"yellow", 0xFFFFFF00u}, {"orange", 0xFFFFA500u},
    {"purple", 0xFF800080u}, {"maroon", 0xFF800000u}, {"olive", 0xFF808000u},
    {"teal", 0xFF008080u},   {"aqua", 0xFF00FFFFu},   {"cyan", 0xFF00FFFFu},
    {"fuchsia", 0xFFFF00FFu}, {"magenta", 0xFFFF00FFu},
};

/* Parse a CSS color slice (s[0..len)) into an opaque ARGB. Handles #rgb,
 * #rrggbb, rgb(r,g,b) and a small named-color set. Returns 1 on success, 0 on
 * an unrecognized value (caller keeps its default). */
static int parse_color(const char *s, size_t len, uint32_t *argb) {
  size_t i;
  if (!s || len == 0u) return 0;
  if (s[0] == '#') {
    if (len == 4u) { /* #rgb */
      int r = hexval(s[1]), g = hexval(s[2]), b = hexval(s[3]);
      if (r < 0 || g < 0 || b < 0) return 0;
      *argb = 0xFF000000u | ((uint32_t)(r * 17) << 16) |
              ((uint32_t)(g * 17) << 8) | (uint32_t)(b * 17);
      return 1;
    }
    if (len == 7u) { /* #rrggbb */
      int h0, j;
      uint32_t v = 0u;
      for (j = 1; j < 7; ++j) {
        h0 = hexval(s[j]);
        if (h0 < 0) return 0;
        v = (v << 4) | (uint32_t)h0;
      }
      *argb = 0xFF000000u | v;
      return 1;
    }
    return 0;
  }
  if (len >= 4u && (s[0] == 'r' || s[0] == 'R') && (s[1] == 'g' || s[1] == 'G') &&
      (s[2] == 'b' || s[2] == 'B')) {
    /* rgb(r,g,b) / rgba(...) -- read the first three decimal components. */
    uint32_t comp[3] = {0u, 0u, 0u};
    int ci = 0, have = 0, cur = 0;
    for (i = 3u; i < len && ci < 3; ++i) {
      char c = s[i];
      if (c >= '0' && c <= '9') {
        cur = cur * 10 + (c - '0');
        if (cur > 255) cur = 255;
        have = 1;
      } else if (have) {
        comp[ci++] = (uint32_t)cur;
        cur = 0;
        have = 0;
      }
    }
    if (ci < 3 && have) comp[ci++] = (uint32_t)cur;
    if (ci < 3) return 0;
    *argb = 0xFF000000u | (comp[0] << 16) | (comp[1] << 8) | comp[2];
    return 1;
  }
  for (i = 0u; i < sizeof(g_named) / sizeof(g_named[0]); ++i) {
    if (name_eq(s, len, g_named[i].name)) {
      *argb = g_named[i].argb;
      return 1;
    }
  }
  return 0;
}

/* ---- display-list arena access ------------------------------------------- */

/* Return a pointer to a [off,len) payload slice in the arena, or NULL if it
 * falls outside the arena (fail-closed against a corrupt display list). */
static const char *dl_slice(const struct capy_dl *dl, size_t off, size_t len) {
  if (len == 0u) return NULL;
  if (off > dl->string_len || len > dl->string_len - off) return NULL;
  return dl->strings + off;
}

/* ---- public entry -------------------------------------------------------- */

int capyos_browser_render_pixels(const struct capy_dl *dl, uint32_t *out,
                                 uint32_t out_w, uint32_t out_h,
                                 const struct capyos_browser_pixel_opts *opts,
                                 struct capyos_browser_pixel_stats *stats) {
  struct capyos_browser_pixel_stats sink;
  uint32_t cw, ch, bg, fg, link;
  capyos_browser_image_resolver resolve_image = (opts) ? opts->resolve_image : 0;
  void *image_ctx = (opts) ? opts->image_ctx : 0;
  size_t n;
  int clipped_ignore = 0;

  if (stats == NULL) stats = &sink;
  stats->text_nodes = 0u;
  stats->rect_nodes = 0u;
  stats->image_nodes = 0u;
  stats->images_decoded = 0u;
  stats->link_nodes = 0u;
  stats->glyphs_drawn = 0u;
  stats->clipped = 0;
  stats->truncated = 0;

  if (out == NULL || out_w == 0u || out_h == 0u) {
    stats->truncated = 1;
    return -1;
  }
  if (dl == NULL || dl->version != CAPY_DL_VERSION) {
    /* fail-closed: leave the surface untouched so the caller can show its own
     * error UI rather than a half-painted page. */
    stats->truncated = 1;
    return -1;
  }

  cw = (opts && opts->cell_w) ? opts->cell_w : CAPYOS_BROWSER_PX_DEFAULT_CELL_W;
  ch = (opts && opts->cell_h) ? opts->cell_h : CAPYOS_BROWSER_PX_DEFAULT_CELL_H;
  bg = opts ? opts->bg : CAPYOS_BROWSER_PX_BG;
  fg = opts ? opts->fg : CAPYOS_BROWSER_PX_FG;
  link = opts ? opts->link : CAPYOS_BROWSER_PX_LINK;
  if (opts) {
    if (bg == 0u) bg = CAPYOS_BROWSER_PX_BG;
    if (fg == 0u) fg = CAPYOS_BROWSER_PX_FG;
    if (link == 0u) link = CAPYOS_BROWSER_PX_LINK;
  }

  /* Page background. */
  px_fill(out, out_w, out_h, 0, 0, (int64_t)out_w, (int64_t)out_h, bg,
          &clipped_ignore);

  for (n = 0u; n < dl->node_count; ++n) {
    const struct capy_dl_node *nd = &dl->nodes[n];
    int64_t px = (int64_t)nd->x * (int64_t)cw;
    int64_t py = (int64_t)nd->y * (int64_t)ch;

    switch (nd->kind) {
    case CAPY_DL_RECT: {
      uint32_t color = bg;
      const char *cs = dl_slice(dl, nd->color_off, nd->color_len);
      stats->rect_nodes++;
      if (cs && parse_color(cs, nd->color_len, &color)) {
        px_fill(out, out_w, out_h, px, py, (int64_t)nd->width * (int64_t)cw,
                (int64_t)nd->height * (int64_t)ch, color, &stats->clipped);
      }
      break;
    }
    case CAPY_DL_TEXT: {
      uint32_t color = fg;
      const char *cs = dl_slice(dl, nd->color_off, nd->color_len);
      const char *ts = dl_slice(dl, nd->text_off, nd->text_len);
      size_t i;
      stats->text_nodes++;
      if (cs) (void)parse_color(cs, nd->color_len, &color);
      if (!ts) break;
      for (i = 0u; i < nd->text_len; ++i) {
        unsigned char c = (unsigned char)ts[i];
        int64_t gx = px + (int64_t)i * (int64_t)cw;
        if (c < 0x20u || c >= 0x7Fu) c = '?'; /* sanitize ctrl + non-ASCII */
        if (gx >= (int64_t)out_w) {
          stats->clipped = 1;
          break;
        }
        if (px_glyph(out, out_w, out_h, gx, py, cw, ch, c, color))
          stats->glyphs_drawn++;
      }
      break;
    }
    case CAPY_DL_IMAGE: {
      /* Decode (via the injected resolver) and draw the real image when
       * available; else a bordered placeholder box + alt label. */
      int64_t w = (int64_t)(nd->width > 0 ? nd->width : 10) * (int64_t)cw;
      int64_t h = (int64_t)(nd->height > 0 ? nd->height : 3) * (int64_t)ch;
      const char *ls = dl_slice(dl, nd->label_off, nd->label_len);
      stats->image_nodes++;
      if (resolve_image != 0 && nd->url_len > 0u) {
        const char *src = dl_slice(dl, nd->url_off, nd->url_len);
        const uint32_t *ipx = 0;
        uint32_t iw = 0u, ih = 0u;
        if (src != 0 &&
            resolve_image(image_ctx, src, nd->url_len, &ipx, &iw, &ih) == 1 &&
            ipx != 0 && iw > 0u && ih > 0u) {
          px_blit_scaled(out, out_w, out_h, px, py, w, h, ipx, iw, ih,
                         &stats->clipped);
          stats->images_decoded++;
          break; /* drawn the decoded image; skip the placeholder */
        }
      }
      px_fill(out, out_w, out_h, px, py, w, h, 0xFFE8E8E8u, &stats->clipped);
      px_fill(out, out_w, out_h, px, py, w, 1, 0xFF888888u, &clipped_ignore);
      px_fill(out, out_w, out_h, px, py + h - 1, w, 1, 0xFF888888u,
              &clipped_ignore);
      px_fill(out, out_w, out_h, px, py, 1, h, 0xFF888888u, &clipped_ignore);
      px_fill(out, out_w, out_h, px + w - 1, py, 1, h, 0xFF888888u,
              &clipped_ignore);
      if (ls) {
        size_t i;
        for (i = 0u; i < nd->label_len; ++i) {
          unsigned char c = (unsigned char)ls[i];
          int64_t gx = px + 2 + (int64_t)i * (int64_t)cw;
          if (c < 0x20u || c >= 0x7Fu) c = '?';
          if (gx + (int64_t)cw > px + w) break; /* keep label inside the box */
          if (px_glyph(out, out_w, out_h, gx, py + 2, cw, ch, c, 0xFF555555u))
            stats->glyphs_drawn++;
        }
      }
      break;
    }
    case CAPY_DL_LINK: {
      /* Underline across the link bounds; anchor glyphs come as TEXT nodes. */
      int64_t w = (int64_t)(nd->width > 0 ? nd->width : 1) * (int64_t)cw;
      int64_t uy = py + (int64_t)(nd->height > 0 ? nd->height : 1) * (int64_t)ch -
                   1;
      stats->link_nodes++;
      px_fill(out, out_w, out_h, px, uy, w, 1, link, &stats->clipped);
      break;
    }
    default:
      break;
    }
  }

  if (dl->truncated) stats->truncated = 1;
  return 0;
}
