#ifndef APPS_FILE_MANAGER_H
#define APPS_FILE_MANAGER_H

#include "gui/compositor.h"

#define FM_MAX_ENTRIES 128
#define FM_PATH_MAX 256

struct fm_entry {
  char name[64];
  uint16_t mode;
  uint32_t size;
};

struct file_manager_app {
  struct gui_window *window;
  char current_path[FM_PATH_MAX];
  struct fm_entry entries[FM_MAX_ENTRIES];
  int entry_count;
  int selected;
  int scroll_offset;
  char status_text[96];
  uint32_t status_color;
};

void file_manager_open(void);
void file_manager_navigate(struct file_manager_app *app, const char *path);
void file_manager_paint(struct file_manager_app *app);
void file_manager_handle_click(struct file_manager_app *app, int32_t x, int32_t y);

#endif
