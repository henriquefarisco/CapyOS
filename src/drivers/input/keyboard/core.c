/* keyboard/core.c: IRQ handler and scancode translation with dead keys. */
#include <stddef.h>
#include <stdint.h>

#include "arch/x86/cpu/isr.h"
#include "arch/x86/hw/io.h"
#include "drivers/console/tty.h"
#include "drivers/input/keyboard.h"
#include "drivers/input/keyboard_compose.h"
#include "drivers/input/keyboard_layout.h"
#include "security/csprng.h"

static const struct keyboard_layout *g_layouts[2];
static int g_layouts_initialized = 0;

static const struct keyboard_layout *current_layout = NULL;
static int shift_on = 0;
static int extended_prefix = 0;
static char g_dead_accent = 0; // '\'', '`', '^', '~', '"' for diaeresis
static keyboard_hotkey_callback g_help_hotkey = NULL;

static void keyboard_init_layout_table(void) {
  if (g_layouts_initialized) {
    return;
  }
  g_layouts[0] = &g_keyboard_layout_us;
  g_layouts[1] = &g_keyboard_layout_br_abnt2;
  g_layouts_initialized = 1;
}

static void keyboard_apply_layout(const struct keyboard_layout *layout) {
  if (layout) {
    current_layout = layout;
    g_dead_accent = 0;
  }
}

static int is_dead_key(uint8_t sc, char ch, int shift_active) {
  if (!current_layout) {
    return 0;
  }
  uint8_t flags = current_layout->dead[sc];
  int dead = shift_active ? (flags & 0x2) : (flags & 0x1);
  if (!dead) {
    return 0;
  }
  return keyboard_compose_is_dead_accent(ch);
}

static uint8_t normalize_extended_scancode(uint8_t scancode) {
  if (scancode == 0x35u) {
    return 0x73u;
  }
  return scancode;
}

static void keyboard_irq(void) {
  uint8_t sc = inb(0x60);
  int is_break = 0;
  csprng_feed_entropy((uint32_t)sc);

  if (sc == 0xE0u) {
    extended_prefix = 1;
    return;
  }

  is_break = (sc & 0x80u) ? 1 : 0;
  sc &= 0x7Fu;
  if (extended_prefix) {
    sc = normalize_extended_scancode(sc);
    extended_prefix = 0;
  }

  if (sc == 0x2A || sc == 0x36) {
    shift_on = is_break ? 0 : 1;
    return;
  }
  if (is_break) {
    return;
  }

  if (sc == 0x3B) { // F1 - help
    if (g_help_hotkey) {
      g_help_hotkey();
    }
    return;
  }

  if (sc == 0x0E) {
    tty_handle_backspace();
    return;
  }
  if (sc == 0x1C) {
    tty_handle_enter();
    return;
  }

  if (!current_layout || sc >= 128) {
    return;
  }

  const char *mapping = shift_on ? current_layout->shift : current_layout->base;
  char ch = mapping[sc];
  char pending = 0;
  if (!ch)
    return;

  if (!keyboard_compose_step(&g_dead_accent, &pending, ch,
                             is_dead_key(sc, ch, shift_on), &ch)) {
    return;
  }

  tty_handle_char(ch);
  if (pending) {
    tty_handle_char(pending);
  }
}

void keyboard_init(void) {
  keyboard_init_layout_table();
  keyboard_apply_layout(g_layouts[0]);
  irq_install_handler(1, keyboard_irq);
}

void keyboard_set_help_callback(keyboard_hotkey_callback cb) {
  g_help_hotkey = cb;
}

size_t keyboard_layout_count(void) {
  keyboard_init_layout_table();
  return sizeof(g_layouts) / sizeof(g_layouts[0]);
}

const char *keyboard_layout_name(size_t index) {
  switch (index) {
  case 0:
    return "us";
  case 1:
    return "br-abnt2";
  default:
    return NULL;
  }
}

const char *keyboard_layout_description(size_t index) {
  switch (index) {
  case 0:
    return "Layout US (ANSI) padrao";
  case 1:
    return "Layout Brasileiro ABNT2";
  default:
    return NULL;
  }
}

const char *keyboard_current_layout(void) {
  if (!current_layout) {
    return NULL;
  }
  if (current_layout == g_layouts[1]) {
    return "br-abnt2";
  }
  return "us";
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

int keyboard_set_layout_by_name(const char *name) {
  if (!name) {
    return -1;
  }
  keyboard_init_layout_table();
  if (g_layouts[0] && (layout_name_equal(name, "us") || layout_name_equal(name, "0"))) {
    keyboard_apply_layout(g_layouts[0]);
    return 0;
  }
  if (g_layouts[1] &&
      (layout_name_equal(name, "br-abnt2") || layout_name_equal(name, "1"))) {
    keyboard_apply_layout(g_layouts[1]);
    return 0;
  }
  return -1;
}

void keyboard_wait_any(void) {
  /* Espera ate que o buffer do teclado tenha dados (status bit 0 = output full)
   */
  while (!(inb(0x64) & 0x01)) {
    hlt(); /* Deixa CPU descansar entre polls */
  }
  /* Le e descarta o scancode */
  (void)inb(0x60);
}
