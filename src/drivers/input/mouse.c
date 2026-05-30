#include "drivers/input/mouse.h"
#ifdef CAPYOS_EXPERIMENTAL_HYPERV_MOUSE
#include "drivers/hyperv/hyperv.h"
#endif
#include "drivers/usb/usb_hid.h"
#include "security/csprng.h"
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
#ifdef UNIT_TEST
  (void)port;
  (void)val;
#else
  __asm__ volatile("outb %0, %1" : : "a"(val), "Nd"(port));
#endif
}

static inline uint8_t ps2_inb(uint16_t port) {
#ifdef UNIT_TEST
  (void)port;
  return 0xFFu;
#else
  uint8_t ret;
  __asm__ volatile("inb %1, %0" : "=a"(ret) : "Nd"(port));
  return ret;
#endif
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

static void mouse_ensure_bounds(void) {
  if (mouse_current.screen_width <= 0) mouse_current.screen_width = 800;
  if (mouse_current.screen_height <= 0) mouse_current.screen_height = 600;
}

static void mouse_apply_event(const struct mouse_event *ev) {
  if (!ev) return;
  mouse_ensure_bounds();
  mouse_current.buttons = ev->buttons;
  mouse_current.x += ev->dx;
  mouse_current.y += ev->dy;
  if (mouse_current.x < 0) mouse_current.x = 0;
  if (mouse_current.y < 0) mouse_current.y = 0;
  if (mouse_current.x >= mouse_current.screen_width)
    mouse_current.x = mouse_current.screen_width - 1;
  if (mouse_current.y >= mouse_current.screen_height)
    mouse_current.y = mouse_current.screen_height - 1;
  mouse_current.initialized = 1;
  mouse_push_event(ev);
}

/* Defensive probe before bring-up.  Hyper-V Generation 2 VMs have no
 * legacy PS/2 controller, and a "missing controller" reads as 0xFF on
 * port 0x64.  Without this gate, mouse_ps2_init would still flag the
 * driver as initialised, the IRQ/poll path would read 0xFF as the
 * status byte (bit 0 = 1, bit 5 = 1 → "mouse byte ready"), and the
 * packet decoder would interpret each phantom 0xFF triple as
 * dx=-1, dy=+1 → the diagonal cursor drift observed in the field.
 * Returns 1 if a real controller is present, 0 otherwise. */
static int mouse_ps2_controller_present(void) {
  uint8_t status = ps2_inb(PS2_STATUS_PORT);
  if (status == 0xFF) return 0;
  for (int i = 0; i < 100000; i++) {
    if (!(ps2_inb(PS2_STATUS_PORT) & 0x02)) break;
  }
  ps2_outb(PS2_CMD_PORT, 0x20);
  for (int i = 0; i < 100000; i++) {
    if (ps2_inb(PS2_STATUS_PORT) & 0x01) break;
  }
  uint8_t cfg = ps2_inb(PS2_DATA_PORT);
  return cfg != 0xFF;
}

void mouse_ps2_init(void) {
  mouse_inited = 0;
  mouse_current.initialized = 0;

  if (!mouse_ps2_controller_present()) {
    /* No PS/2 controller (typical of Hyper-V Gen2 and modern laptops).
     * Leave the driver disabled; the USB HID and VMBus SynthHID
     * backends are responsible for cursor input on those platforms. */
    return;
  }

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
  /* 0xFF is the "no response" value when the controller is absent
   * but the early probe was inconclusive (e.g. SMM-emulated PS/2 on
   * some firmware).  Refuse to initialise in that case; the IRQ path
   * would otherwise feed phantom packets into the event queue. */
  if (id == 0xFFu) {
    return;
  }
  mouse_has_wheel = (id == 3 || id == 4) ? 1 : 0;

  mouse_current.x = 0;
  mouse_current.y = 0;
  mouse_current.buttons = 0;
  /* P1-E fix (2026-05-25): the hardcoded 800×600 initial bounds were
   * removed because they overwrote any earlier `mouse_set_bounds(W, H)`
   * call (the framebuffer dimensions are typically published before
   * the PS/2 controller probe completes). The `mouse_ensure_bounds()`
   * helper applies the same 800×600 default lazily on the first
   * `mouse_apply_event` only when no real bounds have been published
   * yet, so this init no longer clobbers the real framebuffer
   * dimensions. The cursor position and button state are still zeroed
   * so an emulated phantom packet cannot inherit a stale state. */
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
/* Cada byte de pacote PS/2 do mouse contem timing humano residual
   * (intervalos entre movimentos do mouse sao nao-deterministicos e
   * variam por sub-milissegundos entre interrupcoes consecutivas).
   * Alimentar o CSPRNG com esses bytes engrossa a entropia de pool
   * em qualquer sessao com mouse ativo, sem custo adicional alem da
   * propria invocacao da ISR (que ja estava no caminho). */
  csprng_feed_entropy((uint32_t)data);


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

  mouse_apply_event(&ev);
}

static void mouse_usb_hid_poll(void) {
  int8_t dx = 0;
  int8_t dy = 0;
  int8_t dz = 0;
  uint8_t buttons = 0;
  struct mouse_event ev;

  if (!usb_hid_mouse_available()) return;
  mouse_ensure_bounds();
  mouse_current.initialized = 1;
  if (usb_hid_mouse_poll(&dx, &dy, &dz, &buttons) != 1) return;
  buttons &= (MOUSE_BUTTON_LEFT | MOUSE_BUTTON_RIGHT | MOUSE_BUTTON_MIDDLE);
  if (dx == 0 && dy == 0 && dz == 0 && buttons == mouse_current.buttons) {
    return;
  }
  ev.dx = dx;
  ev.dy = dy;
  ev.dz = dz;
  ev.buttons = buttons;
  ev.changed = buttons ^ mouse_current.buttons;
  csprng_feed_entropy(((uint32_t)(uint8_t)buttons << 24) |
                      ((uint32_t)(uint8_t)dx << 16) |
                      ((uint32_t)(uint8_t)dy << 8) |
                      (uint32_t)(uint8_t)dz);
  mouse_apply_event(&ev);
}

#if !defined(UNIT_TEST) && defined(CAPYOS_EXPERIMENTAL_HYPERV_MOUSE)
static int mouse_scale_hyperv_abs(uint16_t value, int32_t limit,
                                  uint32_t denominator) {
  if (limit <= 1) return 0;
  if (denominator == 0u) denominator = 0x7FFFu;
  return (int)(((uint32_t)value * (uint32_t)(limit - 1) +
                (denominator / 2u)) /
               denominator);
}
#endif

static void mouse_hyperv_poll(void) {
#if !defined(UNIT_TEST) && defined(CAPYOS_EXPERIMENTAL_HYPERV_MOUSE)
  struct hyperv_mouse_report report;
  struct mouse_event ev;

  if (!hyperv_mouse_available()) return;
  mouse_ensure_bounds();
  mouse_current.initialized = 1;
  if (hyperv_mouse_poll(&report) != 1) return;
  ev.buttons =
      report.buttons & (MOUSE_BUTTON_LEFT | MOUSE_BUTTON_RIGHT | MOUSE_BUTTON_MIDDLE);
  ev.dz = report.dz;
  if (report.absolute) {
    uint32_t denominator =
        (report.abs_x > 0x7FFFu || report.abs_y > 0x7FFFu) ? 0xFFFFu : 0x7FFFu;
    int32_t target_x =
        mouse_scale_hyperv_abs(report.abs_x, mouse_current.screen_width,
                               denominator);
    int32_t target_y =
        mouse_scale_hyperv_abs(report.abs_y, mouse_current.screen_height,
                               denominator);
    ev.dx = (int16_t)(target_x - mouse_current.x);
    ev.dy = (int16_t)(target_y - mouse_current.y);
  } else {
    ev.dx = report.dx;
    ev.dy = report.dy;
  }
  ev.changed = ev.buttons ^ mouse_current.buttons;
  if (ev.dx == 0 && ev.dy == 0 && ev.dz == 0 && ev.changed == 0) return;
  csprng_feed_entropy(((uint32_t)(uint8_t)ev.buttons << 24) |
                      ((uint32_t)(uint8_t)ev.dx << 16) |
                      ((uint32_t)(uint8_t)ev.dy << 8) |
                      (uint32_t)(uint8_t)ev.dz);
  mouse_apply_event(&ev);
#endif
}

void mouse_ps2_poll(void) {
  mouse_hyperv_poll();
  mouse_usb_hid_poll();
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
  if (mouse_inited || usb_hid_mouse_available()) return 1;
#if !defined(UNIT_TEST) && defined(CAPYOS_EXPERIMENTAL_HYPERV_MOUSE)
  return hyperv_mouse_available();
#else
  return 0;
#endif
}

int mouse_pending(void) {
  return event_count != 0;
}
