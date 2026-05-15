#ifndef GUI_DESKTOP_H
#define GUI_DESKTOP_H

#include <stdint.h>
#include "core/system_init.h"
#include "gui/compositor.h"
#include "gui/taskbar.h"
#include "gui/terminal.h"
#include "gui/font.h"
#include "gui/event.h"
#include "gui/window_manager.h"
#include "gui/window_dispatcher.h"
#include "drivers/input/mouse.h"

struct desktop_session {
  int active;
  uint32_t screen_w;
  uint32_t screen_h;
  uint32_t *framebuffer;
  uint32_t pitch;
  struct taskbar taskbar;
  struct window_manager wm;
  struct terminal *active_terminal;
  const struct system_settings *settings;
  uint32_t wallpaper_color;
  char theme_name[16];
  int mouse_initialized;
  int cursor_valid;
  int32_t cursor_x;
  int32_t cursor_y;
  struct gui_window_dispatcher_health dispatcher_health;
  uint64_t dispatcher_health_samples;
};

struct desktop_session_health {
  int active;
  int framebuffer_ready;
  int dimensions_ready;
  int mouse_initialized;
  int cursor_valid;
  int taskbar_ready;
  int overlay_active;
  int taskbar_menu_open;
  int window_manager_drag_active;
  uint32_t focused_window_id;
  uint64_t dispatcher_health_samples;
  int dispatcher_health_ready;
  struct gui_window_dispatcher_health dispatcher;
};

#define DESKTOP_SMOKE_BLOCK_INACTIVE          0x00000001u
#define DESKTOP_SMOKE_BLOCK_FRAMEBUFFER       0x00000002u
#define DESKTOP_SMOKE_BLOCK_DIMENSIONS        0x00000004u
#define DESKTOP_SMOKE_BLOCK_MOUSE             0x00000008u
#define DESKTOP_SMOKE_BLOCK_CURSOR            0x00000010u
#define DESKTOP_SMOKE_BLOCK_TASKBAR           0x00000020u
#define DESKTOP_SMOKE_BLOCK_DISPATCHER        0x00000040u
#define DESKTOP_SMOKE_BLOCK_DISPATCHER_ROUTES 0x00000080u
#define DESKTOP_SMOKE_BLOCK_QUEUE             0x00000100u
#define DESKTOP_SMOKE_BLOCK_OVERLAY           0x00000200u
#define DESKTOP_SMOKE_BLOCK_WINDOW_DRAG       0x00000400u

#define DESKTOP_SMOKE_BLOCK_KNOWN_MASK \
  (DESKTOP_SMOKE_BLOCK_INACTIVE | DESKTOP_SMOKE_BLOCK_FRAMEBUFFER | \
   DESKTOP_SMOKE_BLOCK_DIMENSIONS | DESKTOP_SMOKE_BLOCK_MOUSE | \
   DESKTOP_SMOKE_BLOCK_CURSOR | DESKTOP_SMOKE_BLOCK_TASKBAR | \
   DESKTOP_SMOKE_BLOCK_DISPATCHER | DESKTOP_SMOKE_BLOCK_DISPATCHER_ROUTES | \
   DESKTOP_SMOKE_BLOCK_QUEUE | DESKTOP_SMOKE_BLOCK_OVERLAY | \
   DESKTOP_SMOKE_BLOCK_WINDOW_DRAG)

#define DESKTOP_GUI_SESSION_SMOKE_GATE_VERSION 1
#define DESKTOP_GUI_SESSION_SMOKE_MARKER "[smoke] gui-session ready"
#define DESKTOP_MOUSE_EVENTS_SMOKE_GATE_VERSION 1
#define DESKTOP_MOUSE_EVENTS_SMOKE_MARKER "[smoke] mouse-events ready"
#define DESKTOP_GUI_SESSION_SMOKE_REQUIRED_BLOCKER_MASK \
  (DESKTOP_SMOKE_BLOCK_INACTIVE | DESKTOP_SMOKE_BLOCK_FRAMEBUFFER | \
   DESKTOP_SMOKE_BLOCK_DIMENSIONS | DESKTOP_SMOKE_BLOCK_TASKBAR | \
   DESKTOP_SMOKE_BLOCK_DISPATCHER | DESKTOP_SMOKE_BLOCK_QUEUE | \
   DESKTOP_SMOKE_BLOCK_OVERLAY | DESKTOP_SMOKE_BLOCK_WINDOW_DRAG)
#define DESKTOP_MOUSE_EVENTS_SMOKE_REQUIRED_BLOCKER_MASK \
  (DESKTOP_GUI_SESSION_SMOKE_REQUIRED_BLOCKER_MASK | \
   DESKTOP_SMOKE_BLOCK_MOUSE | DESKTOP_SMOKE_BLOCK_CURSOR | \
   DESKTOP_SMOKE_BLOCK_DISPATCHER_ROUTES)

#define DESKTOP_DISPATCHER_ROUTE_KEY_DOWN             0x00000001u
#define DESKTOP_DISPATCHER_ROUTE_KEY_UP               0x00000002u
#define DESKTOP_DISPATCHER_ROUTE_MOUSE_SCROLL         0x00000004u
#define DESKTOP_DISPATCHER_ROUTE_MOUSE_HOVER          0x00000008u
#define DESKTOP_DISPATCHER_ROUTE_MOUSE_LEFT_BUTTON    0x00000010u
#define DESKTOP_DISPATCHER_ROUTE_MOUSE_RIGHT_CONTEXT  0x00000020u
#define DESKTOP_DISPATCHER_ROUTE_MOUSE_CAPTURE_OPT_IN 0x00000040u
#define DESKTOP_DISPATCHER_ROUTE_QUEUE_MIRROR_FREE    0x00000080u
#define DESKTOP_DISPATCHER_ROUTE_OVERLAYS_DIRECT      0x00000100u
#define DESKTOP_DISPATCHER_ROUTE_WINDOW_MANAGER       0x00000200u
#define DESKTOP_DISPATCHER_ROUTE_TITLEBAR_DIRECT      0x00000400u
#define DESKTOP_DISPATCHER_ROUTE_TASKBAR_DIRECT       0x00000800u
#define DESKTOP_DISPATCHER_ROUTE_DESKTOP_ICONS_DIRECT 0x00001000u

#define DESKTOP_DISPATCHER_ROUTE_KNOWN_MASK \
  (DESKTOP_DISPATCHER_ROUTE_KEY_DOWN | DESKTOP_DISPATCHER_ROUTE_KEY_UP | \
   DESKTOP_DISPATCHER_ROUTE_MOUSE_SCROLL | \
   DESKTOP_DISPATCHER_ROUTE_MOUSE_HOVER | \
   DESKTOP_DISPATCHER_ROUTE_MOUSE_LEFT_BUTTON | \
   DESKTOP_DISPATCHER_ROUTE_MOUSE_RIGHT_CONTEXT | \
   DESKTOP_DISPATCHER_ROUTE_MOUSE_CAPTURE_OPT_IN | \
   DESKTOP_DISPATCHER_ROUTE_QUEUE_MIRROR_FREE | \
   DESKTOP_DISPATCHER_ROUTE_OVERLAYS_DIRECT | \
   DESKTOP_DISPATCHER_ROUTE_WINDOW_MANAGER | \
   DESKTOP_DISPATCHER_ROUTE_TITLEBAR_DIRECT | \
   DESKTOP_DISPATCHER_ROUTE_TASKBAR_DIRECT | \
   DESKTOP_DISPATCHER_ROUTE_DESKTOP_ICONS_DIRECT)

#define DESKTOP_GUI_SESSION_ROUTE_REQUIRED_MASK \
  (DESKTOP_DISPATCHER_ROUTE_KEY_DOWN | DESKTOP_DISPATCHER_ROUTE_KEY_UP | \
   DESKTOP_DISPATCHER_ROUTE_QUEUE_MIRROR_FREE | \
   DESKTOP_DISPATCHER_ROUTE_OVERLAYS_DIRECT | \
   DESKTOP_DISPATCHER_ROUTE_WINDOW_MANAGER | \
   DESKTOP_DISPATCHER_ROUTE_TITLEBAR_DIRECT | \
   DESKTOP_DISPATCHER_ROUTE_TASKBAR_DIRECT | \
   DESKTOP_DISPATCHER_ROUTE_DESKTOP_ICONS_DIRECT)

#define DESKTOP_MOUSE_EVENTS_ROUTE_REQUIRED_MASK \
  (DESKTOP_DISPATCHER_ROUTE_MOUSE_SCROLL | \
   DESKTOP_DISPATCHER_ROUTE_MOUSE_HOVER | \
   DESKTOP_DISPATCHER_ROUTE_MOUSE_LEFT_BUTTON | \
   DESKTOP_DISPATCHER_ROUTE_MOUSE_RIGHT_CONTEXT | \
   DESKTOP_DISPATCHER_ROUTE_MOUSE_CAPTURE_OPT_IN)

#define DESKTOP_MOUSE_EVENTS_SMOKE_ROUTE_REQUIRED_MASK \
  (DESKTOP_GUI_SESSION_ROUTE_REQUIRED_MASK | \
   DESKTOP_MOUSE_EVENTS_ROUTE_REQUIRED_MASK)

struct desktop_smoke_blocker_summary {
  uint32_t blocker_flags;
  uint32_t known_blocker_flags;
  uint32_t unknown_blocker_flags;
  uint32_t blocker_count;
  const char *first_blocker_name;
};

struct desktop_dispatcher_route_summary {
  uint32_t expected_route_flags;
  uint32_t ready_route_flags;
  uint32_t missing_route_flags;
  uint32_t missing_route_count;
  const char *first_missing_route_name;
};

struct desktop_session_smoke_readiness {
  struct desktop_session_health health;
  uint32_t blocker_flags;
  struct desktop_smoke_blocker_summary blocker_summary;
  struct desktop_dispatcher_route_summary route_summary;
  int snapshot_ready;
  int dispatcher_routes_ready;
  int queue_healthy;
  int no_modal_blockers;
  int no_window_drag;
  int gui_session_ready;
  int mouse_events_ready;
};

struct desktop_gui_session_smoke_gate {
  int version;
  int readiness_available;
  int snapshot_ready;
  int active;
  int framebuffer_ready;
  int dimensions_ready;
  int taskbar_ready;
  int dispatcher_health_ready;
  int queue_healthy;
  int no_modal_blockers;
  int no_window_drag;
  int smoke_ready;
  int mouse_events_deferred;
  uint32_t blocker_flags;
  uint32_t required_blocker_flags;
  uint32_t blocked_required_flags;
  uint32_t deferred_mouse_blocker_flags;
  uint32_t required_route_flags;
  uint32_t missing_required_route_flags;
  uint32_t deferred_mouse_route_flags;
  const char *first_blocker_name;
  const char *first_missing_required_route_name;
  const char *marker;
  const char *state;
  const char *blocked_reason;
};

struct desktop_mouse_events_smoke_gate {
  int version;
  int readiness_available;
  int snapshot_ready;
  int gui_session_ready;
  int mouse_events_ready;
  int active;
  int framebuffer_ready;
  int dimensions_ready;
  int mouse_initialized;
  int cursor_valid;
  int taskbar_ready;
  int dispatcher_health_ready;
  int queue_healthy;
  int no_modal_blockers;
  int no_window_drag;
  int smoke_ready;
  uint32_t blocker_flags;
  uint32_t required_blocker_flags;
  uint32_t blocked_required_flags;
  uint32_t required_route_flags;
  uint32_t missing_required_route_flags;
  const char *first_blocker_name;
  const char *first_missing_required_route_name;
  const char *marker;
  const char *state;
  const char *blocked_reason;
};

void desktop_init(struct desktop_session *ds, uint32_t *fb, uint32_t w,
                  uint32_t h, uint32_t pitch,
                  const struct system_settings *settings);
int desktop_run_frame(struct desktop_session *ds);
void desktop_open_terminal(struct desktop_session *ds);
void desktop_open_terminal_here(const char *path);
void desktop_handle_input(struct desktop_session *ds, uint32_t keycode, char ch);
int desktop_handle_mouse(struct desktop_session *ds);
int desktop_session_health_snapshot(const struct desktop_session *ds,
                                    struct desktop_session_health *out);
uint32_t desktop_smoke_block_known_mask(void);
const char *desktop_smoke_block_name(uint32_t blocker);
int desktop_smoke_blocker_summary(
    uint32_t blocker_flags,
    struct desktop_smoke_blocker_summary *out);
uint32_t desktop_dispatcher_route_known_mask(void);
const char *desktop_dispatcher_route_name(uint32_t route);
int desktop_dispatcher_route_summary(
    const struct gui_window_dispatcher_input_routes *routes,
    struct desktop_dispatcher_route_summary *out);
int desktop_session_smoke_readiness_from_health(
    const struct desktop_session_health *health,
    struct desktop_session_smoke_readiness *out);
const char *desktop_gui_session_smoke_marker(void);
int desktop_gui_session_smoke_gate_from_readiness(
    const struct desktop_session_smoke_readiness *readiness,
    struct desktop_gui_session_smoke_gate *out);
const char *desktop_mouse_events_smoke_marker(void);
int desktop_mouse_events_smoke_gate_from_readiness(
    const struct desktop_session_smoke_readiness *readiness,
    struct desktop_mouse_events_smoke_gate *out);
int desktop_session_smoke_readiness_snapshot(
    const struct desktop_session *ds,
    struct desktop_session_smoke_readiness *out);
void desktop_shutdown(struct desktop_session *ds);

#endif /* GUI_DESKTOP_H */
