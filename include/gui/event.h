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

void gui_event_init(void);
int gui_event_push(const struct gui_event *ev);
int gui_event_poll(struct gui_event *ev);
int gui_event_peek(struct gui_event *ev);
int gui_event_pending(void);
void gui_event_flush(void);

#endif /* GUI_EVENT_H */
