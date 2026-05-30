/* src/gui/widgets/context_menu.c
 *
 * Etapa UX W7-ish (2026-05-03): implementacao do popup de menu de
 * contexto. Singleton estatico (so 1 menu aberto por vez), undecorated
 * window com corner_radius=4 + border do tema. Hover destacado em
 * accent color. Contrato em include/gui/context_menu.h.
 */
#include "gui/context_menu.h"
#include "gui/compositor.h"
#include "gui/font.h"
#include <stddef.h>

#if defined(CAPYOS_HAVE_CAPYUI_WIDGET)
int context_menu_render_display_list(struct gui_window *win,
                                     const struct context_menu_item *items,
                                     uint32_t count,
                                     int32_t hover_index);
void context_menu_display_list_reset(void);
#endif

/* Estado singleton. */
static struct {
  struct gui_window *win;
  struct context_menu_item items[CONTEXT_MENU_MAX_ITEMS];
  uint32_t count;
  int32_t  hover_index;
  context_menu_pick_fn on_pick;
  void *ctx;
} g_ctx = {0};

static void cm_strcpy(char *d, const char *s, uint32_t max) {
  uint32_t i = 0;
  if (!d || max == 0u) return;
  if (s) {
    while (i + 1u < max && s[i]) { d[i] = s[i]; i++; }
  }
  d[i] = '\0';
}

static void cm_fill_rect(struct gui_surface *s, int32_t x, int32_t y,
                          uint32_t w, uint32_t h, uint32_t color) {
  if (!s) return;
  for (uint32_t r = 0; r < h; r++) {
    int32_t py = y + (int32_t)r;
    if (py < 0 || (uint32_t)py >= s->height) continue;
    uint32_t *line = (uint32_t *)((uint8_t *)s->pixels + (uint32_t)py * s->pitch);
    for (uint32_t c = 0; c < w; c++) {
      int32_t px = x + (int32_t)c;
      if (px >= 0 && (uint32_t)px < s->width) line[px] = color;
    }
  }
}

static void cm_fit_text(const struct font *f, const char *src,
                        uint32_t max_width, char *out, uint32_t out_len) {
  uint32_t len = 0;
  uint32_t max_chars = 0;
  if (!out || out_len == 0u) return;
  out[0] = '\0';
  if (!f || !src || max_width == 0u || f->glyph_width == 0u) return;
  max_chars = max_width / f->glyph_width;
  if (max_chars == 0u) return;
  while (src[len]) len++;
  if (len <= max_chars && len < out_len) {
    cm_strcpy(out, src, out_len);
    return;
  }
  if (max_chars <= 3u || out_len <= 4u) {
    uint32_t n = max_chars;
    if (n >= out_len) n = out_len - 1u;
    for (uint32_t i = 0; i < n; i++) out[i] = '.';
    out[n] = '\0';
    return;
  }
  {
    uint32_t copy = max_chars - 3u;
    if (copy > out_len - 4u) copy = out_len - 4u;
    for (uint32_t i = 0; i < copy; i++) out[i] = src[i];
    out[copy] = '.';
    out[copy + 1u] = '.';
    out[copy + 2u] = '.';
    out[copy + 3u] = '\0';
  }
}

static void cm_draw_fit(struct gui_surface *s, const struct font *f,
                        int32_t x, int32_t y, uint32_t max_width,
                        const char *text, uint32_t color) {
  char fitted[CONTEXT_MENU_LABEL_MAX];
  cm_fit_text(f, text, max_width, fitted, sizeof(fitted));
  if (fitted[0]) font_draw_string(s, f, x, y, fitted, color);
}

/* Mistura "para cima" cada canal RGB; identico ao tb_lighten do
 * taskbar mas duplicado deliberadamente para manter o modulo
 * autocontido (sem cross-include). */
static uint32_t cm_lighten(uint32_t color, uint8_t amount) {
  uint32_t r = (color >> 16) & 0xFFu;
  uint32_t g = (color >> 8) & 0xFFu;
  uint32_t b = color & 0xFFu;
  uint32_t a = color & 0xFF000000u;
  r = r + ((255u - r) * amount) / 255u;
  g = g + ((255u - g) * amount) / 255u;
  b = b + ((255u - b) * amount) / 255u;
  if (r > 255u) r = 255u;
  if (g > 255u) g = 255u;
  if (b > 255u) b = 255u;
  return a | (r << 16) | (g << 8) | b;
}

static uint32_t cm_total_height(void) {
  uint32_t h = 4u;
  for (uint32_t i = 0; i < g_ctx.count; i++) {
    h += (g_ctx.items[i].label[0] == '\0')
              ? CONTEXT_MENU_SEP_H
              : CONTEXT_MENU_ITEM_H;
  }
  return h;
}

static int cm_item_rect(int32_t index, struct gui_rect *out) {
  int32_t ey = 2;
  if (!g_ctx.win || !out || index < 0) return 0;
  for (uint32_t i = 0; i < g_ctx.count; i++) {
    int sep = (g_ctx.items[i].label[0] == '\0');
    uint32_t row_h = sep ? CONTEXT_MENU_SEP_H : CONTEXT_MENU_ITEM_H;
    if ((int32_t)i == index) {
      out->x = 0;
      out->y = ey;
      out->width = g_ctx.win->surface.width;
      out->height = row_h;
      return sep ? 0 : 1;
    }
    ey += (int32_t)row_h;
  }
  return 0;
}

static void cm_invalidate_item(int32_t index) {
  struct gui_rect rect;
  if (g_ctx.win && cm_item_rect(index, &rect)) {
    compositor_invalidate_rect(g_ctx.win->id, &rect);
  }
}

static void cm_paint(struct gui_window *win) {
  if (!win || win != g_ctx.win) return;
#if defined(CAPYOS_HAVE_CAPYUI_WIDGET)
  if (context_menu_render_display_list(win, g_ctx.items, g_ctx.count,
                                       g_ctx.hover_index) == 0) return;
#endif
  const struct gui_theme_palette *theme = compositor_theme();
  const struct font *f = font_default();
  struct gui_surface *s = &win->surface;

  /* Background; cantos arredondados sao mascarados pelo compositor. */
  cm_fill_rect(s, 0, 0, s->width, s->height, theme->window_bg);
  if (!f) return;

  int32_t ey = 2;
  for (uint32_t i = 0; i < g_ctx.count; i++) {
    int sep = (g_ctx.items[i].label[0] == '\0');
    uint32_t row_h = sep ? CONTEXT_MENU_SEP_H : CONTEXT_MENU_ITEM_H;
    if (sep) {
      cm_fill_rect(s, 8, ey + (int32_t)(row_h / 2),
                    s->width - 16, 1, theme->window_border);
    } else {
      uint32_t txt_color = g_ctx.items[i].enabled ? theme->text
                                                   : theme->text_muted;
      if ((int32_t)i == g_ctx.hover_index && g_ctx.items[i].enabled) {
        uint32_t hover_bg = cm_lighten(theme->window_bg, 50u);
        cm_fill_rect(s, 2, ey, s->width - 4, row_h, hover_bg);
        /* Marcador esquerdo accent. */
        cm_fill_rect(s, 2, ey, 3, row_h, theme->accent);
        txt_color = theme->accent;
      }
      cm_draw_fit(s, f, 12, ey + 4,
                  (s->width > 24u) ? s->width - 24u : 0u,
                  g_ctx.items[i].label, txt_color);
    }
    ey += (int32_t)row_h;
  }
}

int context_menu_show(const struct context_menu_item *items, uint32_t count,
                      int32_t screen_x, int32_t screen_y,
                      context_menu_pick_fn on_pick, void *ctx) {
  if (!items || count == 0u) return -1;
  context_menu_close();
#if defined(CAPYOS_HAVE_CAPYUI_WIDGET)
  context_menu_display_list_reset();
#endif

  if (count > CONTEXT_MENU_MAX_ITEMS) count = CONTEXT_MENU_MAX_ITEMS;
  for (uint32_t i = 0; i < count; i++) {
    cm_strcpy(g_ctx.items[i].label, items[i].label, CONTEXT_MENU_LABEL_MAX);
    g_ctx.items[i].action_id = items[i].action_id;
    g_ctx.items[i].enabled   = items[i].enabled;
    g_ctx.items[i].reserved  = 0u;
  }
  g_ctx.count = count;
  g_ctx.hover_index = -1;
  g_ctx.on_pick = on_pick;
  g_ctx.ctx = ctx;

  uint32_t h = cm_total_height();
  uint32_t w = CONTEXT_MENU_WIDTH;
  uint32_t screen_w = 0u;
  uint32_t screen_h = 0u;

  int32_t x = screen_x;
  int32_t y = screen_y;
  compositor_screen_size(&screen_w, &screen_h);
  if (x < 0) x = 0;
  if (y < 0) y = 0;
  if (screen_w > 0u) {
    int32_t max_x = (screen_w > w) ? (int32_t)(screen_w - w) : 0;
    if (x > max_x) x = max_x;
  }
  if (screen_h > 0u) {
    int32_t max_y = (screen_h > h) ? (int32_t)(screen_h - h) : 0;
    if (y > max_y) y = max_y;
  }

  g_ctx.win = compositor_create_window("Context", x, y, w, h);
  if (!g_ctx.win) {
    g_ctx.count = 0u;
    g_ctx.on_pick = NULL;
    g_ctx.ctx = NULL;
    return -1;
  }
  g_ctx.win->decorated = 0;
  g_ctx.win->movable = 0;
  g_ctx.win->resizable = 0;
  g_ctx.win->corner_radius = 4;
  g_ctx.win->border_color = compositor_theme()->window_border;
  g_ctx.win->bg_color = compositor_theme()->window_bg;
  g_ctx.win->z_order = COMPOSITOR_MAX_WINDOWS + 6;
  g_ctx.win->user_data = NULL;
  g_ctx.win->on_paint = cm_paint;
  compositor_show_window(g_ctx.win->id);
  return 0;
}

void context_menu_close(void) {
  if (g_ctx.win) {
    compositor_destroy_window(g_ctx.win->id);
    g_ctx.win = NULL;
  }
  g_ctx.count = 0u;
  g_ctx.hover_index = -1;
  g_ctx.on_pick = NULL;
  g_ctx.ctx = NULL;
#if defined(CAPYOS_HAVE_CAPYUI_WIDGET)
  context_menu_display_list_reset();
#endif
}

int context_menu_is_open(void) {
  return g_ctx.win != NULL;
}

int context_menu_handle_click(int32_t screen_x, int32_t screen_y) {
  if (!g_ctx.win) return 0;
  int32_t px = g_ctx.win->frame.x;
  int32_t py = g_ctx.win->frame.y;
  uint32_t pw = g_ctx.win->frame.width;
  uint32_t ph = g_ctx.win->frame.height;

  if (screen_x < px || screen_x >= px + (int32_t)pw ||
      screen_y < py || screen_y >= py + (int32_t)ph) {
    /* Click fora -> fecha sem callback. */
    context_menu_close();
    return 1;
  }

  int32_t local_y = screen_y - py;
  int32_t ey = 2;
  for (uint32_t i = 0; i < g_ctx.count; i++) {
    int sep = (g_ctx.items[i].label[0] == '\0');
    uint32_t row_h = sep ? CONTEXT_MENU_SEP_H : CONTEXT_MENU_ITEM_H;
    if (local_y >= ey && local_y < ey + (int32_t)row_h) {
      if (sep || !g_ctx.items[i].enabled) return 1;
      uint16_t aid = g_ctx.items[i].action_id;
      context_menu_pick_fn cb = g_ctx.on_pick;
      void *cb_ctx = g_ctx.ctx;
      context_menu_close();
      if (cb) cb(aid, cb_ctx);
      return 1;
    }
    ey += (int32_t)row_h;
  }
  return 1; /* dentro mas nao em item; segura o click */
}

void context_menu_handle_hover(int32_t screen_x, int32_t screen_y) {
  if (!g_ctx.win) return;
  int32_t px = g_ctx.win->frame.x;
  int32_t py = g_ctx.win->frame.y;
  uint32_t pw = g_ctx.win->frame.width;
  uint32_t ph = g_ctx.win->frame.height;

  int new_hover = -1;
  if (screen_x >= px && screen_x < px + (int32_t)pw &&
      screen_y >= py && screen_y < py + (int32_t)ph) {
    int32_t local_y = screen_y - py;
    int32_t ey = 2;
    for (uint32_t i = 0; i < g_ctx.count; i++) {
      int sep = (g_ctx.items[i].label[0] == '\0');
      uint32_t row_h = sep ? CONTEXT_MENU_SEP_H : CONTEXT_MENU_ITEM_H;
      if (local_y >= ey && local_y < ey + (int32_t)row_h) {
        if (!sep && g_ctx.items[i].enabled) new_hover = (int32_t)i;
        break;
      }
      ey += (int32_t)row_h;
    }
  }
  if (new_hover != g_ctx.hover_index) {
    int32_t old_hover = g_ctx.hover_index;
    g_ctx.hover_index = new_hover;
    cm_invalidate_item(old_hover);
    cm_invalidate_item(new_hover);
  }
}
