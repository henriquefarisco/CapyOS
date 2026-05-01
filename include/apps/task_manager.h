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

/* Post-M5 W2: per-frame tick. Must be called once per
 * `desktop_run_frame` (the desktop's main loop). When the Task
 * Manager window is open the tick advances `refresh_tick`; every
 * `TASK_MANAGER_AUTO_REFRESH_FRAMES` frames it invalidates the
 * window so the next compositor render re-paints from a fresh
 * `task_iter` / `process_iter` / `service_manager` snapshot. This
 * lifts the previous limitation where apps started after Task
 * Manager opened (or services restarted) only appeared after a
 * manual click on Refresh / scroll / tab change.
 *
 * No-op when Task Manager is closed. Safe to call before any
 * window has ever been opened. */
void task_manager_tick(void);

/* Post-M5 W2: kill the currently selected row.
 *
 * Behaviour by view:
 *   - SERVICES: same as Restart (services are not "killed").
 *   - TASKS / PROCESSES: invokes `process_kill_pid(pid)` on the
 *     selected row's pid. Quietly no-ops if the row is invalid or
 *     the kill fails (e.g. self-kill). */
void task_manager_kill_selected(struct task_manager_app *app);

#endif
