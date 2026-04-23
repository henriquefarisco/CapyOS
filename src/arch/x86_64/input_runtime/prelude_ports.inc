#include "arch/x86_64/input_runtime.h"

#include <stddef.h>
#include <stdint.h>

#include "drivers/efi/efi_console.h"
#include "drivers/hyperv/hyperv.h"
#include "drivers/input/keyboard.h"
#include "drivers/input/keyboard_compose.h"
#include "drivers/input/keyboard_layout.h"
#include "drivers/timer/pit.h"
#include "drivers/usb/xhci.h"

enum {
  HYPERV_PROMOTION_RETRY_DELAY_TICKS = 100u,
  HYPERV_PROMOTION_MAX_ATTEMPTS = 3u,
  HYPERV_STABLE_PARK_PS2_EVENTS = 8u,
};

static int g_has_com1 = -1;
static int g_has_ps2_active = -1;
static int g_has_ps2_passive = -1;
static int g_hyperv_keyboard_auto_promotion_enabled = 0;
static int g_hyperv_keyboard_auto_promotion_logged = 0;

void x64_input_enable_auto_promotion(void) {
  g_hyperv_keyboard_auto_promotion_enabled = 1;
  g_hyperv_keyboard_auto_promotion_logged = 0;
}

static inline void outb_local(uint16_t port, uint8_t value) {
  __asm__ volatile("outb %0, %1" : : "a"(value), "Nd"(port));
}

static inline uint8_t inb_local(uint16_t port) {
  uint8_t value = 0;
  __asm__ volatile("inb %1, %0" : "=a"(value) : "Nd"(port));
  return value;
}

static inline void cpu_relax_local(void) {
  __asm__ volatile("pause" ::: "memory");
}

static void runtime_print(void (*print)(const char *), const char *message) {
  if (print && message) {
    print(message);
  }
}

static int com1_detect(void) {
  if (g_has_com1 >= 0) {
    return g_has_com1;
  }

  outb_local(0x3F8 + 4, 0x1Eu);
  outb_local(0x3F8, 0xAEu);

  for (int i = 0; i < 10000; ++i) {
    if (inb_local(0x3F8 + 5) & 0x01u) {
      uint8_t received = inb_local(0x3F8);
      outb_local(0x3F8 + 4, 0x0Bu);
      if (received == 0xAEu) {
        g_has_com1 = 1;
        return 1;
      }
      break;
    }
    cpu_relax_local();
  }

  outb_local(0x3F8 + 4, 0x0Bu);
  g_has_com1 = 0;
  return 0;
}

extern void mouse_ps2_irq_handler(void);

