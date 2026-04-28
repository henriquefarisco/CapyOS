#ifndef INPUT_RUNTIME_INTERNAL_H
#define INPUT_RUNTIME_INTERNAL_H

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

extern int g_has_ps2_active;
extern int g_has_ps2_passive;
extern int g_hyperv_keyboard_auto_promotion_enabled;
extern int g_hyperv_keyboard_auto_promotion_logged;

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

static inline void runtime_print(void (*print)(const char *),
                                  const char *message) {
  if (print && message) print(message);
}

extern void mouse_ps2_irq_handler(void);

int com1_detect(void);
int ps2_poll_scancode(uint8_t *out_scancode);
int com1_poll_char(char *out_char);
char scancode_to_ascii(struct x64_input_runtime *runtime, uint8_t sc,
                       int shift);
uint8_t normalize_extended_scancode(uint8_t scancode);
int decode_prefixed_scancode(int *prefix_flag, uint8_t raw_scancode,
                             uint8_t *out_scancode, int *out_break);
int translate_efi_char_to_active_layout(struct x64_input_runtime *runtime,
                                        char in, char *out);
int ps2_controller_detect_passive(void);
int ps2_controller_detect_active(void);
void append_backend(struct x64_input_runtime *runtime,
                    enum x64_input_backend backend);
void prepend_backend(struct x64_input_runtime *runtime,
                     enum x64_input_backend backend);
void refresh_primary_backend(struct x64_input_runtime *runtime);
void retire_firmware_backend(struct x64_input_runtime *runtime);
void park_ps2_fallback(struct x64_input_runtime *runtime);
void defer_hyperv_backend(struct x64_input_runtime *runtime);

#endif
