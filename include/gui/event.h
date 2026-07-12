#ifndef GUI_EVENT_H
#define GUI_EVENT_H

#include <stdint.h>

#define GUI_EVENT_QUEUE_SIZE 256

enum gui_event_type {
  GUI_EVENT_NONE = 0,
  GUI_EVENT_KEY_DOWN,
  GUI_EVENT_KEY_UP,
  GUI_EVENT_MOUSE_MOVE,
  GUI_EVENT_MOUSE_DOWN,
  GUI_EVENT_MOUSE_UP,
  GUI_EVENT_MOUSE_SCROLL,
  GUI_EVENT_WINDOW_CLOSE,
  GUI_EVENT_WINDOW_RESIZE,
  GUI_EVENT_WINDOW_FOCUS,
  GUI_EVENT_WINDOW_BLUR,
  GUI_EVENT_PAINT,
  GUI_EVENT_TIMER
};

struct gui_event {
  enum gui_event_type type;
  uint32_t window_id;
  union {
    struct { uint32_t keycode; uint8_t modifiers; char ch; } key;
    struct { int32_t x, y; int16_t dx, dy; uint8_t buttons; } mouse;
    struct { int32_t width, height; } resize;
    struct { uint32_t timer_id; } timer;
  };
  uint64_t timestamp;
};

struct gui_event_snapshot {
  uint32_t capacity;
  uint32_t pending;
  uint32_t space_available;
  uint32_t high_watermark;
  uint64_t dropped_total;
};

typedef void (*gui_event_dispatch_fn)(const struct gui_event *ev, void *ctx);

void gui_event_init(void);
int gui_event_push(const struct gui_event *ev);
int gui_event_push_key(uint32_t window_id, uint32_t keycode,
                       uint8_t modifiers, char ch, uint64_t timestamp);
int gui_event_push_key_up(uint32_t window_id, uint32_t keycode,
                          uint8_t modifiers, char ch, uint64_t timestamp);
int gui_event_push_mouse_move(int32_t x, int32_t y, int16_t dx, int16_t dy,
                              uint8_t buttons, uint64_t timestamp);
int gui_event_push_mouse_button(int down, int32_t x, int32_t y,
                                uint8_t buttons, uint64_t timestamp);
int gui_event_push_mouse_scroll(int32_t x, int32_t y, int16_t dz,
                                uint8_t buttons, uint64_t timestamp);
int gui_event_push_window_close(uint32_t window_id, uint64_t timestamp);
int gui_event_push_window_resize(uint32_t window_id, int32_t width,
                                 int32_t height, uint64_t timestamp);
int gui_event_push_window_focus(uint32_t window_id, uint64_t timestamp);
int gui_event_push_window_blur(uint32_t window_id, uint64_t timestamp);
int gui_event_push_paint(uint32_t window_id, uint64_t timestamp);
int gui_event_push_timer(uint32_t window_id, uint32_t timer_id,
                         uint64_t timestamp);
int gui_event_poll(struct gui_event *ev);
/* Remove the oldest event addressed to `window_id` without consuming or
 * reordering events for any other window.  This is the ownership-aware poll
 * used by ring-3 graphical clients: the desktop dispatcher and multiple
 * clients share the physical queue, so a client must never steal a foreign
 * event merely because it happened to be at the FIFO head.  `window_id == 0`
 * is rejected deliberately; global/raw input belongs to the desktop router,
 * which resolves it to a concrete target before a user process can consume
 * it. Returns 0 when an event was removed, -1 when none matched/arguments are
 * invalid. */
int gui_event_poll_window(uint32_t window_id, struct gui_event *ev);
uint32_t gui_event_poll_many(struct gui_event *out, uint32_t max);
uint32_t gui_event_peek_many(struct gui_event *out, uint32_t max);
uint32_t gui_event_dispatch(gui_event_dispatch_fn dispatch, void *ctx,
                            uint32_t max_events);
int gui_event_peek(struct gui_event *ev);
int gui_event_pending(void);
int gui_event_ready(void);
void gui_event_flush(void);
uint32_t gui_event_discard_window(uint32_t window_id);
uint32_t gui_event_capacity(void);
uint32_t gui_event_space_available(void);
int gui_event_full(void);
uint64_t gui_event_dropped_total(void);
int gui_event_snapshot(struct gui_event_snapshot *out);
void gui_event_reset_diagnostics(void);

#endif /* GUI_EVENT_H */
