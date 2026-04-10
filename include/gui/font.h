#ifndef GUI_FONT_H
#define GUI_FONT_H

#include <stdint.h>
#include <stddef.h>
#include "gui/compositor.h"

#define FONT_GLYPH_WIDTH  8
#define FONT_GLYPH_HEIGHT 16
#define FONT_FIRST_CHAR   32
#define FONT_LAST_CHAR    126
#define FONT_GLYPH_COUNT  (FONT_LAST_CHAR - FONT_FIRST_CHAR + 1)

struct font {
  const uint8_t *data;
  uint32_t glyph_width;
  uint32_t glyph_height;
  uint32_t first_char;
  uint32_t last_char;
  uint32_t bytes_per_glyph;
};

struct font_metrics {
  uint32_t ascent;
  uint32_t descent;
  uint32_t line_height;
  uint32_t avg_width;
};

void font_init(void);
const struct font *font_default(void);
void font_draw_char(struct gui_surface *surface, const struct font *f,
                    int32_t x, int32_t y, char c, uint32_t color);
void font_draw_string(struct gui_surface *surface, const struct font *f,
                      int32_t x, int32_t y, const char *text, uint32_t color);
uint32_t font_string_width(const struct font *f, const char *text);
uint32_t font_string_height(const struct font *f, const char *text);
void font_metrics_get(const struct font *f, struct font_metrics *out);

#endif /* GUI_FONT_H */
