#include "gui/desktop.h"
#include "util/kstring.h"

uint32_t desktop_smoke_block_known_mask(void) {
  return DESKTOP_SMOKE_BLOCK_KNOWN_MASK;
}

const char *desktop_smoke_block_name(uint32_t blocker) {
  switch (blocker) {
    case DESKTOP_SMOKE_BLOCK_INACTIVE: return "inactive";
    case DESKTOP_SMOKE_BLOCK_FRAMEBUFFER: return "framebuffer";
    case DESKTOP_SMOKE_BLOCK_DIMENSIONS: return "dimensions";
    case DESKTOP_SMOKE_BLOCK_MOUSE: return "mouse";
    case DESKTOP_SMOKE_BLOCK_CURSOR: return "cursor";
    case DESKTOP_SMOKE_BLOCK_TASKBAR: return "taskbar";
    case DESKTOP_SMOKE_BLOCK_DISPATCHER: return "dispatcher";
    case DESKTOP_SMOKE_BLOCK_DISPATCHER_ROUTES: return "dispatcher-routes";
    case DESKTOP_SMOKE_BLOCK_QUEUE: return "queue";
    case DESKTOP_SMOKE_BLOCK_OVERLAY: return "overlay";
    case DESKTOP_SMOKE_BLOCK_WINDOW_DRAG: return "window-drag";
    default: return "unknown";
  }
}

int desktop_smoke_blocker_summary(
    uint32_t blocker_flags,
    struct desktop_smoke_blocker_summary *out) {
  static const uint32_t known_order[] = {
      DESKTOP_SMOKE_BLOCK_INACTIVE,
      DESKTOP_SMOKE_BLOCK_FRAMEBUFFER,
      DESKTOP_SMOKE_BLOCK_DIMENSIONS,
      DESKTOP_SMOKE_BLOCK_MOUSE,
      DESKTOP_SMOKE_BLOCK_CURSOR,
      DESKTOP_SMOKE_BLOCK_TASKBAR,
      DESKTOP_SMOKE_BLOCK_DISPATCHER,
      DESKTOP_SMOKE_BLOCK_DISPATCHER_ROUTES,
      DESKTOP_SMOKE_BLOCK_QUEUE,
      DESKTOP_SMOKE_BLOCK_OVERLAY,
      DESKTOP_SMOKE_BLOCK_WINDOW_DRAG,
  };
  uint32_t i = 0;
  uint32_t count = 0;
  const char *first = "none";
  if (!out) return 0;
  kmemzero(out, sizeof(*out));
  out->blocker_flags = blocker_flags;
  out->known_blocker_flags = blocker_flags & DESKTOP_SMOKE_BLOCK_KNOWN_MASK;
  out->unknown_blocker_flags = blocker_flags & ~DESKTOP_SMOKE_BLOCK_KNOWN_MASK;
  for (i = 0; i < (uint32_t)(sizeof(known_order) / sizeof(known_order[0])); i++) {
    if (out->known_blocker_flags & known_order[i]) {
      if (count == 0u) first = desktop_smoke_block_name(known_order[i]);
      count++;
    }
  }
  if (out->unknown_blocker_flags != 0u) {
    if (count == 0u) first = "unknown";
    count++;
  }
  out->blocker_count = count;
  out->first_blocker_name = first;
  return 1;
}

uint32_t desktop_dispatcher_route_known_mask(void) {
  return DESKTOP_DISPATCHER_ROUTE_KNOWN_MASK;
}

const char *desktop_dispatcher_route_name(uint32_t route) {
  switch (route) {
    case DESKTOP_DISPATCHER_ROUTE_KEY_DOWN: return "key-down";
    case DESKTOP_DISPATCHER_ROUTE_KEY_UP: return "key-up";
    case DESKTOP_DISPATCHER_ROUTE_MOUSE_SCROLL: return "mouse-scroll";
    case DESKTOP_DISPATCHER_ROUTE_MOUSE_HOVER: return "mouse-hover";
    case DESKTOP_DISPATCHER_ROUTE_MOUSE_LEFT_BUTTON: return "mouse-left-button";
    case DESKTOP_DISPATCHER_ROUTE_MOUSE_RIGHT_CONTEXT: return "mouse-right-context";
    case DESKTOP_DISPATCHER_ROUTE_MOUSE_CAPTURE_OPT_IN: return "mouse-capture-opt-in";
    case DESKTOP_DISPATCHER_ROUTE_QUEUE_MIRROR_FREE: return "queue-mirror-free";
    case DESKTOP_DISPATCHER_ROUTE_OVERLAYS_DIRECT: return "overlays-direct";
    case DESKTOP_DISPATCHER_ROUTE_WINDOW_MANAGER: return "window-manager";
    case DESKTOP_DISPATCHER_ROUTE_TITLEBAR_DIRECT: return "titlebar-direct";
    case DESKTOP_DISPATCHER_ROUTE_TASKBAR_DIRECT: return "taskbar-direct";
    case DESKTOP_DISPATCHER_ROUTE_DESKTOP_ICONS_DIRECT: return "desktop-icons-direct";
    default: return "unknown";
  }
}

static uint32_t desktop_dispatcher_route_flags(
    const struct gui_window_dispatcher_input_routes *routes) {
  uint32_t flags = 0u;
  if (!routes) return 0u;
  if (routes->key_down) flags |= DESKTOP_DISPATCHER_ROUTE_KEY_DOWN;
  if (routes->key_up) flags |= DESKTOP_DISPATCHER_ROUTE_KEY_UP;
  if (routes->mouse_scroll) flags |= DESKTOP_DISPATCHER_ROUTE_MOUSE_SCROLL;
  if (routes->mouse_hover) flags |= DESKTOP_DISPATCHER_ROUTE_MOUSE_HOVER;
  if (routes->mouse_left_button) flags |= DESKTOP_DISPATCHER_ROUTE_MOUSE_LEFT_BUTTON;
  if (routes->mouse_right_context) flags |= DESKTOP_DISPATCHER_ROUTE_MOUSE_RIGHT_CONTEXT;
  if (routes->mouse_capture_opt_in) flags |= DESKTOP_DISPATCHER_ROUTE_MOUSE_CAPTURE_OPT_IN;
  if (routes->queue_mirror_free) flags |= DESKTOP_DISPATCHER_ROUTE_QUEUE_MIRROR_FREE;
  if (routes->overlays_direct) flags |= DESKTOP_DISPATCHER_ROUTE_OVERLAYS_DIRECT;
  if (routes->window_manager_direct) flags |= DESKTOP_DISPATCHER_ROUTE_WINDOW_MANAGER;
  if (routes->titlebar_direct) flags |= DESKTOP_DISPATCHER_ROUTE_TITLEBAR_DIRECT;
  if (routes->taskbar_direct) flags |= DESKTOP_DISPATCHER_ROUTE_TASKBAR_DIRECT;
  if (routes->desktop_icons_direct) flags |= DESKTOP_DISPATCHER_ROUTE_DESKTOP_ICONS_DIRECT;
  return flags;
}

int desktop_dispatcher_route_summary(
    const struct gui_window_dispatcher_input_routes *routes,
    struct desktop_dispatcher_route_summary *out) {
  static const uint32_t known_order[] = {
      DESKTOP_DISPATCHER_ROUTE_KEY_DOWN,
      DESKTOP_DISPATCHER_ROUTE_KEY_UP,
      DESKTOP_DISPATCHER_ROUTE_MOUSE_SCROLL,
      DESKTOP_DISPATCHER_ROUTE_MOUSE_HOVER,
      DESKTOP_DISPATCHER_ROUTE_MOUSE_LEFT_BUTTON,
      DESKTOP_DISPATCHER_ROUTE_MOUSE_RIGHT_CONTEXT,
      DESKTOP_DISPATCHER_ROUTE_MOUSE_CAPTURE_OPT_IN,
      DESKTOP_DISPATCHER_ROUTE_QUEUE_MIRROR_FREE,
      DESKTOP_DISPATCHER_ROUTE_OVERLAYS_DIRECT,
      DESKTOP_DISPATCHER_ROUTE_WINDOW_MANAGER,
      DESKTOP_DISPATCHER_ROUTE_TITLEBAR_DIRECT,
      DESKTOP_DISPATCHER_ROUTE_TASKBAR_DIRECT,
      DESKTOP_DISPATCHER_ROUTE_DESKTOP_ICONS_DIRECT,
  };
  uint32_t i = 0;
  uint32_t count = 0;
  const char *first = "none";
  if (!out) return 0;
  kmemzero(out, sizeof(*out));
  out->expected_route_flags = DESKTOP_DISPATCHER_ROUTE_KNOWN_MASK;
  out->ready_route_flags = desktop_dispatcher_route_flags(routes);
  out->missing_route_flags = out->expected_route_flags & ~out->ready_route_flags;
  for (i = 0; i < (uint32_t)(sizeof(known_order) / sizeof(known_order[0])); i++) {
    if (out->missing_route_flags & known_order[i]) {
      if (count == 0u) first = desktop_dispatcher_route_name(known_order[i]);
      count++;
    }
  }
  out->missing_route_count = count;
  out->first_missing_route_name = first;
  return routes ? 1 : 0;
}

static const char *desktop_first_route_name_from_mask(uint32_t flags) {
  static const uint32_t known_order[] = {
      DESKTOP_DISPATCHER_ROUTE_KEY_DOWN,
      DESKTOP_DISPATCHER_ROUTE_KEY_UP,
      DESKTOP_DISPATCHER_ROUTE_MOUSE_SCROLL,
      DESKTOP_DISPATCHER_ROUTE_MOUSE_HOVER,
      DESKTOP_DISPATCHER_ROUTE_MOUSE_LEFT_BUTTON,
      DESKTOP_DISPATCHER_ROUTE_MOUSE_RIGHT_CONTEXT,
      DESKTOP_DISPATCHER_ROUTE_MOUSE_CAPTURE_OPT_IN,
      DESKTOP_DISPATCHER_ROUTE_QUEUE_MIRROR_FREE,
      DESKTOP_DISPATCHER_ROUTE_OVERLAYS_DIRECT,
      DESKTOP_DISPATCHER_ROUTE_WINDOW_MANAGER,
      DESKTOP_DISPATCHER_ROUTE_TITLEBAR_DIRECT,
      DESKTOP_DISPATCHER_ROUTE_TASKBAR_DIRECT,
      DESKTOP_DISPATCHER_ROUTE_DESKTOP_ICONS_DIRECT,
  };
  uint32_t i = 0;
  for (i = 0; i < (uint32_t)(sizeof(known_order) / sizeof(known_order[0])); i++) {
    if (flags & known_order[i]) {
      return desktop_dispatcher_route_name(known_order[i]);
    }
  }
  return "none";
}

int desktop_session_smoke_readiness_from_health(
    const struct desktop_session_health *health,
    struct desktop_session_smoke_readiness *out) {
  uint32_t blockers = 0u;
  if (!out) return 0;
  kmemzero(out, sizeof(*out));
  if (!health) return 0;
  out->health = *health;
  out->snapshot_ready = 1;
  (void)desktop_dispatcher_route_summary(&out->health.dispatcher.routes,
                                          &out->route_summary);
  out->dispatcher_routes_ready =
      (out->route_summary.missing_route_flags == 0u) ? 1 : 0;
  out->queue_healthy =
      (out->health.dispatcher.queue_snapshot_available &&
       !out->health.dispatcher.backlog_warning &&
       !out->health.dispatcher.drop_warning &&
       !out->health.dispatcher.stale_capture_warning) ? 1 : 0;
  out->no_modal_blockers = out->health.overlay_active ? 0 : 1;
  out->no_window_drag = out->health.window_manager_drag_active ? 0 : 1;
  if (!out->health.active) blockers |= DESKTOP_SMOKE_BLOCK_INACTIVE;
  if (!out->health.framebuffer_ready) blockers |= DESKTOP_SMOKE_BLOCK_FRAMEBUFFER;
  if (!out->health.dimensions_ready) blockers |= DESKTOP_SMOKE_BLOCK_DIMENSIONS;
  if (!out->health.mouse_initialized) blockers |= DESKTOP_SMOKE_BLOCK_MOUSE;
  if (!out->health.cursor_valid) blockers |= DESKTOP_SMOKE_BLOCK_CURSOR;
  if (!out->health.taskbar_ready) blockers |= DESKTOP_SMOKE_BLOCK_TASKBAR;
  if (!out->health.dispatcher_health_ready) blockers |= DESKTOP_SMOKE_BLOCK_DISPATCHER;
  if (!out->dispatcher_routes_ready) blockers |= DESKTOP_SMOKE_BLOCK_DISPATCHER_ROUTES;
  if (!out->queue_healthy) blockers |= DESKTOP_SMOKE_BLOCK_QUEUE;
  if (!out->no_modal_blockers) blockers |= DESKTOP_SMOKE_BLOCK_OVERLAY;
  if (!out->no_window_drag) blockers |= DESKTOP_SMOKE_BLOCK_WINDOW_DRAG;
  out->blocker_flags = blockers;
  (void)desktop_smoke_blocker_summary(blockers, &out->blocker_summary);
  out->gui_session_ready =
      ((blockers & DESKTOP_GUI_SESSION_SMOKE_REQUIRED_BLOCKER_MASK) == 0u &&
       (out->route_summary.missing_route_flags &
        DESKTOP_GUI_SESSION_ROUTE_REQUIRED_MASK) == 0u) ? 1 : 0;
  out->mouse_events_ready =
      (out->gui_session_ready && out->health.mouse_initialized &&
       out->health.cursor_valid &&
       (out->route_summary.missing_route_flags &
        DESKTOP_MOUSE_EVENTS_ROUTE_REQUIRED_MASK) == 0u) ? 1 : 0;
  return 1;
}

const char *desktop_gui_session_smoke_marker(void) {
  return DESKTOP_GUI_SESSION_SMOKE_MARKER;
}

const char *desktop_mouse_events_smoke_marker(void) {
  return DESKTOP_MOUSE_EVENTS_SMOKE_MARKER;
}

int desktop_gui_session_smoke_gate_from_readiness(
    const struct desktop_session_smoke_readiness *readiness,
    struct desktop_gui_session_smoke_gate *out) {
  struct desktop_smoke_blocker_summary blocked_summary;
  uint32_t missing_routes = 0u;
  if (!out) return 0;
  kmemzero(out, sizeof(*out));
  out->version = DESKTOP_GUI_SESSION_SMOKE_GATE_VERSION;
  out->required_blocker_flags = DESKTOP_GUI_SESSION_SMOKE_REQUIRED_BLOCKER_MASK;
  out->required_route_flags = DESKTOP_GUI_SESSION_ROUTE_REQUIRED_MASK;
  out->first_blocker_name = "none";
  out->first_missing_required_route_name = "none";
  out->marker = DESKTOP_GUI_SESSION_SMOKE_MARKER;
  out->state = "blocked";
  out->blocked_reason = "readiness-unavailable";
  if (!readiness) return 0;

  out->readiness_available = 1;
  out->snapshot_ready = readiness->snapshot_ready ? 1 : 0;
  out->active = readiness->health.active ? 1 : 0;
  out->framebuffer_ready = readiness->health.framebuffer_ready ? 1 : 0;
  out->dimensions_ready = readiness->health.dimensions_ready ? 1 : 0;
  out->taskbar_ready = readiness->health.taskbar_ready ? 1 : 0;
  out->dispatcher_health_ready = readiness->health.dispatcher_health_ready ? 1 : 0;
  out->queue_healthy = readiness->queue_healthy ? 1 : 0;
  out->no_modal_blockers = readiness->no_modal_blockers ? 1 : 0;
  out->no_window_drag = readiness->no_window_drag ? 1 : 0;
  out->blocker_flags = readiness->blocker_flags;
  missing_routes = readiness->route_summary.missing_route_flags;
  out->missing_required_route_flags =
      missing_routes & DESKTOP_GUI_SESSION_ROUTE_REQUIRED_MASK;
  out->deferred_mouse_route_flags =
      missing_routes & DESKTOP_MOUSE_EVENTS_ROUTE_REQUIRED_MASK;
  out->blocked_required_flags =
      (readiness->blocker_flags & DESKTOP_GUI_SESSION_SMOKE_REQUIRED_BLOCKER_MASK) |
      (readiness->blocker_flags & ~DESKTOP_SMOKE_BLOCK_KNOWN_MASK);
  if (out->missing_required_route_flags != 0u) {
    out->blocked_required_flags |= DESKTOP_SMOKE_BLOCK_DISPATCHER_ROUTES;
  }
  out->deferred_mouse_blocker_flags =
      readiness->blocker_flags & (DESKTOP_SMOKE_BLOCK_MOUSE |
                                  DESKTOP_SMOKE_BLOCK_CURSOR);
  if ((readiness->blocker_flags & DESKTOP_SMOKE_BLOCK_DISPATCHER_ROUTES) &&
      out->missing_required_route_flags == 0u) {
    out->deferred_mouse_blocker_flags |= DESKTOP_SMOKE_BLOCK_DISPATCHER_ROUTES;
  }
  out->mouse_events_deferred =
      (out->deferred_mouse_blocker_flags != 0u ||
       out->deferred_mouse_route_flags != 0u) ? 1 : 0;
  (void)desktop_smoke_blocker_summary(out->blocked_required_flags,
                                      &blocked_summary);
  out->first_blocker_name = blocked_summary.first_blocker_name;
  out->first_missing_required_route_name =
      desktop_first_route_name_from_mask(out->missing_required_route_flags);

  if (!out->snapshot_ready) {
    out->blocked_reason = "snapshot-unavailable";
    return 1;
  }
  if (out->blocked_required_flags != 0u) {
    out->blocked_reason = out->first_blocker_name;
    return 1;
  }
  out->smoke_ready = 1;
  out->state = "gui-session-ready";
  out->blocked_reason = "ready";
  return 1;
}

int desktop_mouse_events_smoke_gate_from_readiness(
    const struct desktop_session_smoke_readiness *readiness,
    struct desktop_mouse_events_smoke_gate *out) {
  struct desktop_smoke_blocker_summary blocked_summary;
  uint32_t missing_routes = 0u;
  if (!out) return 0;
  kmemzero(out, sizeof(*out));
  out->version = DESKTOP_MOUSE_EVENTS_SMOKE_GATE_VERSION;
  out->required_blocker_flags =
      DESKTOP_MOUSE_EVENTS_SMOKE_REQUIRED_BLOCKER_MASK;
  out->required_route_flags = DESKTOP_MOUSE_EVENTS_SMOKE_ROUTE_REQUIRED_MASK;
  out->first_blocker_name = "none";
  out->first_missing_required_route_name = "none";
  out->marker = DESKTOP_MOUSE_EVENTS_SMOKE_MARKER;
  out->state = "blocked";
  out->blocked_reason = "readiness-unavailable";
  if (!readiness) return 0;

  out->readiness_available = 1;
  out->snapshot_ready = readiness->snapshot_ready ? 1 : 0;
  out->gui_session_ready = readiness->gui_session_ready ? 1 : 0;
  out->mouse_events_ready = readiness->mouse_events_ready ? 1 : 0;
  out->active = readiness->health.active ? 1 : 0;
  out->framebuffer_ready = readiness->health.framebuffer_ready ? 1 : 0;
  out->dimensions_ready = readiness->health.dimensions_ready ? 1 : 0;
  out->mouse_initialized = readiness->health.mouse_initialized ? 1 : 0;
  out->cursor_valid = readiness->health.cursor_valid ? 1 : 0;
  out->taskbar_ready = readiness->health.taskbar_ready ? 1 : 0;
  out->dispatcher_health_ready = readiness->health.dispatcher_health_ready ? 1 : 0;
  out->queue_healthy = readiness->queue_healthy ? 1 : 0;
  out->no_modal_blockers = readiness->no_modal_blockers ? 1 : 0;
  out->no_window_drag = readiness->no_window_drag ? 1 : 0;
  out->blocker_flags = readiness->blocker_flags;
  missing_routes = readiness->route_summary.missing_route_flags;
  out->missing_required_route_flags =
      missing_routes & DESKTOP_MOUSE_EVENTS_SMOKE_ROUTE_REQUIRED_MASK;
  out->blocked_required_flags =
      (readiness->blocker_flags &
       DESKTOP_MOUSE_EVENTS_SMOKE_REQUIRED_BLOCKER_MASK) |
      (readiness->blocker_flags & ~DESKTOP_SMOKE_BLOCK_KNOWN_MASK);
  if (out->missing_required_route_flags != 0u) {
    out->blocked_required_flags |= DESKTOP_SMOKE_BLOCK_DISPATCHER_ROUTES;
  }
  (void)desktop_smoke_blocker_summary(out->blocked_required_flags,
                                      &blocked_summary);
  out->first_blocker_name = blocked_summary.first_blocker_name;
  out->first_missing_required_route_name =
      desktop_first_route_name_from_mask(out->missing_required_route_flags);

  if (!out->snapshot_ready) {
    out->blocked_reason = "snapshot-unavailable";
    return 1;
  }
  if (out->blocked_required_flags != 0u) {
    out->blocked_reason = out->first_blocker_name;
    return 1;
  }
  if (!out->gui_session_ready) {
    out->blocked_reason = "gui-session";
    return 1;
  }
  if (!out->mouse_events_ready) {
    out->blocked_reason = "mouse-events";
    return 1;
  }
  out->smoke_ready = 1;
  out->state = "mouse-events-ready";
  out->blocked_reason = "ready";
  return 1;
}
