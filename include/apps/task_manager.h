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
  uint64_t snapshot_hash;
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
 * Manager window is open the tick advances `refresh_tick`, detects
 * snapshot changes, and invalidates the window so the next compositor
 * render re-paints from fresh `task_iter` / `process_iter` /
 * `service_manager` data. An unchanged view still refreshes every
 * `TASK_MANAGER_AUTO_REFRESH_FRAMES` frames.
 *
 * No-op when Task Manager is closed. Safe to call before any
 * window has ever been opened. */
void task_manager_tick(void);

/* Post-M5 W2: kill the currently selected row.
 *
 * Behaviour by view:
 *   - SERVICES: toggles Stop/Start through the service_manager.
 *   - TASKS: kills the selected task's owning process; kernel-only
 *     tasks are protected.
 *   - PROCESSES: invokes `process_kill(pid, 9)` on the selected row.
 * Quietly no-ops if the row is invalid or the kill would self-kill. */
void task_manager_kill_selected(struct task_manager_app *app);

/* Etapa 6 / Slice 6.6: headless smoke roundtrip (no GUI). Returns 0 when the
 * Task Manager's primary data path (task_iter / process_iter enumeration) runs
 * and terminates with sane, stable counts. The pre-login run queue may be empty
 * (0 tasks/processes), so success does NOT require any task to exist. Called by
 * the apps-basic-roundtrip orchestrator via the apps/apps_smoke.h contract. */
int task_manager_smoke_roundtrip(void);

#endif
