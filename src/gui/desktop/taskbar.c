#include "gui/taskbar.h"
#include "gui/font.h"
#include "gui/compositor.h"
#include "lang/app_language.h"
#include <stddef.h>

static void tb_strcpy(char *d, const char *s, size_t max) {
  size_t i = 0;
  while (i < max - 1 && s[i]) { d[i] = s[i]; i++; }
  d[i] = '\0';
}

static void tb_fill_rect(struct gui_surface *s, int32_t x, int32_t y,
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

static void taskbar_window_paint(struct gui_window *win) {
  if (!win || !win->user_data) return;
  taskbar_paint((struct taskbar *)win->user_data);
}

void taskbar_init(struct taskbar *tb, uint32_t screen_w, uint32_t screen_h) {
  const struct gui_theme_palette *theme = compositor_theme();
  if (!tb) return;
  tb->position = TASKBAR_BOTTOM;
  tb->bg_color = theme->taskbar_bg;
  tb->fg_color = theme->taskbar_fg;
  tb->highlight_color = theme->taskbar_highlight;
  tb->item_count = 0;
  tb->menu_open = 0;
  tb->menu_entry_count = 0;
  tb->menu_popup = NULL;
  tb->hover_entry = -1;
  tb->show_clock = 1;
  tb_strcpy(tb->clock_text, "00:00:00", 16);

  int32_t y = (int32_t)(screen_h - TASKBAR_HEIGHT);
  tb->window = compositor_create_window("Taskbar", 0, y, screen_w, TASKBAR_HEIGHT);
  if (tb->window) {
    tb->window->decorated = 0;
    tb->window->movable = 0;
    tb->window->resizable = 0;
    tb->window->corner_radius = 0; /* taskbar fica retangular */
    tb->window->z_order = COMPOSITOR_MAX_WINDOWS + 4;
    tb->window->bg_color = theme->taskbar_bg;
    tb->window->user_data = tb;
    tb->window->on_paint = taskbar_window_paint;
    compositor_show_window(tb->window->id);
  }
}

void taskbar_add_window(struct taskbar *tb, uint32_t window_id, const char *name) {
  if (!tb || tb->item_count >= TASKBAR_MAX_ITEMS) return;
  /* Check if window is already in the taskbar */
  for (uint32_t i = 0; i < tb->item_count; i++) {
    if (tb->items[i].window_id == window_id) return;
  }
  {
    struct taskbar_item *item = &tb->items[tb->item_count++];
    item->window_id = window_id;
    tb_strcpy(item->name, name ? name : "Window", TASKBAR_ITEM_NAME_MAX);
    item->active = 1;
    item->focused = 0;
    if (tb->window) compositor_invalidate(tb->window->id);
  }
}

void taskbar_remove_window(struct taskbar *tb, uint32_t window_id) {
  if (!tb) return;
  for (uint32_t i = 0; i < tb->item_count; i++) {
    if (tb->items[i].window_id == window_id) {
      for (uint32_t j = i; j < tb->item_count - 1; j++)
        tb->items[j] = tb->items[j + 1];
      tb->item_count--;
      if (tb->window) compositor_invalidate(tb->window->id);
      return;
    }
  }
}

void taskbar_set_focused(struct taskbar *tb, uint32_t window_id) {
  int changed = 0;
  if (!tb) return;
  for (uint32_t i = 0; i < tb->item_count; i++) {
    int focused = (tb->items[i].window_id == window_id) ? 1 : 0;
    if (tb->items[i].focused != focused) {
      tb->items[i].focused = focused;
      changed = 1;
    }
  }
  if (changed && tb->window) compositor_invalidate(tb->window->id);
}

int taskbar_update_clock(struct taskbar *tb, const char *time_str) {
  if (!tb || !time_str) return 0;
  if (tb->clock_text[0] == time_str[0] &&
      tb->clock_text[1] == time_str[1] &&
      tb->clock_text[2] == time_str[2] &&
      tb->clock_text[3] == time_str[3] &&
      tb->clock_text[4] == time_str[4] &&
      tb->clock_text[5] == time_str[5] &&
      tb->clock_text[6] == time_str[6] &&
      tb->clock_text[7] == time_str[7] &&
      tb->clock_text[8] == time_str[8]) {
    return 0;
  }
  tb_strcpy(tb->clock_text, time_str, 16);
  if (tb->window) compositor_invalidate(tb->window->id);
  return 1;
}

void taskbar_paint(struct taskbar *tb) {
  const struct gui_theme_palette *theme = compositor_theme();
  uint8_t scale = compositor_ui_scale();
  uint32_t menu_w = 60 + 16 * (scale - 1);
  uint32_t item_w = 120 + 28 * (scale - 1);
  if (!tb || !tb->window) return;
  struct gui_surface *s = &tb->window->surface;
  tb->bg_color = theme->taskbar_bg;
  tb->fg_color = theme->taskbar_fg;
  tb->highlight_color = theme->taskbar_highlight;

  tb_fill_rect(s, 0, 0, s->width, s->height, tb->bg_color);

  /* Separator line at top */
  tb_fill_rect(s, 0, 0, s->width, 1, theme->window_border);

  const struct font *f = font_default();
  if (!f) return;

  /* Menu button */
  int32_t x = 4;
  uint32_t menu_btn_bg = tb->menu_open ? theme->accent_alt : theme->accent;
  tb_fill_rect(s, x, 4, menu_w, TASKBAR_HEIGHT - 8, menu_btn_bg);
  font_draw_string(s, f, x + 8, 8,
                   APP_T("Menu", "Menu", "Menu"), theme->accent_text);
  x += (int32_t)menu_w + 8;

  /* Window list */
  for (uint32_t i = 0; i < tb->item_count; i++) {
    struct taskbar_item *item = &tb->items[i];
    uint32_t bg = item->focused ? tb->highlight_color : tb->bg_color;
    tb_fill_rect(s, x, 4, item_w, TASKBAR_HEIGHT - 8, bg);
    font_draw_string(s, f, x + 4, 8, item->name, tb->fg_color);
    x += (int32_t)item_w + 4;
  }

  /* Clock on right side */
  if (tb->show_clock) {
    uint32_t cw = font_string_width(f, tb->clock_text);
    int32_t cx = (int32_t)(s->width - cw - 12);
    font_draw_string(s, f, cx, 8, tb->clock_text, tb->fg_color);
  }
}

void taskbar_handle_click(struct taskbar *tb, int32_t x, int32_t y) {
  uint8_t scale = compositor_ui_scale();
  int32_t menu_w = 60 + 16 * (scale - 1);
  int32_t item_x = menu_w + 12;
  int32_t item_w = 120 + 28 * (scale - 1);
  if (!tb) return;
  (void)y;

  if (x >= 4 && x < 4 + menu_w) {
    taskbar_toggle_menu(tb);
    return;
  }

  /* Click outside the menu button while menu is open -> close it */
  if (tb->menu_open) {
    taskbar_toggle_menu(tb);
  }

  for (uint32_t i = 0; i < tb->item_count; i++) {
    if (x >= item_x && x < item_x + item_w) {
      compositor_focus_window(tb->items[i].window_id);
      compositor_show_window(tb->items[i].window_id);
      taskbar_set_focused(tb, tb->items[i].window_id);
      return;
    }
    item_x += item_w + 4;
  }
}

static uint32_t menu_total_height(struct taskbar *tb) {
  uint32_t h = 4;
  for (uint32_t i = 0; i < tb->menu_entry_count; i++)
    h += tb->menu_entries[i].is_separator ? TASKBAR_MENU_SEP_HEIGHT
                                          : TASKBAR_MENU_ENTRY_HEIGHT;
  return h;
}

/* Etapa UX W7-ish (2026-05-03): mistura "para cima" cada canal RGB
 * por uma fracao 0..255 (255 = branco puro). Mantem hue, aumenta
 * brilho. Util para o efeito "fade" de hover sobre uma cor de bg
 * arbitraria sem precisar de palette extra. */
static uint32_t tb_lighten(uint32_t color, uint8_t amount) {
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

static void menu_popup_paint(struct gui_window *win) {
  if (!win || !win->user_data) return;
  struct taskbar *tb = (struct taskbar *)win->user_data;
  const struct gui_theme_palette *theme = compositor_theme();
  const struct font *f = font_default();
  struct gui_surface *s = &win->surface;
  if (!f) return;

  /* Background. O compositor mascara os cantos arredondados;
   * border externa eh desenhada pelo render_window_outline (corner
   * radius habilitado via win->corner_radius). */
  tb_fill_rect(s, 0, 0, s->width, s->height, theme->window_bg);

  /* Etapa UX W7-ish (2026-05-03): faixa lateral de 4 px usando
   * accent_alt como "marcador visual" - inspirado na barra colorida
   * do start menu do Windows 7. Largura fixa 4 px, altura cheia. */
  tb_fill_rect(s, 0, 0, 4, s->height, theme->accent_alt);

  int32_t ey = 2;
  for (uint32_t i = 0; i < tb->menu_entry_count; i++) {
    uint32_t row_h = tb->menu_entries[i].is_separator ? TASKBAR_MENU_SEP_HEIGHT
                                                      : TASKBAR_MENU_ENTRY_HEIGHT;
    if (tb->menu_entries[i].is_separator) {
      tb_fill_rect(s, 12, ey + (int32_t)(row_h / 2),
                   s->width - 24, 1, theme->window_border);
    } else {
      /* Etapa UX W7-ish (2026-05-03): hover highlight. A linha sob o
       * cursor recebe um bg ligeiramente mais claro que o window_bg
       * (efeito "brilho/fade"). Pin-test: tb->hover_entry == i. */
      if ((int)i == tb->hover_entry) {
        uint32_t hover_bg = tb_lighten(theme->window_bg, 40u);
        /* Linha do hover: bg uniforme + texto em cor accent (mais
         * destacado). Inset lateral de 4 px (borda colorida +
         * margem). */
        tb_fill_rect(s, 4, ey, s->width - 4, row_h, hover_bg);
        /* Marcador horizontal no inicio da linha (3 px de
         * accent_alt), reforca o destaque. */
        tb_fill_rect(s, 4, ey, 4, row_h, theme->accent);
        font_draw_string(s, f, 16, ey + 6, tb->menu_entries[i].label,
                         theme->accent);
      } else {
        font_draw_string(s, f, 16, ey + 6, tb->menu_entries[i].label,
                         theme->text);
      }
    }
    ey += (int32_t)row_h;
  }
}

void taskbar_toggle_menu(struct taskbar *tb) {
  if (!tb) return;
  tb->menu_open = !tb->menu_open;

  if (tb->menu_open) {
    /* Create popup on first open; reuse it afterwards */
    if (!tb->menu_popup && tb->menu_entry_count > 0) {
      uint32_t popup_h = menu_total_height(tb);
      int32_t popup_y = 0;
      if (tb->window)
        popup_y = tb->window->frame.y - (int32_t)popup_h;
      tb->menu_popup = compositor_create_window(
          "Menu", 0, popup_y, TASKBAR_MENU_WIDTH, popup_h);
      if (tb->menu_popup) {
        tb->menu_popup->decorated = 0;
        tb->menu_popup->movable = 0;
        tb->menu_popup->resizable = 0;
        /* Etapa UX W7-ish (2026-05-03): cantos arredondados (raio
         * 6 px) + border externa do tema. O compositor desenha
         * automaticamente quando corner_radius != 0. */
        tb->menu_popup->corner_radius = 6;
        tb->menu_popup->border_color =
            compositor_theme()->window_border;
        tb->menu_popup->z_order = COMPOSITOR_MAX_WINDOWS + 5;
        tb->menu_popup->bg_color = compositor_theme()->window_bg;
        tb->menu_popup->user_data = tb;
        tb->menu_popup->on_paint = menu_popup_paint;
      }
    }
    if (tb->menu_popup) {
      tb->hover_entry = -1; /* sem hover ativo ao abrir */
      compositor_show_window(tb->menu_popup->id);
      compositor_invalidate(tb->menu_popup->id);
    }
  } else {
    tb->hover_entry = -1;
    if (tb->menu_popup) {
      compositor_hide_window(tb->menu_popup->id);
    }
  }

  if (tb->window) compositor_invalidate(tb->window->id);
}

void taskbar_add_menu_entry(struct taskbar *tb, const char *label,
                            void (*action)(void *), void *user_data) {
  if (!tb || tb->menu_entry_count >= TASKBAR_MENU_MAX_ENTRIES) return;
  struct taskbar_menu_entry *e = &tb->menu_entries[tb->menu_entry_count++];
  tb_strcpy(e->label, label ? label : "", TASKBAR_ITEM_NAME_MAX);
  e->action = action;
  e->user_data = user_data;
  e->is_separator = 0;
}

void taskbar_add_menu_separator(struct taskbar *tb) {
  if (!tb || tb->menu_entry_count >= TASKBAR_MENU_MAX_ENTRIES) return;
  struct taskbar_menu_entry *e = &tb->menu_entries[tb->menu_entry_count++];
  e->label[0] = '\0';
  e->action = NULL;
  e->user_data = NULL;
  e->is_separator = 1;
}

int taskbar_handle_menu_click(struct taskbar *tb, int32_t screen_x,
                              int32_t screen_y) {
  if (!tb || !tb->menu_open || !tb->menu_popup) return 0;

  int32_t px = tb->menu_popup->frame.x;
  int32_t py = tb->menu_popup->frame.y;
  uint32_t pw = tb->menu_popup->frame.width;
  uint32_t ph = tb->menu_popup->frame.height;

  if (screen_x < px || screen_x >= px + (int32_t)pw ||
      screen_y < py || screen_y >= py + (int32_t)ph) {
    taskbar_toggle_menu(tb);
    return 0;
  }

  /* Walk entries with cumulative Y to find the clicked one */
  int32_t local_y = screen_y - py;
  int32_t ey = 2;
  for (uint32_t i = 0; i < tb->menu_entry_count; i++) {
    uint32_t row_h = tb->menu_entries[i].is_separator ? TASKBAR_MENU_SEP_HEIGHT
                                                      : TASKBAR_MENU_ENTRY_HEIGHT;
    if (local_y >= ey && local_y < ey + (int32_t)row_h) {
      if (tb->menu_entries[i].is_separator) return 1;
      taskbar_toggle_menu(tb);
      if (tb->menu_entries[i].action)
        tb->menu_entries[i].action(tb->menu_entries[i].user_data);
      return 1;
    }
    ey += (int32_t)row_h;
  }

  return 0;
}

/* Etapa UX W7-ish (2026-05-03): atualiza hover_entry segundo a posicao
 * de tela do mouse. Se o mouse estiver fora do popup, hover_entry = -1.
 * Sobre um separador, tambem -1. Convergencia com o paint: a linha
 * destacada e exatamente a sob o cursor. Invalida a janela so quando
 * o estado muda (evita repaint desnecessario). */
void taskbar_handle_menu_hover(struct taskbar *tb, int32_t screen_x,
                                int32_t screen_y) {
  if (!tb || !tb->menu_open || !tb->menu_popup) return;

  int32_t px = tb->menu_popup->frame.x;
  int32_t py = tb->menu_popup->frame.y;
  uint32_t pw = tb->menu_popup->frame.width;
  uint32_t ph = tb->menu_popup->frame.height;

  int new_hover = -1;
  if (screen_x >= px && screen_x < px + (int32_t)pw &&
      screen_y >= py && screen_y < py + (int32_t)ph) {
    int32_t local_y = screen_y - py;
    int32_t ey = 2;
    for (uint32_t i = 0; i < tb->menu_entry_count; i++) {
      uint32_t row_h = tb->menu_entries[i].is_separator
                           ? TASKBAR_MENU_SEP_HEIGHT
                           : TASKBAR_MENU_ENTRY_HEIGHT;
      if (local_y >= ey && local_y < ey + (int32_t)row_h) {
        if (!tb->menu_entries[i].is_separator) new_hover = (int)i;
        break;
      }
      ey += (int32_t)row_h;
    }
  }
  if (new_hover != tb->hover_entry) {
    tb->hover_entry = new_hover;
    if (tb->menu_popup) compositor_invalidate(tb->menu_popup->id);
  }
}
