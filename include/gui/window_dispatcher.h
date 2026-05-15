#ifndef GUI_WINDOW_DISPATCHER_H
#define GUI_WINDOW_DISPATCHER_H

#include <stdint.h>

#include "gui/event.h"

struct gui_window_dispatcher_stats {
  uint64_t dispatched_total;
  uint64_t handled_total;
  uint64_t missing_target_total;
  uint64_t missing_handler_total;
  uint64_t ignored_total;
};

struct gui_window_dispatcher_snapshot {
  struct gui_window_dispatcher_stats stats;
  uint32_t captured_mouse_window_id;
  int mouse_capture_active;
};

struct gui_window_dispatcher_input_routes {
  int key_down;
  int key_up;
  int mouse_scroll;
  int mouse_hover;
  int mouse_left_button;
  int mouse_right_context;
  int mouse_capture_opt_in;
  int queue_mirror_free;
  int overlays_direct;
  int window_manager_direct;
  int titlebar_direct;
  int taskbar_direct;
  int desktop_icons_direct;
};

struct gui_window_dispatcher_health {
  struct gui_window_dispatcher_snapshot dispatcher;
  struct gui_window_dispatcher_input_routes routes;
  struct gui_event_snapshot queue;
  int queue_snapshot_available;
  int backlog_warning;
  int drop_warning;
  int stale_capture_warning;
};

void gui_window_dispatcher_reset(void);
void gui_window_dispatcher_reset_stats(void);
void gui_window_dispatcher_stats(struct gui_window_dispatcher_stats *out);
int gui_window_dispatcher_snapshot(struct gui_window_dispatcher_snapshot *out);
int gui_window_dispatcher_health_snapshot(struct gui_window_dispatcher_health *out);
int gui_window_dispatch_event(const struct gui_event *ev);
uint32_t gui_window_dispatch(uint32_t max_events);

#endif /* GUI_WINDOW_DISPATCHER_H */
