#ifndef GUI_TASKBAR_H
#define GUI_TASKBAR_H

#include <stdint.h>
#include "gui/compositor.h"

#define TASKBAR_HEIGHT 32
#define TASKBAR_MAX_ITEMS 16
#define TASKBAR_ITEM_NAME_MAX 24
#define TASKBAR_MENU_MAX_ENTRIES 8
#define TASKBAR_MENU_ENTRY_HEIGHT 28
#define TASKBAR_MENU_WIDTH 160

enum taskbar_position {
  TASKBAR_BOTTOM = 0,
  TASKBAR_TOP
};

struct taskbar_item {
  uint32_t window_id;
  char name[TASKBAR_ITEM_NAME_MAX];
  int active;
  int focused;
};

struct taskbar_menu_entry {
  char label[TASKBAR_ITEM_NAME_MAX];
  void (*action)(void *user_data);
  void *user_data;
};

struct taskbar {
  struct gui_window *window;
  struct gui_window *menu_popup;
  enum taskbar_position position;
  uint32_t bg_color;
  uint32_t fg_color;
  uint32_t highlight_color;
  struct taskbar_item items[TASKBAR_MAX_ITEMS];
  uint32_t item_count;
  int menu_open;
  struct taskbar_menu_entry menu_entries[TASKBAR_MENU_MAX_ENTRIES];
  uint32_t menu_entry_count;
  char clock_text[16];
  int show_clock;
};

void taskbar_init(struct taskbar *tb, uint32_t screen_w, uint32_t screen_h);
void taskbar_add_window(struct taskbar *tb, uint32_t window_id, const char *name);
void taskbar_remove_window(struct taskbar *tb, uint32_t window_id);
void taskbar_set_focused(struct taskbar *tb, uint32_t window_id);
int taskbar_update_clock(struct taskbar *tb, const char *time_str);
void taskbar_paint(struct taskbar *tb);
void taskbar_handle_click(struct taskbar *tb, int32_t x, int32_t y);
void taskbar_toggle_menu(struct taskbar *tb);
void taskbar_add_menu_entry(struct taskbar *tb, const char *label,
                            void (*action)(void *), void *user_data);
int taskbar_handle_menu_click(struct taskbar *tb, int32_t screen_x,
                              int32_t screen_y);

#endif /* GUI_TASKBAR_H */
