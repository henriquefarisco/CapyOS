#ifndef APPS_TASK_MANAGER_H
#define APPS_TASK_MANAGER_H

#include "gui/compositor.h"

struct task_manager_app {
  struct gui_window *window;
  int selected;
  int scroll_offset;
  uint64_t refresh_tick;
};

void task_manager_open(void);
void task_manager_refresh(struct task_manager_app *app);
void task_manager_paint(struct task_manager_app *app);
void task_manager_restart_selected(struct task_manager_app *app);

#endif
