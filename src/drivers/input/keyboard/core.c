/* keyboard/core.c: IRQ handler and scancode translation with dead keys. */
#include <stddef.h>
#include <stdint.h>

#include "arch/x86/cpu/isr.h"
#include "arch/x86/hw/io.h"
#include "drivers/console/tty.h"
#include "drivers/input/keyboard.h"
#include "drivers/input/keyboard_layout.h"
#include "security/csprng.h"

static const struct keyboard_layout *g_layouts[] = {
    &g_keyboard_layout_us,
    &g_keyboard_layout_br_abnt2,
};

static const struct keyboard_layout *current_layout = NULL;
static int shift_on = 0;
static char g_dead_accent = 0; // '\'', '`', '^', '~', '"' for diaeresis
static keyboard_hotkey_callback g_help_hotkey = NULL;

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
  return ch == '\'' || ch == '`' || ch == '^' || ch == '~' || ch == '"';
}

static void keyboard_irq(void) {
  uint8_t sc = inb(0x60);
  csprng_feed_entropy((uint32_t)sc);

  if (sc == 0x2A || sc == 0x36) {
    shift_on = 1;
    return;
  }
  if (sc == 0xAA || sc == 0xB6) {
    shift_on = 0;
    return;
  }

  if (sc & 0x80) {
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
  if (!ch)
    return;

  if (is_dead_key(sc, ch, shift_on)) {
    g_dead_accent = ch;
    return;
  }

  // Handle dead keys for accents
  if (ch == '\'' || ch == '`' || ch == '^' || ch == '~' || ch == '"') {
    g_dead_accent = ch;
    return;
  }

  if (g_dead_accent) {
    char accent = g_dead_accent;
    g_dead_accent = 0;
    // Compose minimal PT-BR set; return 0 for fallback
    char composed = 0;
    if (accent == '\'') {
      if (ch == 'a')
        composed = (char)0xA0; // á
      else if (ch == 'e')
        composed = (char)0x82; // é
      else if (ch == 'i')
        composed = (char)0xA1; // í
      else if (ch == 'o')
        composed = (char)0xA2; // ó
      else if (ch == 'u')
        composed = (char)0xA3; // ú
      else if (ch == 'A')
        composed = (char)0xB5; // Á
      else if (ch == 'E')
        composed = (char)0x90; // É
      else if (ch == 'I')
        composed = (char)0xD6; // Í
      else if (ch == 'O')
        composed = (char)0xE0; // Ó
      else if (ch == 'U')
        composed = (char)0xE9; // Ú
    } else if (accent == '^') {
      if (ch == 'a')
        composed = (char)0x83; // â
      else if (ch == 'e')
        composed = (char)0x88; // ê
      else if (ch == 'i')
        composed = (char)0x8C; // î
      else if (ch == 'o')
        composed = (char)0x93; // ô
      else if (ch == 'u')
        composed = (char)0x96; // û
    } else if (accent == '~') {
      if (ch == 'a')
        composed = (char)0xC6; // ã
      else if (ch == 'o')
        composed = (char)0xE5; // õ
      else if (ch == 'A')
        composed = (char)0xC7; // Ã
      else if (ch == 'O')
        composed = (char)0xE4; // Õ
    } else if (accent == '`') {
      if (ch == 'a')
        composed = (char)0x85; // à
      else if (ch == 'A')
        composed = (char)0xB7; // À
    } else if (accent == '"') {
      // diaeresis limited
      if (ch == 'u')
        composed = (char)0x81; // ü
      else if (ch == 'U')
        composed = (char)0x9A; // Ü
    }

    if (composed) {
      tty_handle_char(composed);
      return;
    } else {
      // Fallback: print accent then base char
      tty_handle_char(accent);
      tty_handle_char(ch);
      return;
    }
  }

  tty_handle_char(ch);
}

void keyboard_init(void) {
  keyboard_apply_layout(g_layouts[0]);
  irq_install_handler(1, keyboard_irq);
}

void keyboard_set_help_callback(keyboard_hotkey_callback cb) {
  g_help_hotkey = cb;
}

size_t keyboard_layout_count(void) {
  return sizeof(g_layouts) / sizeof(g_layouts[0]);
}

const char *keyboard_layout_name(size_t index) {
  if (index >= keyboard_layout_count()) {
    return NULL;
  }
  return g_layouts[index]->name;
}

const char *keyboard_layout_description(size_t index) {
  if (index >= keyboard_layout_count()) {
    return NULL;
  }
  return g_layouts[index]->description;
}

const char *keyboard_current_layout(void) {
  return current_layout ? current_layout->name : NULL;
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
  for (size_t i = 0; i < keyboard_layout_count(); ++i) {
    if (g_layouts[i] && g_layouts[i]->name &&
        layout_name_equal(name, g_layouts[i]->name)) {
      keyboard_apply_layout(g_layouts[i]);
      return 0;
    }
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
