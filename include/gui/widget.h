#ifndef GUI_WIDGET_H
#define GUI_WIDGET_H

#include <stdint.h>
#include <stddef.h>
#include "gui/compositor.h"
#include "gui/event.h"

#define WIDGET_MAX_TEXT 256
#define WIDGET_MAX_CHILDREN 32

enum widget_type {
  WIDGET_NONE = 0,
  WIDGET_LABEL,
  WIDGET_BUTTON,
  WIDGET_TEXTBOX,
  WIDGET_CHECKBOX,
  WIDGET_LIST,
  WIDGET_PANEL,
  WIDGET_SCROLLBAR,
  WIDGET_MENUBAR,
  WIDGET_MENU_ITEM,
  WIDGET_DIALOG,
  WIDGET_PROGRESS,
  WIDGET_TABS
};

struct widget;

typedef void (*widget_callback)(struct widget *w, void *user_data);

struct widget_style {
  uint32_t bg_color;
  uint32_t fg_color;
  uint32_t border_color;
  uint32_t hover_color;
  uint32_t active_color;
  uint32_t text_color;
  uint8_t border_width;
  uint8_t padding;
  uint8_t margin;
  uint8_t font_size;
};

struct widget {
  uint32_t id;
  enum widget_type type;
  struct gui_rect bounds;
  struct widget_style style;
  char text[WIDGET_MAX_TEXT];
  int visible;
  int enabled;
  int focused;
  int hovered;
  int checked;
  int value;
  int min_value;
  int max_value;
  struct gui_window *window;
  struct widget *parent;
  struct widget *children[WIDGET_MAX_CHILDREN];
  uint32_t child_count;
  widget_callback on_click;
  widget_callback on_change;
  widget_callback on_submit;
  void *user_data;
};

void widget_system_init(void);
struct widget *widget_create(enum widget_type type, struct gui_window *win);
void widget_destroy(struct widget *w);
void widget_set_bounds(struct widget *w, int32_t x, int32_t y,
                       uint32_t width, uint32_t height);
void widget_set_text(struct widget *w, const char *text);
void widget_set_visible(struct widget *w, int visible);
void widget_set_enabled(struct widget *w, int enabled);
void widget_set_style(struct widget *w, const struct widget_style *style);
void widget_add_child(struct widget *parent, struct widget *child);
void widget_remove_child(struct widget *parent, struct widget *child);
void widget_paint(struct widget *w, struct gui_surface *surface);
int widget_handle_event(struct widget *w, const struct gui_event *ev);
struct widget *widget_find_at(struct widget *root, int32_t x, int32_t y);
void widget_focus(struct widget *w);
void widget_set_on_click(struct widget *w, widget_callback cb, void *data);
void widget_set_on_change(struct widget *w, widget_callback cb, void *data);

struct widget_style widget_default_style(void);
struct widget_style widget_button_style(void);
struct widget_style widget_textbox_style(void);

#endif /* GUI_WIDGET_H */
