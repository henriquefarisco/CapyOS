#include "internal/input_runtime_internal.h"

int ps2_poll_scancode(uint8_t *out_scancode) {
  if (!out_scancode) {
    return 0;
  }
  uint8_t status = inb_local(0x64);
  if (!(status & 0x01u)) {
    return 0;
  }
  if (status & 0x20u) {
    mouse_ps2_irq_handler();
    return 0;
  }
  *out_scancode = inb_local(0x60);
  return 1;
}

int com1_poll_char(char *out_char) {
  if (!out_char) {
    return 0;
  }
  if (inb_local(0x3F8 + 5) & 0x01u) {
    *out_char = (char)inb_local(0x3F8);
    return 1;
  }
  return 0;
}

static int layout_name_equal(const char *a, const char *b) {
  if (!a || !b) {
    return 0;
  }
  while (*a && *b) {
    if (*a++ != *b++) {
      return 0;
    }
  }
  return *a == *b;
}

static const struct keyboard_layout *active_layout_map(void) {
  const char *name = keyboard_current_layout();
  if (name && layout_name_equal(name, "br-abnt2")) {
    return &g_keyboard_layout_br_abnt2;
  }
  return &g_keyboard_layout_us;
}

static int find_us_scancode_for_char(char ch, uint8_t *out_sc, int *out_shift) {
  if (!out_sc || !out_shift) {
    return 0;
  }
  for (uint8_t sc = 0; sc < 128; ++sc) {
    if (g_keyboard_layout_us.base[sc] == ch && ch != 0) {
      *out_sc = sc;
      *out_shift = 0;
      return 1;
    }
    if (g_keyboard_layout_us.shift[sc] == ch && ch != 0) {
      *out_sc = sc;
      *out_shift = 1;
      return 1;
    }
  }
  return 0;
}

char scancode_to_ascii(struct x64_input_runtime *runtime, uint8_t sc,
                              int shift) {
  const struct keyboard_layout *layout = active_layout_map();
  if (!runtime || sc >= 128 || !layout) {
    return 0;
  }

  const char *mapping = shift ? layout->shift : layout->base;
  char ch = mapping[sc];
  if (!ch) {
    return 0;
  }

  uint8_t dead_flags = layout->dead[sc];
  int is_dead = shift ? ((dead_flags & 0x2) != 0) : ((dead_flags & 0x1) != 0);
  if (!keyboard_compose_step(&runtime->dead_accent, &runtime->pending_char, ch,
                             is_dead, &ch)) {
    return 0;
  }
  return ch;
}

uint8_t normalize_extended_scancode(uint8_t scancode) {
  if (scancode == 0x35u) {
    return 0x73u;
  }
  return scancode;
}

int decode_prefixed_scancode(int *prefix_flag, uint8_t raw_scancode,
                                    uint8_t *out_scancode, int *out_break) {
  if (!prefix_flag || !out_scancode || !out_break) {
    return 0;
  }
  if (raw_scancode == 0xE0u) {
    *prefix_flag = 1;
    return 0;
  }

  *out_break = (raw_scancode & 0x80u) ? 1 : 0;
  raw_scancode &= 0x7Fu;
  if (*prefix_flag) {
    raw_scancode = normalize_extended_scancode(raw_scancode);
  }
  /* NOTE: prefix_flag is intentionally left set until after the caller
   * has a chance to check it for arrow-key detection.  The caller clears
   * it after use. */
  *out_scancode = raw_scancode;
  return 1;
}

int translate_efi_char_to_active_layout(struct x64_input_runtime *runtime,
                                               char in, char *out) {
  const struct keyboard_layout *layout = active_layout_map();
  uint8_t sc = 0;
  int shift = 0;
  char mapped = 0;
  uint8_t dead_flags = 0;
  int is_dead = 0;

  if (!runtime || !out) {
    return 0;
  }
  if (!layout || layout == &g_keyboard_layout_us) {
    *out = in;
    return 1;
  }
  if ((in == '/' || in == '?') && layout == &g_keyboard_layout_br_abnt2) {
    *out = in;
    return 1;
  }

  if (!find_us_scancode_for_char(in, &sc, &shift)) {
    *out = in;
    return 1;
  }

  mapped = (shift ? layout->shift : layout->base)[sc];
  if (!mapped) {
    *out = in;
    return 1;
  }

  dead_flags = layout->dead[sc];
  is_dead = shift ? ((dead_flags & 0x2) != 0) : ((dead_flags & 0x1) != 0);
  return keyboard_compose_step(&runtime->dead_accent, &runtime->pending_char,
                               mapped, is_dead, out);
}

static void ps2_flush(void) {
  uint8_t discarded = 0;
  for (uint32_t i = 0; i < 1024u; ++i) {
    if (!ps2_poll_scancode(&discarded)) {
      break;
    }
  }
}

static int ps2_wait_input_empty(uint32_t spins) {
  for (uint32_t i = 0; i < spins; ++i) {
    if ((inb_local(0x64) & 0x02u) == 0u) {
      return 1;
    }
    cpu_relax_local();
  }
  return 0;
}

static int ps2_wait_output_ready(uint32_t spins) {
  for (uint32_t i = 0; i < spins; ++i) {
    if (inb_local(0x64) & 0x01u) {
      return 1;
    }
    cpu_relax_local();
  }
  return 0;
}

static int ps2_read_config_byte(uint8_t *out_cfg) {
  if (!out_cfg) {
    return 0;
  }

  if (inb_local(0x64) == 0xFFu) {
    return 0;
  }

  if (!ps2_wait_input_empty(10000u)) {
    return 0;
  }

  outb_local(0x64, 0x20u);
  if (!ps2_wait_output_ready(10000u)) {
    return 0;
  }

  *out_cfg = inb_local(0x60);
  return 1;
}

int ps2_controller_detect_passive(void) {
  uint8_t cfg = 0;

  if (g_has_ps2_passive >= 0) {
    return g_has_ps2_passive;
  }

  if (ps2_read_config_byte(&cfg) && cfg != 0xFFu) {
    g_has_ps2_passive = 1;
    return 1;
  }

  g_has_ps2_passive = 0;
  return 0;
}

int ps2_controller_detect_active(void) {
  if (g_has_ps2_active >= 0) {
    return g_has_ps2_active;
  }

  if (ps2_controller_detect_passive()) {
    g_has_ps2_active = 1;
    return 1;
  }

  ps2_flush();
  if (!ps2_wait_input_empty(10000u)) {
    g_has_ps2_active = 0;
    return 0;
  }

  outb_local(0x64, 0xAAu);
  if (ps2_wait_output_ready(100000u) && inb_local(0x60) == 0x55u) {
    g_has_ps2_active = 1;
    return 1;
  }

  g_has_ps2_active = 0;
  return 0;
}

