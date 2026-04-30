#ifndef APPS_TASK_MANAGER_H
#define APPS_TASK_MANAGER_H

#include "gui/compositor.h"

/*
 * Task manager views.
 *
 * SERVICES is the legacy view (system services from service_manager).
 * TASKS lists kernel tasks via the public iterator (task_iter.h).
 * PROCESSES lists user processes via the process iterator (process_iter.h).
 *
 * The Tasks and Processes tabs are populated in M4 phase 0; today they
 * may be empty because no live kernel tasks/processes exist yet.
 */
enum task_manager_view {
  TASK_MANAGER_VIEW_SERVICES = 0,
  TASK_MANAGER_VIEW_TASKS = 1,
  TASK_MANAGER_VIEW_PROCESSES = 2,
};

struct task_manager_app {
  struct gui_window *window;
  int selected;
  int scroll_offset;
  uint64_t refresh_tick;
  enum task_manager_view view;
};

void task_manager_open(void);
void task_manager_refresh(struct task_manager_app *app);
void task_manager_paint(struct task_manager_app *app);
void task_manager_restart_selected(struct task_manager_app *app);
void task_manager_set_view(struct task_manager_app *app,
                           enum task_manager_view view);

#endif
