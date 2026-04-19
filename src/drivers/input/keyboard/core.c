/* keyboard/core.c: IRQ handler and scancode translation with dead keys.
 * Supports Shift, AltGr (Right Alt), Caps Lock, Ctrl, arrow keys, F-keys,
 * Home/End/PgUp/PgDn, Insert/Delete. */
#include <stddef.h>
#include <stdint.h>

#include "drivers/irq.h"
#include "drivers/io.h"
#include "drivers/console/tty.h"
#include "drivers/input/keyboard.h"
#include "drivers/input/keyboard_compose.h"
#include "drivers/input/keyboard_layout.h"
#include "security/csprng.h"

static const struct keyboard_layout *g_layouts[2];
static int g_layouts_initialized = 0;

static const struct keyboard_layout *current_layout = NULL;
static int shift_on = 0;
static int ctrl_on = 0;
static int altgr_on = 0;
static int capslock_on = 0;
static int extended_prefix = 0;
static char g_dead_accent = 0;
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

static int is_dead_key(uint8_t sc, char ch, int shift_active, int altgr_active) {
  if (!current_layout) {
    return 0;
  }
  uint8_t flags = current_layout->dead[sc];
  int dead = 0;
  if (altgr_active)
    dead = (flags & 0x4);
  else if (shift_active)
    dead = (flags & 0x2);
  else
    dead = (flags & 0x1);
  if (!dead) {
    return 0;
  }
  return keyboard_compose_is_dead_accent(ch);
}

static int is_alpha(char c) {
  return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z');
}

static char toggle_case(char c) {
  if (c >= 'a' && c <= 'z') return (char)(c - 32);
  if (c >= 'A' && c <= 'Z') return (char)(c + 32);
  return c;
}

static char handle_extended_key(uint8_t sc) {
  switch (sc) {
  case 0x48: return (char)KEY_UP;
  case 0x50: return (char)KEY_DOWN;
  case 0x4B: return (char)KEY_LEFT;
  case 0x4D: return (char)KEY_RIGHT;
  case 0x47: return (char)KEY_HOME;
  case 0x4F: return (char)KEY_END;
  case 0x49: return (char)KEY_PGUP;
  case 0x51: return (char)KEY_PGDN;
  case 0x52: return (char)KEY_INSERT;
  case 0x53: return (char)KEY_DELETE;
  case 0x35: return '/';
  case 0x1C: return '\n';
  default:   return 0;
  }
}

static char handle_fkey(uint8_t sc) {
  if (sc >= 0x3B && sc <= 0x44)
    return (char)(KEY_F1 + (sc - 0x3B));
  if (sc == 0x57) return (char)KEY_F11;
  if (sc == 0x58) return (char)KEY_F12;
  return 0;
}

static void keyboard_irq(void) {
  uint8_t status = inb(0x64);
  if (!(status & 0x01)) return;
  if (status & 0x20) {
    extern void mouse_ps2_irq_handler(void);
    mouse_ps2_irq_handler();
    return;
  }
  uint8_t raw_sc = inb(0x60);
  int is_break = 0;
  int is_extended = 0;
  uint8_t sc;
  csprng_feed_entropy((uint32_t)raw_sc);

  if (raw_sc == 0xE0u) {
    extended_prefix = 1;
    return;
  }
  if (raw_sc == 0xE1u) {
    return;
  }

  is_break = (raw_sc & 0x80u) ? 1 : 0;
  sc = raw_sc & 0x7Fu;
  is_extended = extended_prefix;
  extended_prefix = 0;

  /* Modifier keys */
  if (sc == 0x2A || (!is_extended && sc == 0x36)) {
    shift_on = is_break ? 0 : 1;
    return;
  }
  if (!is_extended && sc == 0x1D) {
    ctrl_on = is_break ? 0 : 1;
    return;
  }
  if (is_extended && sc == 0x1D) {
    ctrl_on = is_break ? 0 : 1;
    return;
  }
  if (is_extended && sc == 0x38) {
    altgr_on = is_break ? 0 : 1;
    return;
  }
  if (!is_extended && sc == 0x38) {
    return;
  }
  if (sc == 0x3A && !is_break) {
    capslock_on = !capslock_on;
    return;
  }

  if (is_break) {
    return;
  }

  /* Extended keys: arrows, home, end, pgup, pgdn, ins, del, numpad enter */
  if (is_extended) {
    char special = handle_extended_key(sc);
    if (special) {
      tty_handle_char(special);
    }
    return;
  }

  /* F-keys */
  {
    char fk = handle_fkey(sc);
    if (fk) {
      if (sc == 0x3B && g_help_hotkey) {
        g_help_hotkey();
        return;
      }
      tty_handle_char(fk);
      return;
    }
  }

  /* Backspace / Enter */
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

  /* Ctrl combos */
  if (ctrl_on) {
    char base_ch = current_layout->base[sc];
    if (base_ch >= 'a' && base_ch <= 'z') {
      char ctrl_ch = (char)(base_ch - 'a' + 1);
      tty_handle_char(ctrl_ch);
      return;
    }
    return;
  }

  /* Select mapping: AltGr > Shift > Base */
  const char *mapping;
  int dead_layer = 0;
  if (altgr_on && current_layout->altgr[sc]) {
    mapping = current_layout->altgr;
    dead_layer = 2;
  } else if (shift_on) {
    mapping = current_layout->shift;
    dead_layer = 1;
  } else {
    mapping = current_layout->base;
    dead_layer = 0;
  }

  char ch = mapping[sc];
  char pending = 0;
  if (!ch) return;

  /* Caps Lock: toggle case for alphabetic characters */
  if (capslock_on && is_alpha(ch)) {
    ch = toggle_case(ch);
  }

  if (!keyboard_compose_step(&g_dead_accent, &pending, ch,
                             is_dead_key(sc, ch, dead_layer == 1, dead_layer == 2), &ch)) {
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
