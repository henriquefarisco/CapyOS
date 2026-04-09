#ifndef DRIVERS_INPUT_MOUSE_H
#define DRIVERS_INPUT_MOUSE_H

#include <stdint.h>

#define MOUSE_BUTTON_LEFT   0x01
#define MOUSE_BUTTON_RIGHT  0x02
#define MOUSE_BUTTON_MIDDLE 0x04

struct mouse_event {
  int16_t dx;
  int16_t dy;
  int8_t  dz;
  uint8_t buttons;
  uint8_t changed;
};

struct mouse_state {
  int32_t x;
  int32_t y;
  uint8_t buttons;
  int32_t screen_width;
  int32_t screen_height;
  int initialized;
};

void mouse_ps2_init(void);
void mouse_ps2_irq_handler(void);
int mouse_poll(struct mouse_event *event);
void mouse_get_state(struct mouse_state *out);
void mouse_set_bounds(int32_t width, int32_t height);
void mouse_set_position(int32_t x, int32_t y);
int mouse_available(void);

#endif /* DRIVERS_INPUT_MOUSE_H */
