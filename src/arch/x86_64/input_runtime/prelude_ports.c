#include "internal/input_runtime_internal.h"

int g_has_com1 = -1;
int g_has_ps2_active = -1;
int g_has_ps2_passive = -1;
int g_hyperv_keyboard_auto_promotion_enabled = 0;
int g_hyperv_keyboard_auto_promotion_logged = 0;

void x64_input_enable_auto_promotion(void) {
  g_hyperv_keyboard_auto_promotion_enabled = 1;
  g_hyperv_keyboard_auto_promotion_logged = 0;
}

int com1_detect(void) {
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

