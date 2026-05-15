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
  char previous_path[FM_PATH_MAX];
  struct fm_entry entries[FM_MAX_ENTRIES];
  int entry_count;
  int selected;
  int scroll_offset;
  char status_text[96];
  uint32_t status_color;
  int drag_active;
  int drag_source;
  int drag_over;
  int drag_open_on_release;
  int drag_moved;
  int32_t drag_start_x;
  int32_t drag_start_y;
  int external_drag_over;
};

void file_manager_open(void);
/* Etapa UX W7-ish (2026-05-03): abre o file manager e navega para
 * `path` (cria a janela se nao existir). Usado pelo desktop icons
 * para "abrir folder" sem o caller precisar conhecer o singleton. */
void file_manager_open_at(const char *path);
void file_manager_navigate(struct file_manager_app *app, const char *path);
void file_manager_paint(struct file_manager_app *app);
void file_manager_handle_click(struct file_manager_app *app, int32_t x, int32_t y);
int file_manager_handle_drag_move(struct gui_window *win, int32_t screen_x,
                                  int32_t screen_y, uint8_t buttons);
int file_manager_handle_mouse_up(struct gui_window *win, int32_t screen_x,
                                 int32_t screen_y);
int file_manager_drop_path_at(int32_t screen_x, int32_t screen_y,
                              const char *src_path);
int file_manager_preview_drop_path_at(int32_t screen_x, int32_t screen_y,
                                      const char *src_path);
void file_manager_clear_external_drop(void);

#endif
