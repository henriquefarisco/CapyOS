#ifndef GUI_NOTIFICATION_H
#define GUI_NOTIFICATION_H

#include <stdint.h>
#include "gui/compositor.h"

#define NOTIFY_MAX 8
#define NOTIFY_TEXT_MAX 128
#define NOTIFY_WIDTH 280
#define NOTIFY_HEIGHT 60
#define NOTIFY_DURATION_TICKS 300

enum notify_type {
  NOTIFY_INFO = 0,
  NOTIFY_SUCCESS,
  NOTIFY_WARNING,
  NOTIFY_ERROR
};

struct notification {
  char text[NOTIFY_TEXT_MAX];
  enum notify_type type;
  uint32_t remaining_ticks;
  int active;
};

struct notification_manager {
  struct notification items[NOTIFY_MAX];
  uint32_t screen_w;
  uint32_t screen_h;
};

void notify_init(struct notification_manager *nm, uint32_t sw, uint32_t sh);
void notify_push(struct notification_manager *nm, const char *text, enum notify_type type);
void notify_tick(struct notification_manager *nm);
void notify_paint(struct notification_manager *nm, struct gui_surface *surface);

#endif
