#include "drivers/input/mouse.h"
#include <stddef.h>

#define PS2_DATA_PORT    0x60
#define PS2_STATUS_PORT  0x64
#define PS2_CMD_PORT     0x64

#define PS2_MOUSE_CMD_RESET       0xFF
#define PS2_MOUSE_CMD_ENABLE      0xF4
#define PS2_MOUSE_CMD_DISABLE     0xF5
#define PS2_MOUSE_CMD_SET_DEFAULT 0xF6
#define PS2_MOUSE_CMD_GET_ID      0xF2
#define PS2_MOUSE_CMD_SET_RATE    0xF3

#define MOUSE_EVENT_QUEUE_SIZE 256

static struct mouse_state mouse_current;
static struct mouse_event event_queue[MOUSE_EVENT_QUEUE_SIZE];
static uint32_t event_head = 0;
static uint32_t event_tail = 0;
static uint32_t event_count = 0;
static uint8_t mouse_packet[4];
static int mouse_packet_idx = 0;
static int mouse_has_wheel = 0;
static int mouse_inited = 0;

static inline void ps2_outb(uint16_t port, uint8_t val) {
  __asm__ volatile("outb %0, %1" : : "a"(val), "Nd"(port));
}

static inline uint8_t ps2_inb(uint16_t port) {
  uint8_t ret;
  __asm__ volatile("inb %1, %0" : "=a"(ret) : "Nd"(port));
  return ret;
}

static void ps2_wait_write(void) {
  for (int i = 0; i < 100000; i++) {
    if (!(ps2_inb(PS2_STATUS_PORT) & 0x02)) return;
  }
}

static void ps2_wait_read(void) {
  for (int i = 0; i < 100000; i++) {
    if (ps2_inb(PS2_STATUS_PORT) & 0x01) return;
  }
}

static void ps2_mouse_write(uint8_t data) {
  ps2_wait_write();
  ps2_outb(PS2_CMD_PORT, 0xD4);
  ps2_wait_write();
  ps2_outb(PS2_DATA_PORT, data);
  ps2_wait_read();
  ps2_inb(PS2_DATA_PORT);
}

static uint8_t ps2_mouse_read(void) {
  ps2_wait_read();
  return ps2_inb(PS2_DATA_PORT);
}

static void mouse_push_event(const struct mouse_event *ev) {
  if (!ev) return;
  if (event_count >= MOUSE_EVENT_QUEUE_SIZE) {
    event_tail = (event_tail + 1) % MOUSE_EVENT_QUEUE_SIZE;
    event_count--;
  }
  event_queue[event_head] = *ev;
  event_head = (event_head + 1) % MOUSE_EVENT_QUEUE_SIZE;
  event_count++;
}

void mouse_ps2_init(void) {
  ps2_wait_write();
  ps2_outb(PS2_CMD_PORT, 0xA8);

  ps2_wait_write();
  ps2_outb(PS2_CMD_PORT, 0x20);
  ps2_wait_read();
  uint8_t status = ps2_inb(PS2_DATA_PORT);
  status |= 0x02;
  status &= ~0x20;
  ps2_wait_write();
  ps2_outb(PS2_CMD_PORT, 0x60);
  ps2_wait_write();
  ps2_outb(PS2_DATA_PORT, status);

  ps2_mouse_write(PS2_MOUSE_CMD_SET_DEFAULT);
  ps2_mouse_write(PS2_MOUSE_CMD_ENABLE);

  ps2_mouse_write(PS2_MOUSE_CMD_SET_RATE); ps2_mouse_write(200);
  ps2_mouse_write(PS2_MOUSE_CMD_SET_RATE); ps2_mouse_write(100);
  ps2_mouse_write(PS2_MOUSE_CMD_SET_RATE); ps2_mouse_write(80);
  ps2_mouse_write(PS2_MOUSE_CMD_GET_ID);
  uint8_t id = ps2_mouse_read();
  mouse_has_wheel = (id == 3 || id == 4) ? 1 : 0;

  mouse_current.x = 0;
  mouse_current.y = 0;
  mouse_current.buttons = 0;
  mouse_current.screen_width = 800;
  mouse_current.screen_height = 600;
  mouse_current.initialized = 1;
  mouse_packet_idx = 0;
  mouse_inited = 1;
}

void mouse_ps2_irq_handler(void) {
  uint8_t data = ps2_inb(PS2_DATA_PORT);
  if (!mouse_inited) return;

  int packet_size = mouse_has_wheel ? 4 : 3;

  if (mouse_packet_idx == 0 && !(data & 0x08)) {
    return;
  }

  mouse_packet[mouse_packet_idx++] = data;

  if (mouse_packet_idx < packet_size) return;
  mouse_packet_idx = 0;

  if (!(mouse_packet[0] & 0x08)) return;

  struct mouse_event ev;
  ev.buttons = mouse_packet[0] & 0x07;
  ev.changed = ev.buttons ^ mouse_current.buttons;

  int16_t dx = (int16_t)mouse_packet[1];
  int16_t dy = (int16_t)mouse_packet[2];
  if (mouse_packet[0] & 0x10) dx |= (int16_t)0xFF00;
  if (mouse_packet[0] & 0x20) dy |= (int16_t)0xFF00;
  dy = -dy;

  ev.dx = dx;
  ev.dy = dy;
  ev.dz = mouse_has_wheel ? (int8_t)(mouse_packet[3] & 0x0F) : 0;
  if (ev.dz & 0x08) ev.dz |= (int8_t)0xF0;

  mouse_current.buttons = ev.buttons;
  mouse_current.x += dx;
  mouse_current.y += dy;

  if (mouse_current.x < 0) mouse_current.x = 0;
  if (mouse_current.y < 0) mouse_current.y = 0;
  if (mouse_current.x >= mouse_current.screen_width)
    mouse_current.x = mouse_current.screen_width - 1;
  if (mouse_current.y >= mouse_current.screen_height)
    mouse_current.y = mouse_current.screen_height - 1;

  mouse_push_event(&ev);
}

void mouse_ps2_poll(void) {
  if (!mouse_inited) return;
  /* Drain every pending PS/2 mouse byte in one shot.  The keyboard polling
   * path (ps2_poll_scancode) only consumes one byte per call and stops as
   * soon as a mouse byte is seen, which would otherwise spread a single
   * 3-4 byte packet over several frames and leave clicks feeling dead. */
  for (int i = 0; i < 64; i++) {
    uint8_t status = ps2_inb(PS2_STATUS_PORT);
    if (!(status & 0x01)) return;       /* output buffer empty */
    if (!(status & 0x20)) return;       /* keyboard byte — leave it */
    mouse_ps2_irq_handler();
  }
}

int mouse_poll(struct mouse_event *event) {
  if (!event || event_count == 0) return -1;
  *event = event_queue[event_tail];
  event_tail = (event_tail + 1) % MOUSE_EVENT_QUEUE_SIZE;
  event_count--;
  return 0;
}

void mouse_get_state(struct mouse_state *out) {
  if (out) *out = mouse_current;
}

void mouse_set_bounds(int32_t width, int32_t height) {
  mouse_current.screen_width = width;
  mouse_current.screen_height = height;
}

void mouse_set_position(int32_t x, int32_t y) {
  mouse_current.x = x;
  mouse_current.y = y;
}

int mouse_available(void) {
  return mouse_inited;
}

int mouse_pending(void) {
  return event_count != 0;
}
