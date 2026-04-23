#include "gui/event.h"
#include <stddef.h>

static struct gui_event event_queue[GUI_EVENT_QUEUE_SIZE];
static uint32_t eq_head = 0;
static uint32_t eq_tail = 0;
static uint32_t eq_count = 0;

void gui_event_init(void) {
  eq_head = 0;
  eq_tail = 0;
  eq_count = 0;
}

int gui_event_push(const struct gui_event *ev) {
  if (!ev || eq_count >= GUI_EVENT_QUEUE_SIZE) return -1;
  event_queue[eq_head] = *ev;
  eq_head = (eq_head + 1) % GUI_EVENT_QUEUE_SIZE;
  eq_count++;
  return 0;
}

int gui_event_poll(struct gui_event *ev) {
  if (!ev || eq_count == 0) return -1;
  *ev = event_queue[eq_tail];
  eq_tail = (eq_tail + 1) % GUI_EVENT_QUEUE_SIZE;
  eq_count--;
  return 0;
}

int gui_event_peek(struct gui_event *ev) {
  if (!ev || eq_count == 0) return -1;
  *ev = event_queue[eq_tail];
  return 0;
}

int gui_event_pending(void) {
  return (int)eq_count;
}

void gui_event_flush(void) {
  eq_head = 0;
  eq_tail = 0;
  eq_count = 0;
}
