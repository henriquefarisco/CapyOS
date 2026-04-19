#include "gui/widget.h"
#include "gui/font.h"
#include "util/kstring.h"
#include "memory/kmem.h"
#include <stddef.h>

static uint32_t next_widget_id = 1;

struct widget_style widget_default_style(void) {
  struct widget_style s;
  const struct gui_theme_palette *theme = compositor_theme();
  uint8_t scale = compositor_ui_scale();
  s.bg_color = theme->window_bg;
  s.fg_color = theme->text;
  s.border_color = theme->window_border;
  s.hover_color = theme->accent_alt;
  s.active_color = theme->accent;
  s.text_color = theme->text;
  s.border_width = 1;
  s.padding = (uint8_t)(4 * scale);
  s.margin = (uint8_t)(2 * scale);
  s.font_size = 16;
  return s;
}

struct widget_style widget_button_style(void) {
  struct widget_style s = widget_default_style();
  const struct gui_theme_palette *theme = compositor_theme();
  s.bg_color = theme->accent;
  s.text_color = theme->accent_text;
  s.hover_color = theme->accent_alt;
  s.active_color = theme->title_active;
  s.border_color = theme->window_border;
  s.padding = (uint8_t)(8 * compositor_ui_scale());
  return s;
}

struct widget_style widget_textbox_style(void) {
  struct widget_style s = widget_default_style();
  s.bg_color = compositor_theme()->terminal_bg;
  s.border_color = compositor_theme()->window_border;
  s.text_color = compositor_theme()->text;
  return s;
}

void widget_system_init(void) { next_widget_id = 1; }

struct widget *widget_create(enum widget_type type, struct gui_window *win) {
  struct widget *w = (struct widget *)kmalloc(sizeof(struct widget));
  if (!w) return NULL;
  kmemzero(w, sizeof(*w));
  w->id = next_widget_id++;
  w->type = type;
  w->visible = 1;
  w->enabled = 1;
  w->window = win;
  switch (type) {
    case WIDGET_BUTTON: w->style = widget_button_style(); break;
    case WIDGET_TEXTBOX: w->style = widget_textbox_style(); break;
    default: w->style = widget_default_style(); break;
  }
  return w;
}

void widget_destroy(struct widget *w) {
  if (!w) return;
  for (uint32_t i = 0; i < w->child_count; i++) widget_destroy(w->children[i]);
  kfree(w);
}

void widget_set_bounds(struct widget *w, int32_t x, int32_t y,
                       uint32_t width, uint32_t height) {
  if (!w) return;
  w->bounds.x = x; w->bounds.y = y; w->bounds.width = width; w->bounds.height = height;
}

void widget_set_text(struct widget *w, const char *text) {
  if (!w || !text) return;
  kstrcpy(w->text, WIDGET_MAX_TEXT, text);
}

void widget_set_visible(struct widget *w, int visible) { if (w) w->visible = visible; }
void widget_set_enabled(struct widget *w, int enabled) { if (w) w->enabled = enabled; }

void widget_set_style(struct widget *w, const struct widget_style *style) {
  if (w && style) w->style = *style;
}

void widget_add_child(struct widget *parent, struct widget *child) {
  if (!parent || !child || parent->child_count >= WIDGET_MAX_CHILDREN) return;
  child->parent = parent;
  parent->children[parent->child_count++] = child;
}

void widget_remove_child(struct widget *parent, struct widget *child) {
  if (!parent || !child) return;
  for (uint32_t i = 0; i < parent->child_count; i++) {
    if (parent->children[i] == child) {
      child->parent = NULL;
      for (uint32_t j = i; j < parent->child_count - 1; j++)
        parent->children[j] = parent->children[j + 1];
      parent->child_count--;
      return;
    }
  }
}

static void fill_rect(struct gui_surface *s, int32_t x, int32_t y,
                       uint32_t w, uint32_t h, uint32_t color) {
  for (uint32_t row = 0; row < h; row++) {
    int32_t py = y + (int32_t)row;
    if (py < 0 || (uint32_t)py >= s->height) continue;
    uint32_t *line = (uint32_t *)((uint8_t *)s->pixels + py * s->pitch);
    for (uint32_t col = 0; col < w; col++) {
      int32_t px = x + (int32_t)col;
      if (px < 0 || (uint32_t)px >= s->width) continue;
      line[px] = color;
    }
  }
}

static void draw_border(struct gui_surface *s, int32_t x, int32_t y,
                         uint32_t w, uint32_t h, uint32_t color, uint8_t width) {
  fill_rect(s, x, y, w, width, color);
  fill_rect(s, x, y + (int32_t)h - width, w, width, color);
  fill_rect(s, x, y, width, h, color);
  fill_rect(s, x + (int32_t)w - width, y, width, h, color);
}

void widget_paint(struct widget *w, struct gui_surface *surface) {
  if (!w || !surface || !w->visible) return;

  int32_t x = w->bounds.x;
  int32_t y = w->bounds.y;
  uint32_t bw = w->bounds.width;
  uint32_t bh = w->bounds.height;

  uint32_t bg = w->hovered ? w->style.hover_color : w->style.bg_color;
  if (!w->enabled) bg = 0xC0C0C0;

  fill_rect(surface, x, y, bw, bh, bg);
  if (w->style.border_width > 0)
    draw_border(surface, x, y, bw, bh, w->style.border_color, w->style.border_width);

  const struct font *f = font_default();
  if (f && w->text[0]) {
    int32_t tx = x + w->style.padding + w->style.border_width;
    int32_t ty = y + (int32_t)(bh / 2) - (int32_t)(f->glyph_height / 2);
    if (w->type == WIDGET_BUTTON) {
      uint32_t tw = font_string_width(f, w->text);
      if (tw < bw) {
        tx = x + (int32_t)((bw - tw) / 2);
      } else {
        tx = x + 2;
      }
    }
    font_draw_string(surface, f, tx, ty, w->text, w->style.text_color);
  }

  if (w->type == WIDGET_CHECKBOX) {
    int32_t cx = x + (int32_t)bw - 20;
    int32_t cy = y + (int32_t)(bh / 2) - 6;
    fill_rect(surface, cx, cy, 12, 12, compositor_theme()->window_bg);
    draw_border(surface, cx, cy, 12, 12, compositor_theme()->window_border, 1);
    if (w->checked) fill_rect(surface, cx + 3, cy + 3, 6, 6, compositor_theme()->accent);
  }

  if (w->type == WIDGET_PROGRESS) {
    int32_t bx = x + w->style.padding;
    int32_t by = y + (int32_t)bh - 12;
    uint32_t bar_w = bw - 2 * w->style.padding;
    fill_rect(surface, bx, by, bar_w, 8, compositor_theme()->accent_alt);
    uint32_t fill_w = 0;
    if (w->max_value > w->min_value)
      fill_w = (uint32_t)((int64_t)(w->value - w->min_value) * (int64_t)bar_w /
                           (w->max_value - w->min_value));
    if (fill_w > bar_w) fill_w = bar_w;
    fill_rect(surface, bx, by, fill_w, 8, compositor_theme()->accent);
  }

  for (uint32_t i = 0; i < w->child_count; i++) widget_paint(w->children[i], surface);
}

static int point_in_rect(int32_t px, int32_t py, const struct gui_rect *r) {
  return px >= r->x && px < r->x + (int32_t)r->width &&
         py >= r->y && py < r->y + (int32_t)r->height;
}

int widget_handle_event(struct widget *w, const struct gui_event *ev) {
  if (!w || !ev || !w->visible || !w->enabled) return 0;

  if (ev->type == GUI_EVENT_MOUSE_MOVE) {
    int hover = point_in_rect(ev->mouse.x, ev->mouse.y, &w->bounds);
    w->hovered = hover;
    /* Propagate hover tracking to children */
    for (uint32_t i = 0; i < w->child_count; i++)
      widget_handle_event(w->children[i], ev);
    return 0;
  }

  /* Dispatch to children first so that items rendered on top of their
   * parent (e.g. MENU_ITEM inside a MENUBAR) get the event before the
   * parent consumes it.  This is the standard front-to-back hit-test
   * order expected by any widget tree. */
  for (uint32_t i = 0; i < w->child_count; i++) {
    if (widget_handle_event(w->children[i], ev)) return 1;
  }

  if (ev->type == GUI_EVENT_MOUSE_DOWN && (ev->mouse.buttons & 1)) {
    if (point_in_rect(ev->mouse.x, ev->mouse.y, &w->bounds)) {
      if ((w->type == WIDGET_BUTTON || w->type == WIDGET_MENUBAR ||
           w->type == WIDGET_MENU_ITEM) && w->on_click)
        w->on_click(w, w->user_data);
      if (w->type == WIDGET_CHECKBOX) {
        w->checked = !w->checked;
        if (w->on_change) w->on_change(w, w->user_data);
      }
      return 1;
    }
  }

  return 0;
}

struct widget *widget_find_at(struct widget *root, int32_t x, int32_t y) {
  if (!root || !root->visible) return NULL;
  for (int i = (int)root->child_count - 1; i >= 0; i--) {
    struct widget *found = widget_find_at(root->children[i], x, y);
    if (found) return found;
  }
  if (point_in_rect(x, y, &root->bounds)) return root;
  return NULL;
}

void widget_focus(struct widget *w) { if (w) w->focused = 1; }

void widget_set_on_click(struct widget *w, widget_callback cb, void *data) {
  if (w) { w->on_click = cb; w->user_data = data; }
}

void widget_set_on_change(struct widget *w, widget_callback cb, void *data) {
  if (w) { w->on_change = cb; w->user_data = data; }
}
