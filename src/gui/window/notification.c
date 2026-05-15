#include "gui/notification.h"
#include "gui/font.h"
#include <stddef.h>

static void n_memset(void *d, int v, size_t n) {
  uint8_t *p = (uint8_t *)d; for (size_t i = 0; i < n; i++) p[i] = (uint8_t)v;
}
static void n_strcpy(char *d, const char *s, size_t max) {
  size_t i = 0; while (i < max - 1 && s[i]) { d[i] = s[i]; i++; } d[i] = '\0';
}

void notify_init(struct notification_manager *nm, uint32_t sw, uint32_t sh) {
  if (!nm) return;
  n_memset(nm, 0, sizeof(*nm));
  nm->screen_w = sw;
  nm->screen_h = sh;
}

void notify_push(struct notification_manager *nm, const char *text, enum notify_type type) {
  if (!nm || !text) return;
  for (int i = 0; i < NOTIFY_MAX; i++) {
    if (!nm->items[i].active) {
      n_strcpy(nm->items[i].text, text, NOTIFY_TEXT_MAX);
      nm->items[i].type = type;
      nm->items[i].remaining_ticks = NOTIFY_DURATION_TICKS;
      nm->items[i].active = 1;
      return;
    }
  }
}

void notify_tick(struct notification_manager *nm) {
  if (!nm) return;
  for (int i = 0; i < NOTIFY_MAX; i++) {
    if (nm->items[i].active) {
      if (nm->items[i].remaining_ticks > 0) nm->items[i].remaining_ticks--;
      else nm->items[i].active = 0;
    }
  }
}

static void n_fill_rect(struct gui_surface *s, int32_t x, int32_t y,
                         uint32_t w, uint32_t h, uint32_t color) {
  for (uint32_t r = 0; r < h; r++) {
    int32_t py = y + (int32_t)r;
    if (py < 0 || (uint32_t)py >= s->height) continue;
    uint32_t *line = (uint32_t *)((uint8_t *)s->pixels + py * s->pitch);
    for (uint32_t c = 0; c < w; c++) {
      int32_t px = x + (int32_t)c;
      if (px >= 0 && (uint32_t)px < s->width) line[px] = color;
    }
  }
}

void notify_paint(struct notification_manager *nm, struct gui_surface *surface) {
  if (!nm || !surface) return;
  const struct font *f = font_default();
  const struct gui_theme_palette *theme = compositor_theme();
  if (!f || !theme) return;
  int32_t base_x = (nm->screen_w > NOTIFY_WIDTH + 12u)
                       ? (int32_t)(nm->screen_w - NOTIFY_WIDTH - 12u)
                       : 0;
  int32_t base_y = 12;
  int slot = 0;
  for (int i = 0; i < NOTIFY_MAX; i++) {
    if (!nm->items[i].active) continue;
    int32_t y = base_y + slot * (int32_t)(NOTIFY_HEIGHT + 8);
    uint32_t bg = theme->window_bg;
    uint32_t accent = theme->accent;
    switch (nm->items[i].type) {
      case NOTIFY_SUCCESS: accent = 0x00A6E3A1; break;
      case NOTIFY_WARNING: accent = 0x00F9E2AF; break;
      case NOTIFY_ERROR:   accent = 0x00F38BA8; break;
      default: break;
    }
    n_fill_rect(surface, base_x, y, NOTIFY_WIDTH, NOTIFY_HEIGHT,
                theme->window_border);
    n_fill_rect(surface, base_x + 1, y + 1, NOTIFY_WIDTH - 2,
                NOTIFY_HEIGHT - 2, bg);
    n_fill_rect(surface, base_x + 1, y + 1, 5, NOTIFY_HEIGHT - 2, accent);
    font_draw_string(surface, f, base_x + 14, y + 8, nm->items[i].text,
                     theme->text);
    slot++;
  }
}
