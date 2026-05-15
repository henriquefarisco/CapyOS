#include <stdio.h>

#include "gui/desktop.h"
#include "util/kstring.h"

static int tests_run = 0;
static int tests_passed = 0;

#define TEST(name)   do { tests_run++; printf("  %-58s ", name); } while (0)
#define PASS()   do { printf("OK\n"); tests_passed++; } while (0)
#define FAIL(msg)   do { printf("FAIL: %s\n", msg); } while (0)

static void fill_routes(struct gui_window_dispatcher_input_routes *routes) {
  routes->key_down = 1;
  routes->key_up = 1;
  routes->mouse_scroll = 1;
  routes->mouse_hover = 1;
  routes->mouse_left_button = 1;
  routes->mouse_right_context = 1;
  routes->mouse_capture_opt_in = 1;
  routes->queue_mirror_free = 1;
  routes->overlays_direct = 1;
  routes->window_manager_direct = 1;
  routes->titlebar_direct = 1;
  routes->taskbar_direct = 1;
  routes->desktop_icons_direct = 1;
}

static struct desktop_session_health healthy_health(void) {
  struct desktop_session_health health;
  kmemzero(&health, sizeof(health));
  health.active = 1;
  health.framebuffer_ready = 1;
  health.dimensions_ready = 1;
  health.mouse_initialized = 1;
  health.cursor_valid = 1;
  health.taskbar_ready = 1;
  health.dispatcher_health_ready = 1;
  health.dispatcher.queue_snapshot_available = 1;
  fill_routes(&health.dispatcher.routes);
  return health;
}

static int name_is(uint32_t blocker, const char *expected) {
  return kstreq(desktop_smoke_block_name(blocker), expected);
}

static int route_name_is(uint32_t route, const char *expected) {
  return kstreq(desktop_dispatcher_route_name(route), expected);
}

static void test_route_metadata(void) {
  uint32_t expected = DESKTOP_DISPATCHER_ROUTE_KEY_DOWN |
                      DESKTOP_DISPATCHER_ROUTE_KEY_UP |
                      DESKTOP_DISPATCHER_ROUTE_MOUSE_SCROLL |
                      DESKTOP_DISPATCHER_ROUTE_MOUSE_HOVER |
                      DESKTOP_DISPATCHER_ROUTE_MOUSE_LEFT_BUTTON |
                      DESKTOP_DISPATCHER_ROUTE_MOUSE_RIGHT_CONTEXT |
                      DESKTOP_DISPATCHER_ROUTE_MOUSE_CAPTURE_OPT_IN |
                      DESKTOP_DISPATCHER_ROUTE_QUEUE_MIRROR_FREE |
                      DESKTOP_DISPATCHER_ROUTE_OVERLAYS_DIRECT |
                      DESKTOP_DISPATCHER_ROUTE_WINDOW_MANAGER |
                      DESKTOP_DISPATCHER_ROUTE_TITLEBAR_DIRECT |
                      DESKTOP_DISPATCHER_ROUTE_TASKBAR_DIRECT |
                      DESKTOP_DISPATCHER_ROUTE_DESKTOP_ICONS_DIRECT;
  TEST("desktop_smoke_readiness: route mask is stable");
  if (desktop_dispatcher_route_known_mask() == expected) PASS();
  else FAIL("route mask");

  TEST("desktop_smoke_readiness: route names are stable");
  if (route_name_is(DESKTOP_DISPATCHER_ROUTE_KEY_DOWN, "key-down") &&
      route_name_is(DESKTOP_DISPATCHER_ROUTE_KEY_UP, "key-up") &&
      route_name_is(DESKTOP_DISPATCHER_ROUTE_MOUSE_SCROLL, "mouse-scroll") &&
      route_name_is(DESKTOP_DISPATCHER_ROUTE_MOUSE_HOVER, "mouse-hover") &&
      route_name_is(DESKTOP_DISPATCHER_ROUTE_MOUSE_LEFT_BUTTON, "mouse-left-button") &&
      route_name_is(DESKTOP_DISPATCHER_ROUTE_MOUSE_RIGHT_CONTEXT, "mouse-right-context") &&
      route_name_is(DESKTOP_DISPATCHER_ROUTE_MOUSE_CAPTURE_OPT_IN, "mouse-capture-opt-in") &&
      route_name_is(DESKTOP_DISPATCHER_ROUTE_QUEUE_MIRROR_FREE, "queue-mirror-free") &&
      route_name_is(DESKTOP_DISPATCHER_ROUTE_OVERLAYS_DIRECT, "overlays-direct") &&
      route_name_is(DESKTOP_DISPATCHER_ROUTE_WINDOW_MANAGER, "window-manager") &&
      route_name_is(DESKTOP_DISPATCHER_ROUTE_TITLEBAR_DIRECT, "titlebar-direct") &&
      route_name_is(DESKTOP_DISPATCHER_ROUTE_TASKBAR_DIRECT, "taskbar-direct") &&
      route_name_is(DESKTOP_DISPATCHER_ROUTE_DESKTOP_ICONS_DIRECT, "desktop-icons-direct") &&
      route_name_is(0x80000000u, "unknown")) PASS();
  else FAIL("route names");
}

static void test_route_summary(void) {
  struct gui_window_dispatcher_input_routes routes;
  struct desktop_dispatcher_route_summary summary;
  kmemzero(&routes, sizeof(routes));
  fill_routes(&routes);

  TEST("desktop_smoke_readiness: route summary rejects null output");
  if (desktop_dispatcher_route_summary(&routes, 0) == 0) PASS();
  else FAIL("null route summary");

  TEST("desktop_smoke_readiness: all routes ready summary is deterministic");
  if (desktop_dispatcher_route_summary(&routes, &summary) == 1 &&
      summary.expected_route_flags == DESKTOP_DISPATCHER_ROUTE_KNOWN_MASK &&
      summary.ready_route_flags == DESKTOP_DISPATCHER_ROUTE_KNOWN_MASK &&
      summary.missing_route_flags == 0 &&
      summary.missing_route_count == 0 &&
      kstreq(summary.first_missing_route_name, "none")) PASS();
  else FAIL("all routes ready");

  routes.mouse_hover = 0;
  routes.taskbar_direct = 0;
  TEST("desktop_smoke_readiness: missing route summary is deterministic");
  if (desktop_dispatcher_route_summary(&routes, &summary) == 1 &&
      summary.missing_route_flags ==
          (DESKTOP_DISPATCHER_ROUTE_MOUSE_HOVER |
           DESKTOP_DISPATCHER_ROUTE_TASKBAR_DIRECT) &&
      summary.missing_route_count == 2 &&
      kstreq(summary.first_missing_route_name, "mouse-hover")) PASS();
  else FAIL("missing routes");

  TEST("desktop_smoke_readiness: null routes fail closed");
  if (desktop_dispatcher_route_summary(0, &summary) == 0 &&
      summary.ready_route_flags == 0 &&
      summary.missing_route_flags == DESKTOP_DISPATCHER_ROUTE_KNOWN_MASK &&
      summary.missing_route_count == 13 &&
      kstreq(summary.first_missing_route_name, "key-down")) PASS();
  else FAIL("null routes");
}

static void test_blocker_metadata(void) {
  uint32_t expected = DESKTOP_SMOKE_BLOCK_INACTIVE |
                      DESKTOP_SMOKE_BLOCK_FRAMEBUFFER |
                      DESKTOP_SMOKE_BLOCK_DIMENSIONS |
                      DESKTOP_SMOKE_BLOCK_MOUSE |
                      DESKTOP_SMOKE_BLOCK_CURSOR |
                      DESKTOP_SMOKE_BLOCK_TASKBAR |
                      DESKTOP_SMOKE_BLOCK_DISPATCHER |
                      DESKTOP_SMOKE_BLOCK_DISPATCHER_ROUTES |
                      DESKTOP_SMOKE_BLOCK_QUEUE |
                      DESKTOP_SMOKE_BLOCK_OVERLAY |
                      DESKTOP_SMOKE_BLOCK_WINDOW_DRAG;
  TEST("desktop_smoke_readiness: known blocker mask is stable");
  if (desktop_smoke_block_known_mask() == expected) PASS();
  else FAIL("known mask");

  TEST("desktop_smoke_readiness: blocker names are stable");
  if (name_is(DESKTOP_SMOKE_BLOCK_INACTIVE, "inactive") &&
      name_is(DESKTOP_SMOKE_BLOCK_FRAMEBUFFER, "framebuffer") &&
      name_is(DESKTOP_SMOKE_BLOCK_DIMENSIONS, "dimensions") &&
      name_is(DESKTOP_SMOKE_BLOCK_MOUSE, "mouse") &&
      name_is(DESKTOP_SMOKE_BLOCK_CURSOR, "cursor") &&
      name_is(DESKTOP_SMOKE_BLOCK_TASKBAR, "taskbar") &&
      name_is(DESKTOP_SMOKE_BLOCK_DISPATCHER, "dispatcher") &&
      name_is(DESKTOP_SMOKE_BLOCK_DISPATCHER_ROUTES, "dispatcher-routes") &&
      name_is(DESKTOP_SMOKE_BLOCK_QUEUE, "queue") &&
      name_is(DESKTOP_SMOKE_BLOCK_OVERLAY, "overlay") &&
      name_is(DESKTOP_SMOKE_BLOCK_WINDOW_DRAG, "window-drag") &&
      name_is(0x80000000u, "unknown")) PASS();
  else FAIL("blocker names");
}

static void test_blocker_summary(void) {
  struct desktop_smoke_blocker_summary summary;
  uint32_t flags = DESKTOP_SMOKE_BLOCK_QUEUE |
                   DESKTOP_SMOKE_BLOCK_MOUSE |
                   0x80000000u;
  TEST("desktop_smoke_readiness: blocker summary rejects null output");
  if (desktop_smoke_blocker_summary(flags, 0) == 0) PASS();
  else FAIL("null summary");

  TEST("desktop_smoke_readiness: empty blocker summary is deterministic");
  if (desktop_smoke_blocker_summary(0, &summary) == 1 &&
      summary.blocker_flags == 0 &&
      summary.known_blocker_flags == 0 &&
      summary.unknown_blocker_flags == 0 &&
      summary.blocker_count == 0 &&
      kstreq(summary.first_blocker_name, "none")) PASS();
  else FAIL("empty summary");

  TEST("desktop_smoke_readiness: mixed blocker summary is deterministic");
  if (desktop_smoke_blocker_summary(flags, &summary) == 1 &&
      summary.blocker_flags == flags &&
      summary.known_blocker_flags ==
          (DESKTOP_SMOKE_BLOCK_MOUSE | DESKTOP_SMOKE_BLOCK_QUEUE) &&
      summary.unknown_blocker_flags == 0x80000000u &&
      summary.blocker_count == 3 &&
      kstreq(summary.first_blocker_name, "mouse")) PASS();
  else FAIL("mixed summary");

  TEST("desktop_smoke_readiness: unknown-only summary is deterministic");
  if (desktop_smoke_blocker_summary(0x80000000u, &summary) == 1 &&
      summary.known_blocker_flags == 0 &&
      summary.unknown_blocker_flags == 0x80000000u &&
      summary.blocker_count == 1 &&
      kstreq(summary.first_blocker_name, "unknown")) PASS();
  else FAIL("unknown summary");
}

static void test_rejects_null_output(void) {
  struct desktop_session_health health = healthy_health();
  TEST("desktop_smoke_readiness: rejects null output");
  if (desktop_session_smoke_readiness_from_health(&health, 0) == 0) PASS();
  else FAIL("null output");
}

static void test_null_health_clears_output(void) {
  struct desktop_session_smoke_readiness out;
  out.snapshot_ready = 1;
  out.blocker_flags = 0xffffffffu;
  out.gui_session_ready = 1;
  TEST("desktop_smoke_readiness: null health clears output");
  if (desktop_session_smoke_readiness_from_health(0, &out) == 0 &&
      out.snapshot_ready == 0 &&
      out.blocker_flags == 0 &&
      out.gui_session_ready == 0) PASS();
  else FAIL("null health");
}

static void test_ready_health_passes(void) {
  struct desktop_session_health health = healthy_health();
  struct desktop_session_smoke_readiness out;
  TEST("desktop_smoke_readiness: healthy session is ready");
  if (desktop_session_smoke_readiness_from_health(&health, &out) == 1 &&
      out.snapshot_ready == 1 &&
      out.blocker_flags == 0 &&
      out.dispatcher_routes_ready == 1 &&
      out.queue_healthy == 1 &&
      out.route_summary.missing_route_count == 0 &&
      kstreq(out.route_summary.first_missing_route_name, "none") &&
      out.blocker_summary.blocker_count == 0 &&
      kstreq(out.blocker_summary.first_blocker_name, "none") &&
      out.gui_session_ready == 1 &&
      out.mouse_events_ready == 1) PASS();
  else FAIL("ready health");
}

static void test_base_blockers_are_reported(void) {
  struct desktop_session_health health = healthy_health();
  struct desktop_session_smoke_readiness out;
  health.active = 0;
  health.framebuffer_ready = 0;
  health.dimensions_ready = 0;
  health.mouse_initialized = 0;
  health.cursor_valid = 0;
  health.taskbar_ready = 0;
  health.dispatcher_health_ready = 0;
  TEST("desktop_smoke_readiness: base blockers are reported");
  if (desktop_session_smoke_readiness_from_health(&health, &out) == 1 &&
      (out.blocker_flags & DESKTOP_SMOKE_BLOCK_INACTIVE) &&
      (out.blocker_flags & DESKTOP_SMOKE_BLOCK_FRAMEBUFFER) &&
      (out.blocker_flags & DESKTOP_SMOKE_BLOCK_DIMENSIONS) &&
      (out.blocker_flags & DESKTOP_SMOKE_BLOCK_MOUSE) &&
      (out.blocker_flags & DESKTOP_SMOKE_BLOCK_CURSOR) &&
      (out.blocker_flags & DESKTOP_SMOKE_BLOCK_TASKBAR) &&
      (out.blocker_flags & DESKTOP_SMOKE_BLOCK_DISPATCHER) &&
      out.blocker_summary.blocker_flags == out.blocker_flags &&
      kstreq(out.blocker_summary.first_blocker_name, "inactive") &&
      out.gui_session_ready == 0 &&
      out.mouse_events_ready == 0) PASS();
  else FAIL("base blockers");
}

static void test_route_blocker_is_reported(void) {
  struct desktop_session_health health = healthy_health();
  struct desktop_session_smoke_readiness out;
  health.dispatcher.routes.mouse_hover = 0;
  TEST("desktop_smoke_readiness: route blocker is reported");
  if (desktop_session_smoke_readiness_from_health(&health, &out) == 1 &&
      out.dispatcher_routes_ready == 0 &&
      (out.blocker_flags & DESKTOP_SMOKE_BLOCK_DISPATCHER_ROUTES) &&
      out.route_summary.missing_route_flags == DESKTOP_DISPATCHER_ROUTE_MOUSE_HOVER &&
      out.route_summary.missing_route_count == 1 &&
      kstreq(out.route_summary.first_missing_route_name, "mouse-hover") &&
      out.blocker_summary.blocker_count == 1 &&
      kstreq(out.blocker_summary.first_blocker_name, "dispatcher-routes") &&
      out.gui_session_ready == 1 &&
      out.mouse_events_ready == 0) PASS();
  else FAIL("route blocker");
}

static void test_queue_blocker_is_reported(void) {
  struct desktop_session_health health = healthy_health();
  struct desktop_session_smoke_readiness out;
  health.dispatcher.backlog_warning = 1;
  TEST("desktop_smoke_readiness: queue blocker is reported");
  if (desktop_session_smoke_readiness_from_health(&health, &out) == 1 &&
      out.queue_healthy == 0 &&
      (out.blocker_flags & DESKTOP_SMOKE_BLOCK_QUEUE) &&
      out.blocker_summary.blocker_count == 1 &&
      kstreq(out.blocker_summary.first_blocker_name, "queue") &&
      out.gui_session_ready == 0) PASS();
  else FAIL("queue blocker");
}

static void test_modal_and_drag_blockers_are_reported(void) {
  struct desktop_session_health health = healthy_health();
  struct desktop_session_smoke_readiness out;
  health.overlay_active = 1;
  health.window_manager_drag_active = 1;
  TEST("desktop_smoke_readiness: modal and drag blockers are reported");
  if (desktop_session_smoke_readiness_from_health(&health, &out) == 1 &&
      out.no_modal_blockers == 0 &&
      out.no_window_drag == 0 &&
      (out.blocker_flags & DESKTOP_SMOKE_BLOCK_OVERLAY) &&
      (out.blocker_flags & DESKTOP_SMOKE_BLOCK_WINDOW_DRAG) &&
      out.blocker_summary.blocker_count == 2 &&
      kstreq(out.blocker_summary.first_blocker_name, "overlay") &&
      out.gui_session_ready == 0) PASS();
  else FAIL("modal drag blocker");
}

static void test_gui_session_gate_metadata(void) {
  uint32_t expected_blockers =
      DESKTOP_SMOKE_BLOCK_INACTIVE |
      DESKTOP_SMOKE_BLOCK_FRAMEBUFFER |
      DESKTOP_SMOKE_BLOCK_DIMENSIONS |
      DESKTOP_SMOKE_BLOCK_TASKBAR |
      DESKTOP_SMOKE_BLOCK_DISPATCHER |
      DESKTOP_SMOKE_BLOCK_QUEUE |
      DESKTOP_SMOKE_BLOCK_OVERLAY |
      DESKTOP_SMOKE_BLOCK_WINDOW_DRAG;
  uint32_t expected_routes =
      DESKTOP_DISPATCHER_ROUTE_KEY_DOWN |
      DESKTOP_DISPATCHER_ROUTE_KEY_UP |
      DESKTOP_DISPATCHER_ROUTE_QUEUE_MIRROR_FREE |
      DESKTOP_DISPATCHER_ROUTE_OVERLAYS_DIRECT |
      DESKTOP_DISPATCHER_ROUTE_WINDOW_MANAGER |
      DESKTOP_DISPATCHER_ROUTE_TITLEBAR_DIRECT |
      DESKTOP_DISPATCHER_ROUTE_TASKBAR_DIRECT |
      DESKTOP_DISPATCHER_ROUTE_DESKTOP_ICONS_DIRECT;
  TEST("desktop_smoke_readiness: gui-session gate metadata is stable");
  if (DESKTOP_GUI_SESSION_SMOKE_REQUIRED_BLOCKER_MASK == expected_blockers &&
      DESKTOP_GUI_SESSION_ROUTE_REQUIRED_MASK == expected_routes &&
      kstreq(desktop_gui_session_smoke_marker(),
             DESKTOP_GUI_SESSION_SMOKE_MARKER)) PASS();
  else FAIL("gui-session gate metadata");
}

static void test_gui_session_gate_rejects_null_output(void) {
  struct desktop_session_health health = healthy_health();
  struct desktop_session_smoke_readiness readiness;
  TEST("desktop_smoke_readiness: gui-session gate rejects null output");
  if (desktop_session_smoke_readiness_from_health(&health, &readiness) == 1 &&
      desktop_gui_session_smoke_gate_from_readiness(&readiness, 0) == 0) PASS();
  else FAIL("null gui-session gate output");
}

static void test_gui_session_gate_null_readiness_fails_closed(void) {
  struct desktop_gui_session_smoke_gate gate;
  gate.smoke_ready = 1;
  gate.blocked_reason = "dirty";
  TEST("desktop_smoke_readiness: gui-session gate null readiness fails closed");
  if (desktop_gui_session_smoke_gate_from_readiness(0, &gate) == 0 &&
      gate.version == DESKTOP_GUI_SESSION_SMOKE_GATE_VERSION &&
      gate.readiness_available == 0 &&
      gate.smoke_ready == 0 &&
      kstreq(gate.blocked_reason, "readiness-unavailable")) PASS();
  else FAIL("null gui-session readiness");
}

static void test_gui_session_gate_allows_mouse_events_deferred(void) {
  struct desktop_session_health health = healthy_health();
  struct desktop_session_smoke_readiness readiness;
  struct desktop_gui_session_smoke_gate gate;
  health.mouse_initialized = 0;
  health.cursor_valid = 0;
  health.dispatcher.routes.mouse_scroll = 0;
  health.dispatcher.routes.mouse_hover = 0;
  health.dispatcher.routes.mouse_left_button = 0;
  health.dispatcher.routes.mouse_right_context = 0;
  health.dispatcher.routes.mouse_capture_opt_in = 0;
  TEST("desktop_smoke_readiness: gui-session gate defers mouse-events");
  if (desktop_session_smoke_readiness_from_health(&health, &readiness) == 1 &&
      desktop_gui_session_smoke_gate_from_readiness(&readiness, &gate) == 1 &&
      readiness.gui_session_ready == 1 &&
      readiness.mouse_events_ready == 0 &&
      gate.smoke_ready == 1 &&
      gate.mouse_events_deferred == 1 &&
      gate.blocked_required_flags == 0 &&
      (gate.deferred_mouse_blocker_flags & DESKTOP_SMOKE_BLOCK_MOUSE) &&
      (gate.deferred_mouse_blocker_flags & DESKTOP_SMOKE_BLOCK_CURSOR) &&
      (gate.deferred_mouse_blocker_flags & DESKTOP_SMOKE_BLOCK_DISPATCHER_ROUTES) &&
      gate.deferred_mouse_route_flags == DESKTOP_MOUSE_EVENTS_ROUTE_REQUIRED_MASK &&
      kstreq(gate.state, "gui-session-ready") &&
      kstreq(gate.blocked_reason, "ready")) PASS();
  else FAIL("deferred mouse-events gate");
}

static void test_gui_session_gate_blocks_required_route(void) {
  struct desktop_session_health health = healthy_health();
  struct desktop_session_smoke_readiness readiness;
  struct desktop_gui_session_smoke_gate gate;
  health.dispatcher.routes.key_down = 0;
  TEST("desktop_smoke_readiness: gui-session gate blocks required route");
  if (desktop_session_smoke_readiness_from_health(&health, &readiness) == 1 &&
      desktop_gui_session_smoke_gate_from_readiness(&readiness, &gate) == 1 &&
      gate.smoke_ready == 0 &&
      (gate.blocked_required_flags & DESKTOP_SMOKE_BLOCK_DISPATCHER_ROUTES) &&
      gate.missing_required_route_flags == DESKTOP_DISPATCHER_ROUTE_KEY_DOWN &&
      kstreq(gate.first_missing_required_route_name, "key-down") &&
      kstreq(gate.blocked_reason, "dispatcher-routes")) PASS();
  else FAIL("required route block");
}

static void test_gui_session_gate_blocks_base_blocker(void) {
  struct desktop_session_health health = healthy_health();
  struct desktop_session_smoke_readiness readiness;
  struct desktop_gui_session_smoke_gate gate;
  health.taskbar_ready = 0;
  TEST("desktop_smoke_readiness: gui-session gate blocks base blocker");
  if (desktop_session_smoke_readiness_from_health(&health, &readiness) == 1 &&
      desktop_gui_session_smoke_gate_from_readiness(&readiness, &gate) == 1 &&
      gate.smoke_ready == 0 &&
      gate.blocked_required_flags == DESKTOP_SMOKE_BLOCK_TASKBAR &&
      kstreq(gate.first_blocker_name, "taskbar") &&
      kstreq(gate.blocked_reason, "taskbar")) PASS();
  else FAIL("base blocker gate");
}

static void test_gui_session_gate_blocks_unknown_blocker(void) {
  struct desktop_session_health health = healthy_health();
  struct desktop_session_smoke_readiness readiness;
  struct desktop_gui_session_smoke_gate gate;
  TEST("desktop_smoke_readiness: gui-session gate blocks unknown blocker");
  if (desktop_session_smoke_readiness_from_health(&health, &readiness) == 1) {
    readiness.blocker_flags |= 0x80000000u;
  }
  if (desktop_gui_session_smoke_gate_from_readiness(&readiness, &gate) == 1 &&
      gate.smoke_ready == 0 &&
      gate.blocked_required_flags == 0x80000000u &&
      kstreq(gate.first_blocker_name, "unknown") &&
      kstreq(gate.blocked_reason, "unknown")) PASS();
  else FAIL("unknown blocker gate");
}

static void test_mouse_events_gate_metadata(void) {
  uint32_t expected_blockers =
      DESKTOP_GUI_SESSION_SMOKE_REQUIRED_BLOCKER_MASK |
      DESKTOP_SMOKE_BLOCK_MOUSE |
      DESKTOP_SMOKE_BLOCK_CURSOR |
      DESKTOP_SMOKE_BLOCK_DISPATCHER_ROUTES;
  uint32_t expected_routes =
      DESKTOP_GUI_SESSION_ROUTE_REQUIRED_MASK |
      DESKTOP_MOUSE_EVENTS_ROUTE_REQUIRED_MASK;
  TEST("desktop_smoke_readiness: mouse-events gate metadata is stable");
  if (DESKTOP_MOUSE_EVENTS_SMOKE_REQUIRED_BLOCKER_MASK == expected_blockers &&
      DESKTOP_MOUSE_EVENTS_SMOKE_ROUTE_REQUIRED_MASK == expected_routes &&
      kstreq(desktop_mouse_events_smoke_marker(),
             DESKTOP_MOUSE_EVENTS_SMOKE_MARKER)) PASS();
  else FAIL("mouse-events gate metadata");
}

static void test_mouse_events_gate_rejects_null_output(void) {
  struct desktop_session_health health = healthy_health();
  struct desktop_session_smoke_readiness readiness;
  TEST("desktop_smoke_readiness: mouse-events gate rejects null output");
  if (desktop_session_smoke_readiness_from_health(&health, &readiness) == 1 &&
      desktop_mouse_events_smoke_gate_from_readiness(&readiness, 0) == 0) PASS();
  else FAIL("null mouse-events gate output");
}

static void test_mouse_events_gate_null_readiness_fails_closed(void) {
  struct desktop_mouse_events_smoke_gate gate;
  gate.smoke_ready = 1;
  gate.blocked_reason = "dirty";
  TEST("desktop_smoke_readiness: mouse-events gate null readiness fails closed");
  if (desktop_mouse_events_smoke_gate_from_readiness(0, &gate) == 0 &&
      gate.version == DESKTOP_MOUSE_EVENTS_SMOKE_GATE_VERSION &&
      gate.readiness_available == 0 &&
      gate.smoke_ready == 0 &&
      kstreq(gate.blocked_reason, "readiness-unavailable")) PASS();
  else FAIL("null mouse-events readiness");
}

static void test_mouse_events_gate_passes_ready_session(void) {
  struct desktop_session_health health = healthy_health();
  struct desktop_session_smoke_readiness readiness;
  struct desktop_mouse_events_smoke_gate gate;
  TEST("desktop_smoke_readiness: mouse-events gate passes ready session");
  if (desktop_session_smoke_readiness_from_health(&health, &readiness) == 1 &&
      desktop_mouse_events_smoke_gate_from_readiness(&readiness, &gate) == 1 &&
      readiness.gui_session_ready == 1 &&
      readiness.mouse_events_ready == 1 &&
      gate.smoke_ready == 1 &&
      gate.blocked_required_flags == 0 &&
      gate.missing_required_route_flags == 0 &&
      kstreq(gate.state, "mouse-events-ready") &&
      kstreq(gate.blocked_reason, "ready")) PASS();
  else FAIL("ready mouse-events gate");
}

static void test_mouse_events_gate_blocks_mouse_missing(void) {
  struct desktop_session_health health = healthy_health();
  struct desktop_session_smoke_readiness readiness;
  struct desktop_mouse_events_smoke_gate gate;
  health.mouse_initialized = 0;
  TEST("desktop_smoke_readiness: mouse-events gate blocks missing mouse");
  if (desktop_session_smoke_readiness_from_health(&health, &readiness) == 1 &&
      desktop_mouse_events_smoke_gate_from_readiness(&readiness, &gate) == 1 &&
      readiness.gui_session_ready == 1 &&
      readiness.mouse_events_ready == 0 &&
      gate.smoke_ready == 0 &&
      gate.blocked_required_flags == DESKTOP_SMOKE_BLOCK_MOUSE &&
      kstreq(gate.first_blocker_name, "mouse") &&
      kstreq(gate.blocked_reason, "mouse")) PASS();
  else FAIL("missing mouse gate");
}

static void test_mouse_events_gate_blocks_mouse_route(void) {
  struct desktop_session_health health = healthy_health();
  struct desktop_session_smoke_readiness readiness;
  struct desktop_mouse_events_smoke_gate gate;
  health.dispatcher.routes.mouse_left_button = 0;
  TEST("desktop_smoke_readiness: mouse-events gate blocks mouse route");
  if (desktop_session_smoke_readiness_from_health(&health, &readiness) == 1 &&
      desktop_mouse_events_smoke_gate_from_readiness(&readiness, &gate) == 1 &&
      readiness.gui_session_ready == 1 &&
      readiness.mouse_events_ready == 0 &&
      gate.smoke_ready == 0 &&
      (gate.blocked_required_flags & DESKTOP_SMOKE_BLOCK_DISPATCHER_ROUTES) &&
      gate.missing_required_route_flags ==
          DESKTOP_DISPATCHER_ROUTE_MOUSE_LEFT_BUTTON &&
      kstreq(gate.first_missing_required_route_name, "mouse-left-button") &&
      kstreq(gate.blocked_reason, "dispatcher-routes")) PASS();
  else FAIL("mouse route gate");
}

static void test_mouse_events_gate_blocks_gui_session_route(void) {
  struct desktop_session_health health = healthy_health();
  struct desktop_session_smoke_readiness readiness;
  struct desktop_mouse_events_smoke_gate gate;
  health.dispatcher.routes.taskbar_direct = 0;
  TEST("desktop_smoke_readiness: mouse-events gate blocks gui route");
  if (desktop_session_smoke_readiness_from_health(&health, &readiness) == 1 &&
      desktop_mouse_events_smoke_gate_from_readiness(&readiness, &gate) == 1 &&
      readiness.gui_session_ready == 0 &&
      readiness.mouse_events_ready == 0 &&
      gate.smoke_ready == 0 &&
      (gate.blocked_required_flags & DESKTOP_SMOKE_BLOCK_DISPATCHER_ROUTES) &&
      gate.missing_required_route_flags ==
          DESKTOP_DISPATCHER_ROUTE_TASKBAR_DIRECT &&
      kstreq(gate.first_missing_required_route_name, "taskbar-direct") &&
      kstreq(gate.blocked_reason, "dispatcher-routes")) PASS();
  else FAIL("gui route mouse-events gate");
}

static void test_mouse_events_gate_blocks_unknown_blocker(void) {
  struct desktop_session_health health = healthy_health();
  struct desktop_session_smoke_readiness readiness;
  struct desktop_mouse_events_smoke_gate gate;
  TEST("desktop_smoke_readiness: mouse-events gate blocks unknown blocker");
  if (desktop_session_smoke_readiness_from_health(&health, &readiness) == 1) {
    readiness.blocker_flags |= 0x80000000u;
  }
  if (desktop_mouse_events_smoke_gate_from_readiness(&readiness, &gate) == 1 &&
      gate.smoke_ready == 0 &&
      gate.blocked_required_flags == 0x80000000u &&
      kstreq(gate.first_blocker_name, "unknown") &&
      kstreq(gate.blocked_reason, "unknown")) PASS();
  else FAIL("unknown mouse-events blocker gate");
}

int test_desktop_smoke_readiness_run(void) {
  printf("[test_desktop_smoke_readiness]\n");
  tests_run = 0;
  tests_passed = 0;
  test_route_metadata();
  test_route_summary();
  test_blocker_metadata();
  test_blocker_summary();
  test_rejects_null_output();
  test_null_health_clears_output();
  test_ready_health_passes();
  test_base_blockers_are_reported();
  test_route_blocker_is_reported();
  test_queue_blocker_is_reported();
  test_modal_and_drag_blockers_are_reported();
  test_gui_session_gate_metadata();
  test_gui_session_gate_rejects_null_output();
  test_gui_session_gate_null_readiness_fails_closed();
  test_gui_session_gate_allows_mouse_events_deferred();
  test_gui_session_gate_blocks_required_route();
  test_gui_session_gate_blocks_base_blocker();
  test_gui_session_gate_blocks_unknown_blocker();
  test_mouse_events_gate_metadata();
  test_mouse_events_gate_rejects_null_output();
  test_mouse_events_gate_null_readiness_fails_closed();
  test_mouse_events_gate_passes_ready_session();
  test_mouse_events_gate_blocks_mouse_missing();
  test_mouse_events_gate_blocks_mouse_route();
  test_mouse_events_gate_blocks_gui_session_route();
  test_mouse_events_gate_blocks_unknown_blocker();
  printf("  -> %d/%d passed\n", tests_passed, tests_run);
  return tests_run - tests_passed;
}
