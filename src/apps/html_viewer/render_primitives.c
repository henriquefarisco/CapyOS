#include "internal/html_viewer_internal.h"

/* Draw a horizontal line. */
static void hv_draw_hline(struct gui_surface *s, int32_t x, int32_t y,
                           int32_t w, uint32_t color) {
  if (!s || y < 0 || (uint32_t)y >= s->height || w <= 0) return;
  uint32_t *row = (uint32_t *)((uint8_t *)s->pixels + (uint32_t)y * s->pitch);
  int32_t x1 = x < 0 ? 0 : x;
  int32_t x2 = x + w;
  if ((uint32_t)x2 > s->width) x2 = (int32_t)s->width;
  for (int32_t px = x1; px < x2; px++) row[(uint32_t)px] = color;
}

/* Draw a vertical line. */
static void hv_draw_vline(struct gui_surface *s, int32_t x, int32_t y,
                           int32_t h, uint32_t color) {
  if (!s || x < 0 || (uint32_t)x >= s->width || h <= 0) return;
  for (int32_t py = y; py < y + h; py++) {
    if (py < 0 || (uint32_t)py >= s->height) continue;
    uint32_t *row = (uint32_t *)((uint8_t *)s->pixels + (uint32_t)py * s->pitch);
    row[(uint32_t)x] = color;
  }
}

/* Draw a border rectangle (hollow, border_w pixels thick on each side). */
void hv_draw_border_rect(struct gui_surface *s, int32_t x, int32_t y,
                                 int32_t w, int32_t h, int bw, uint32_t color) {
  for (int t = 0; t < bw; t++) {
    hv_draw_hline(s, x, y + t, w, color);
    hv_draw_hline(s, x, y + h - 1 - t, w, color);
    hv_draw_vline(s, x + t, y, h, color);
    hv_draw_vline(s, x + w - 1 - t, y, h, color);
  }
}

/* Draw a single bitmap glyph scaled by an integer factor (nearest-neighbor). */
static void hv_draw_char_scaled(struct gui_surface *surface, const struct font *f,
                                int32_t x, int32_t y, char c, uint32_t color, int scale) {
  uint32_t idx = (uint32_t)(uint8_t)c;
  uint32_t glyph_offset;
  const uint8_t *glyph;
  if (!surface || !f || !f->data || scale < 1) return;
  if (idx < f->first_char || idx > f->last_char) return;
  glyph_offset = (idx - f->first_char) * f->bytes_per_glyph;
  glyph = f->data + glyph_offset;
  for (uint32_t row = 0; row < f->glyph_height; row++) {
    uint8_t bits = glyph[row];
    for (int sy = 0; sy < scale; sy++) {
      int32_t py = y + (int32_t)(row * (uint32_t)scale + (uint32_t)sy);
      if (py < 0 || (uint32_t)py >= surface->height) continue;
      uint32_t *line = (uint32_t *)((uint8_t *)surface->pixels + (uint32_t)py * surface->pitch);
      for (uint32_t col = 0; col < f->glyph_width; col++) {
        if (!(bits & (0x80 >> col))) continue;
        for (int sx = 0; sx < scale; sx++) {
          int32_t px = x + (int32_t)(col * (uint32_t)scale + (uint32_t)sx);
          if (px >= 0 && (uint32_t)px < surface->width) line[(uint32_t)px] = color;
        }
      }
    }
  }
}

/* Word-wrap text using a scaled bitmap font; returns total height in pixels. */
int hv_wrap_text_scaled(struct gui_surface *surface, const struct font *f,
                               int32_t x, int32_t y, int32_t max_width,
                               const char *text, uint32_t color, int scale) {
  int32_t char_w  = (int32_t)(f->glyph_width  * (uint32_t)scale);
  int32_t char_h  = (int32_t)(f->glyph_height * (uint32_t)scale) + 2;
  int32_t line_y  = y;
  int32_t cursor_x = x;
  size_t  i = 0;
  int drew = 0;
  if (!f || !text || !text[0]) return char_h;
  while (text[i]) {
    size_t word_start, word_len = 0;
    if (text[i] == '\n') { cursor_x = x; line_y += char_h; i++; continue; }
    while (text[i] == ' ') {
      if (cursor_x != x) {
        if (cursor_x - x + char_w > max_width) { cursor_x = x; line_y += char_h; }
        else cursor_x += char_w;
      }
      i++;
    }
    word_start = i;
    while (text[i] && text[i] != ' ' && text[i] != '\n') { word_len++; i++; }
    if (!word_len) break;
    if (cursor_x != x && cursor_x - x + (int32_t)word_len * char_w > max_width) {
      cursor_x = x; line_y += char_h;
    }
    for (size_t j = 0; j < word_len; j++) {
      if (surface)
        hv_draw_char_scaled(surface, f, cursor_x, line_y, text[word_start + j], color, scale);
      cursor_x += char_w;
    }
    drew = 1;
  }
  return drew ? (int)(line_y - y + char_h) : char_h;
}

/* Set by html_viewer_render_node before calling wrap_text to apply CSS line-height. */
int g_hv_line_height_px = 0;  /* 0 = use font default */

int html_viewer_wrap_text(struct gui_surface *surface, const struct font *f,
                                 int32_t x, int32_t y, int32_t max_width,
                                 const char *text, uint32_t color,
                                 int underline) {
  int32_t line_y = y;
  int32_t cursor_x = x;
  int32_t default_lh = (int32_t)f->glyph_height + 2;
  int32_t line_height = (g_hv_line_height_px > (int)f->glyph_height)
                        ? (int32_t)g_hv_line_height_px : default_lh;
  size_t i = 0;
  int drew = 0;
  if (!f) return 0;
  if (!text || !text[0]) return line_height;
  if (max_width < (int32_t)f->glyph_width) max_width = (int32_t)f->glyph_width;
  while (text[i]) {
    if (text[i] == '\n') {
      cursor_x = x;
      line_y += line_height;
      i++;
      continue;
    }
    while (text[i] == ' ') {
      if (cursor_x != x) {
        if (cursor_x - x + (int32_t)f->glyph_width > max_width) {
          cursor_x = x;
          line_y += line_height;
        } else {
          cursor_x += (int32_t)f->glyph_width;
        }
      }
      i++;
    }
    if (!text[i]) break;
    {
      size_t word_start = i;
      size_t word_len = 0;
      int32_t word_width = 0;
      while (text[i] && text[i] != ' ' && text[i] != '\n') i++;
      word_len = i - word_start;
      word_width = (int32_t)word_len * (int32_t)f->glyph_width;
      if (cursor_x != x && word_width <= max_width &&
          cursor_x - x + word_width > max_width) {
        cursor_x = x;
        line_y += line_height;
      }
      if (word_width > max_width) {
        for (size_t j = 0; j < word_len; j++) {
          if (cursor_x != x &&
              cursor_x - x + (int32_t)f->glyph_width > max_width) {
            cursor_x = x;
            line_y += line_height;
          }
          if (surface) {
            font_draw_char(surface, f, cursor_x, line_y,
                           text[word_start + j], color);
            if (underline) {
              int32_t uy = line_y + (int32_t)f->glyph_height;
              if (uy >= 0 && (uint32_t)uy < surface->height) {
                uint32_t *row = (uint32_t *)((uint8_t *)surface->pixels +
                                             (uint32_t)uy * surface->pitch);
                for (uint32_t ux = 0; ux < f->glyph_width &&
                                     (uint32_t)(cursor_x + (int32_t)ux) < surface->width; ux++) {
                  if (cursor_x + (int32_t)ux >= 0) {
                    row[(uint32_t)(cursor_x + (int32_t)ux)] = color;
                  }
                }
              }
            }
          }
          cursor_x += (int32_t)f->glyph_width;
          drew = 1;
        }
      } else {
        int32_t start_x = cursor_x;
        if (surface) {
          for (size_t j = 0; j < word_len; j++) {
            font_draw_char(surface, f, cursor_x, line_y, text[word_start + j], color);
            cursor_x += (int32_t)f->glyph_width;
          }
          if (underline) {
            int32_t uy = line_y + (int32_t)f->glyph_height;
            if (uy >= 0 && (uint32_t)uy < surface->height) {
              uint32_t *row = (uint32_t *)((uint8_t *)surface->pixels +
                                           (uint32_t)uy * surface->pitch);
              for (int32_t ux = start_x; ux < cursor_x; ux++) {
                if (ux >= 0 && (uint32_t)ux < surface->width) row[(uint32_t)ux] = color;
              }
            }
          }
        } else {
          cursor_x += word_width;
        }
        drew = 1;
      }
    }
  }
  return drew ? (int)(line_y - y + line_height) : line_height;
}

/* Like html_viewer_wrap_text but starts at x_start (> x_left).
   Wraps to x_left on newline/overflow. Returns height; sets *out_end_x. */
int html_viewer_wrap_text_from(
    struct gui_surface *surface, const struct font *f,
    int32_t x_left, int32_t x_start, int32_t y, int32_t max_width,
    const char *text, uint32_t color, int underline, int32_t *out_end_x) {
  int32_t line_y = y;
  int32_t cursor_x = x_start;
  int32_t default_lh2 = (int32_t)f->glyph_height + 2;
  int32_t line_height = (g_hv_line_height_px > (int)f->glyph_height)
                        ? (int32_t)g_hv_line_height_px : default_lh2;
  size_t i = 0;
  int drew = 0;
  if (!f || !text || !text[0]) { if (out_end_x) *out_end_x = x_start; return line_height; }
  if (max_width < (int32_t)f->glyph_width) max_width = (int32_t)f->glyph_width;
  while (text[i]) {
    if (text[i] == '\n') { cursor_x = x_left; line_y += line_height; i++; continue; }
    while (text[i] == ' ') {
      if (cursor_x != x_left) {
        if (cursor_x - x_left + (int32_t)f->glyph_width > max_width) {
          cursor_x = x_left; line_y += line_height;
        } else cursor_x += (int32_t)f->glyph_width;
      }
      i++;
    }
    if (!text[i]) break;
    {
      size_t word_start = i;
      size_t word_len = 0;
      int32_t word_width;
      while (text[i] && text[i] != ' ' && text[i] != '\n') i++;
      word_len = i - word_start;
      word_width = (int32_t)word_len * (int32_t)f->glyph_width;
      if (cursor_x != x_left && word_width <= max_width &&
          cursor_x - x_left + word_width > max_width) {
        cursor_x = x_left; line_y += line_height;
      }
      if (surface) {
        int32_t start_x = cursor_x;
        for (size_t j = 0; j < word_len; j++) {
          if (cursor_x != x_left &&
              cursor_x - x_left + (int32_t)f->glyph_width > max_width) {
            cursor_x = x_left; line_y += line_height;
          }
          font_draw_char(surface, f, cursor_x, line_y, text[word_start + j], color);
          cursor_x += (int32_t)f->glyph_width;
        }
        if (underline) {
          int32_t uy = line_y + (int32_t)f->glyph_height;
          if (uy >= 0 && (uint32_t)uy < surface->height) {
            uint32_t *row = (uint32_t *)((uint8_t *)surface->pixels +
                                         (uint32_t)uy * surface->pitch);
            for (int32_t ux = start_x; ux < cursor_x; ux++)
              if (ux >= 0 && (uint32_t)ux < surface->width) row[(uint32_t)ux] = color;
          }
        }
      } else {
        cursor_x += word_width;
      }
      drew = 1;
    }
  }
  if (out_end_x) *out_end_x = cursor_x;
  return drew ? (int)(line_y - y + line_height) : line_height;
}

/* Returns 1 if the node should participate in horizontal inline flow. */
int hv_node_is_inline(const struct html_node *node) {
  enum html_node_type t;
  if (!node || node->hidden || !node->text[0]) return 0;
  /* display:inline or display:inline-block overrides normal block behaviour */
  if (node->css_display == 1 || node->css_display == 2) return 1;
  t = node->type;
  return t == HTML_NODE_TAG_A || t == HTML_NODE_TAG_SPAN ||
         t == HTML_NODE_TAG_MARK || t == HTML_NODE_TEXT;
}

int html_viewer_node_margin_top(const struct html_node *node) {
  enum html_node_type type = node->type;
  if (node->css_margin_top) return (int)node->css_margin_top;
  if (type == HTML_NODE_TAG_H1) return 8;
  if (type == HTML_NODE_TAG_H2) return 6;
  if (type == HTML_NODE_TAG_H3) return 4;
  if (type == HTML_NODE_TAG_H4) return 4;
  if (type == HTML_NODE_TAG_H5) return 3;
  if (type == HTML_NODE_TAG_H6) return 2;
  if (type == HTML_NODE_TAG_HR) return 6;
  if (type == HTML_NODE_TAG_BLOCKQUOTE) return 6;
  if (type == HTML_NODE_TAG_PRE) return 4;
  if (type == HTML_NODE_TAG_TR) return 2;
  return 2;
}

int html_viewer_node_margin_bottom(const struct html_node *node) {
  enum html_node_type type = node->type;
  if (node->css_margin_bottom) return (int)node->css_margin_bottom;
  if (type == HTML_NODE_TAG_H1) return 10;
  if (type == HTML_NODE_TAG_H2) return 8;
  if (type == HTML_NODE_TAG_H3) return 6;
  if (type == HTML_NODE_TAG_H4) return 6;
  if (type == HTML_NODE_TAG_H5) return 4;
  if (type == HTML_NODE_TAG_H6) return 4;
  if (type == HTML_NODE_TAG_BR) return 6;
  if (type == HTML_NODE_TAG_HR) return 6;
  if (type == HTML_NODE_TAG_BLOCKQUOTE) return 6;
  if (type == HTML_NODE_TAG_PRE) return 4;
  if (type == HTML_NODE_TAG_TR) return 2;
  if (type == HTML_NODE_TAG_TD) return 1;
  return 4;
}

uint32_t html_viewer_node_color(const struct gui_theme_palette *theme,
                                       const struct html_node *node) {
  if (!theme || !node) return 0xCDD6F4;
  /* CSS inline/stylesheet color takes priority */
  if (node->css_color) return node->css_color;
  if (node->type == HTML_NODE_TAG_H1) return theme->accent;
  if (node->type == HTML_NODE_TAG_H2) return theme->accent_alt;
  if (node->type == HTML_NODE_TAG_H3) return theme->accent_alt;
  if (node->type == HTML_NODE_TAG_H4) return theme->accent_alt;
  if (node->type == HTML_NODE_TAG_H5) return theme->text;
  if (node->type == HTML_NODE_TAG_H6) return theme->text_muted;
  if (node->type == HTML_NODE_TAG_A) return theme->accent;
  if (node->type == HTML_NODE_TAG_BUTTON) return theme->accent_alt;
  if (node->type == HTML_NODE_TAG_INPUT) return theme->text;
  if (node->type == HTML_NODE_TAG_PRE) return theme->terminal_fg;
  if (node->type == HTML_NODE_TAG_CODE) return theme->terminal_fg;
  if (node->type == HTML_NODE_TAG_BLOCKQUOTE) return theme->text_muted;
  if (node->type == HTML_NODE_TAG_MARK) return 0xF9E2AF;
  if (node->type == HTML_NODE_TAG_TD) return node->bold ? theme->accent_alt : theme->text;
  if (node->type == HTML_NODE_TAG_FIGCAPTION) return theme->text_muted;
  if (node->type == HTML_NODE_TAG_DETAILS) return theme->accent;
  if (node->type == HTML_NODE_TAG_MEDIA) return theme->text_muted;
  return node->color ? node->color : theme->text;
}
