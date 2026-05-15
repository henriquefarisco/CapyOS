#include "gui/event.h"
#include <stddef.h>

static struct gui_event event_queue[GUI_EVENT_QUEUE_SIZE];
static uint32_t eq_head = 0;
static uint32_t eq_tail = 0;
static uint32_t eq_count = 0;
static uint32_t eq_high_watermark = 0;
static uint64_t eq_dropped_total = 0;

static int16_t gui_event_clamp_i16(int32_t value) {
  if (value > 32767) return 32767;
  if (value < -32768) return -32768;
  return (int16_t)value;
}

static int gui_event_has_pending_paint(uint32_t window_id) {
  uint32_t idx = eq_tail;
  for (uint32_t i = 0; i < eq_count; i++) {
    if (event_queue[idx].type == GUI_EVENT_PAINT &&
        event_queue[idx].window_id == window_id) return 1;
    idx = (idx + 1) % GUI_EVENT_QUEUE_SIZE;
  }
  return 0;
}

static int gui_event_has_pending_timer(uint32_t window_id, uint32_t timer_id) {
  uint32_t idx = eq_tail;
  for (uint32_t i = 0; i < eq_count; i++) {
    if (event_queue[idx].type == GUI_EVENT_TIMER &&
        event_queue[idx].window_id == window_id &&
        event_queue[idx].timer.timer_id == timer_id) return 1;
    idx = (idx + 1) % GUI_EVENT_QUEUE_SIZE;
  }
  return 0;
}

static int gui_event_coalesce_last_mouse_move(int32_t x, int32_t y,
                                              int16_t dx, int16_t dy,
                                              uint8_t buttons,
                                              uint64_t timestamp) {
  struct gui_event *prev = NULL;
  uint32_t idx = 0;
  if (eq_count == 0) return 0;
  idx = (eq_head + GUI_EVENT_QUEUE_SIZE - 1u) % GUI_EVENT_QUEUE_SIZE;
  prev = &event_queue[idx];
  if (prev->type != GUI_EVENT_MOUSE_MOVE) return 0;
  if (prev->window_id != 0) return 0;
  prev->mouse.x = x;
  prev->mouse.y = y;
  prev->mouse.dx = gui_event_clamp_i16((int32_t)prev->mouse.dx + dx);
  prev->mouse.dy = gui_event_clamp_i16((int32_t)prev->mouse.dy + dy);
  prev->mouse.buttons = buttons;
  prev->timestamp = timestamp;
  return 1;
}

static int gui_event_coalesce_last_mouse_scroll(int32_t x, int32_t y,
                                                int16_t dz, uint8_t buttons,
                                                uint64_t timestamp) {
  struct gui_event *prev = NULL;
  uint32_t idx = 0;
  if (eq_count == 0) return 0;
  idx = (eq_head + GUI_EVENT_QUEUE_SIZE - 1u) % GUI_EVENT_QUEUE_SIZE;
  prev = &event_queue[idx];
  if (prev->type != GUI_EVENT_MOUSE_SCROLL) return 0;
  if (prev->window_id != 0) return 0;
  prev->mouse.x = x;
  prev->mouse.y = y;
  prev->mouse.dx = 0;
  prev->mouse.dy = gui_event_clamp_i16((int32_t)prev->mouse.dy + dz);
  prev->mouse.buttons = buttons;
  prev->timestamp = timestamp;
  return 1;
}

void gui_event_init(void) {
  eq_head = 0;
  eq_tail = 0;
  eq_count = 0;
  eq_high_watermark = 0;
  eq_dropped_total = 0;
}

int gui_event_push(const struct gui_event *ev) {
  if (!ev) return -1;
  if (eq_count >= GUI_EVENT_QUEUE_SIZE) {
    eq_tail = (eq_tail + 1) % GUI_EVENT_QUEUE_SIZE;
    eq_count--;
    eq_dropped_total++;
  }
  event_queue[eq_head] = *ev;
  eq_head = (eq_head + 1) % GUI_EVENT_QUEUE_SIZE;
  eq_count++;
  if (eq_count > eq_high_watermark) eq_high_watermark = eq_count;
  return 0;
}

int gui_event_push_key(uint32_t window_id, uint32_t keycode,
                       uint8_t modifiers, char ch, uint64_t timestamp) {
  struct gui_event ev;
  ev.type = GUI_EVENT_KEY_DOWN;
  ev.window_id = window_id;
  ev.key.keycode = keycode;
  ev.key.modifiers = modifiers;
  ev.key.ch = ch;
  ev.timestamp = timestamp;
  return gui_event_push(&ev);
}

int gui_event_push_key_up(uint32_t window_id, uint32_t keycode,
                          uint8_t modifiers, char ch, uint64_t timestamp) {
  struct gui_event ev;
  ev.type = GUI_EVENT_KEY_UP;
  ev.window_id = window_id;
  ev.key.keycode = keycode;
  ev.key.modifiers = modifiers;
  ev.key.ch = ch;
  ev.timestamp = timestamp;
  return gui_event_push(&ev);
}

int gui_event_push_mouse_move(int32_t x, int32_t y, int16_t dx, int16_t dy,
                              uint8_t buttons, uint64_t timestamp) {
  struct gui_event ev;
  if (gui_event_coalesce_last_mouse_move(x, y, dx, dy, buttons, timestamp))
    return 0;
  ev.type = GUI_EVENT_MOUSE_MOVE;
  ev.window_id = 0;
  ev.mouse.x = x;
  ev.mouse.y = y;
  ev.mouse.dx = dx;
  ev.mouse.dy = dy;
  ev.mouse.buttons = buttons;
  ev.timestamp = timestamp;
  return gui_event_push(&ev);
}

int gui_event_push_mouse_button(int down, int32_t x, int32_t y,
                                uint8_t buttons, uint64_t timestamp) {
  struct gui_event ev;
  ev.type = down ? GUI_EVENT_MOUSE_DOWN : GUI_EVENT_MOUSE_UP;
  ev.window_id = 0;
  ev.mouse.x = x;
  ev.mouse.y = y;
  ev.mouse.dx = 0;
  ev.mouse.dy = 0;
  ev.mouse.buttons = buttons;
  ev.timestamp = timestamp;
  return gui_event_push(&ev);
}

int gui_event_push_mouse_scroll(int32_t x, int32_t y, int16_t dz,
                                uint8_t buttons, uint64_t timestamp) {
  struct gui_event ev;
  if (gui_event_coalesce_last_mouse_scroll(x, y, dz, buttons, timestamp))
    return 0;
  ev.type = GUI_EVENT_MOUSE_SCROLL;
  ev.window_id = 0;
  ev.mouse.x = x;
  ev.mouse.y = y;
  ev.mouse.dx = 0;
  ev.mouse.dy = dz;
  ev.mouse.buttons = buttons;
  ev.timestamp = timestamp;
  return gui_event_push(&ev);
}

int gui_event_push_window_close(uint32_t window_id, uint64_t timestamp) {
  struct gui_event ev = {0};
  ev.type = GUI_EVENT_WINDOW_CLOSE;
  ev.window_id = window_id;
  ev.timestamp = timestamp;
  return gui_event_push(&ev);
}

int gui_event_push_window_resize(uint32_t window_id, int32_t width,
                                 int32_t height, uint64_t timestamp) {
  struct gui_event ev = {0};
  ev.type = GUI_EVENT_WINDOW_RESIZE;
  ev.window_id = window_id;
  ev.resize.width = width;
  ev.resize.height = height;
  ev.timestamp = timestamp;
  return gui_event_push(&ev);
}

int gui_event_push_window_focus(uint32_t window_id, uint64_t timestamp) {
  struct gui_event ev = {0};
  ev.type = GUI_EVENT_WINDOW_FOCUS;
  ev.window_id = window_id;
  ev.timestamp = timestamp;
  return gui_event_push(&ev);
}

int gui_event_push_window_blur(uint32_t window_id, uint64_t timestamp) {
  struct gui_event ev = {0};
  ev.type = GUI_EVENT_WINDOW_BLUR;
  ev.window_id = window_id;
  ev.timestamp = timestamp;
  return gui_event_push(&ev);
}

int gui_event_push_paint(uint32_t window_id, uint64_t timestamp) {
  struct gui_event ev = {0};
  if (window_id == 0) return -1;
  if (gui_event_has_pending_paint(window_id)) return 0;
  ev.type = GUI_EVENT_PAINT;
  ev.window_id = window_id;
  ev.timestamp = timestamp;
  return gui_event_push(&ev);
}

int gui_event_push_timer(uint32_t window_id, uint32_t timer_id,
                         uint64_t timestamp) {
  struct gui_event ev = {0};
  if (window_id == 0) return -1;
  if (gui_event_has_pending_timer(window_id, timer_id)) return 0;
  ev.type = GUI_EVENT_TIMER;
  ev.window_id = window_id;
  ev.timer.timer_id = timer_id;
  ev.timestamp = timestamp;
  return gui_event_push(&ev);
}

int gui_event_poll(struct gui_event *ev) {
  if (!ev || eq_count == 0) return -1;
  *ev = event_queue[eq_tail];
  eq_tail = (eq_tail + 1) % GUI_EVENT_QUEUE_SIZE;
  eq_count--;
  return 0;
}

uint32_t gui_event_poll_many(struct gui_event *out, uint32_t max) {
  uint32_t copied = 0;
  if (!out || max == 0) return 0;
  while (copied < max && eq_count > 0) {
    out[copied++] = event_queue[eq_tail];
    eq_tail = (eq_tail + 1) % GUI_EVENT_QUEUE_SIZE;
    eq_count--;
  }
  return copied;
}

uint32_t gui_event_peek_many(struct gui_event *out, uint32_t max) {
  uint32_t copied = 0;
  uint32_t idx = eq_tail;
  if (!out || max == 0) return 0;
  while (copied < max && copied < eq_count) {
    out[copied++] = event_queue[idx];
    idx = (idx + 1) % GUI_EVENT_QUEUE_SIZE;
  }
  return copied;
}

uint32_t gui_event_dispatch(gui_event_dispatch_fn dispatch, void *ctx,
                            uint32_t max_events) {
  uint32_t limit = eq_count;
  uint32_t dispatched = 0;
  struct gui_event ev;
  if (!dispatch || max_events == 0) return 0;
  if (limit > max_events) limit = max_events;
  while (dispatched < limit && gui_event_poll(&ev) == 0) {
    dispatch(&ev, ctx);
    dispatched++;
  }
  return dispatched;
}

int gui_event_peek(struct gui_event *ev) {
  if (!ev || eq_count == 0) return -1;
  *ev = event_queue[eq_tail];
  return 0;
}

int gui_event_pending(void) {
  return (int)eq_count;
}

int gui_event_ready(void) {
  return eq_count > 0 ? 1 : 0;
}

void gui_event_flush(void) {
  eq_head = 0;
  eq_tail = 0;
  eq_count = 0;
}

uint32_t gui_event_discard_window(uint32_t window_id) {
  uint32_t original = eq_count;
  uint32_t kept = 0;
  uint32_t read = eq_tail;
  uint32_t write = eq_tail;
  if (window_id == 0 || eq_count == 0) return 0;
  for (uint32_t i = 0; i < original; i++) {
    struct gui_event ev = event_queue[read];
    read = (read + 1) % GUI_EVENT_QUEUE_SIZE;
    if (ev.window_id == window_id) continue;
    event_queue[write] = ev;
    write = (write + 1) % GUI_EVENT_QUEUE_SIZE;
    kept++;
  }
  eq_head = write;
  eq_count = kept;
  if (kept == 0) {
    eq_head = 0;
    eq_tail = 0;
  }
  return original - kept;
}

uint32_t gui_event_capacity(void) {
  return GUI_EVENT_QUEUE_SIZE;
}

uint32_t gui_event_space_available(void) {
  return GUI_EVENT_QUEUE_SIZE - eq_count;
}

int gui_event_full(void) {
  return eq_count >= GUI_EVENT_QUEUE_SIZE ? 1 : 0;
}

uint64_t gui_event_dropped_total(void) {
  return eq_dropped_total;
}

int gui_event_snapshot(struct gui_event_snapshot *out) {
  if (!out) return 0;
  uint32_t count = eq_count;
  out->capacity = GUI_EVENT_QUEUE_SIZE;
  out->pending = count;
  out->space_available = GUI_EVENT_QUEUE_SIZE - count;
  out->high_watermark = eq_high_watermark;
  out->dropped_total = eq_dropped_total;
  return 1;
}

void gui_event_reset_diagnostics(void) {
  eq_high_watermark = eq_count;
  eq_dropped_total = 0;
}
