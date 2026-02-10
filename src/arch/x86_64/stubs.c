// Temporary stubs for x86_64 build until proper implementations are added.
#include <stddef.h>
#include <stdint.h>

void gdt_init(void) {}
void idt_install(void) {}
void pic_remap(uint8_t a, uint8_t b) {
  (void)a;
  (void)b;
}
void pic_set_mask(uint8_t a, uint8_t b) {
  (void)a;
  (void)b;
}
void irq_install_handler(int irq, void (*handler)(void)) {
  (void)irq;
  (void)handler;
}

/* Memory functions for freestanding environment */
void *memcpy(void *dest, const void *src, size_t n) {
  uint8_t *d = (uint8_t *)dest;
  const uint8_t *s = (const uint8_t *)src;
  while (n--) {
    *d++ = *s++;
  }
  return dest;
}

void *memset(void *dest, int c, size_t n) {
  uint8_t *d = (uint8_t *)dest;
  while (n--) {
    *d++ = (uint8_t)c;
  }
  return dest;
}

int memcmp(const void *s1, const void *s2, size_t n) {
  const uint8_t *p1 = (const uint8_t *)s1;
  const uint8_t *p2 = (const uint8_t *)s2;
  while (n--) {
    if (*p1 != *p2) {
      return *p1 - *p2;
    }
    p1++;
    p2++;
  }
  return 0;
}

/* VGA stubs - redirect to framebuffer console */
extern void fbcon_clear_view(void);
void vga_init(void) { fbcon_clear_view(); }
void vga_clear(void) { fbcon_clear_view(); }

extern void fbcon_putc(char c);
void vga_putc(char c) { fbcon_putc(c); }

void vga_backspace(void) { fbcon_putc('\b'); }
void vga_backspace_multiple(int n) {
  for (int i = 0; i < n; i++) {
    fbcon_putc('\b');
  }
}
void vga_get_cursor(int *row, int *col) {
  if (row)
    *row = 0;
  if (col)
    *col = 0;
}
void vga_set_cursor(int row, int col) {
  (void)row;
  (void)col;
}
void vga_update_hw_cursor(void) {}
void vga_set_color(unsigned char fg, unsigned char bg) {
  (void)fg;
  (void)bg;
}

/* vga_write - print string to framebuffer console */
extern void fbcon_print(const char *s);
void vga_write(const char *s) {
  if (s)
    fbcon_print(s);
}

void vga_newline(void) { fbcon_putc('\n'); }

/* PIT timer stub */
static uint32_t g_fake_ticks = 0;
uint32_t pit_ticks(void) { return g_fake_ticks++; }

/* ACPI stub */
void acpi_shutdown(void) {
  for (;;)
    __asm__ volatile("hlt");
}

/* ============== TTY stubs (polled mode, no interrupts) ============== */
static char g_tty_prompt[64] = "capy64> ";
static int g_tty_echo = 1;
static char g_tty_echo_mask = '\0';

/* Implemented in kernel_main64.c */
extern size_t kernel_input_readline(char *buf, size_t maxlen, int mask);
extern int kernel_input_getc(char *out);

void tty_init(void) {}

void tty_set_prompt(const char *prompt) {
  if (!prompt)
    return;
  size_t i = 0;
  while (prompt[i] && i < sizeof(g_tty_prompt) - 1) {
    g_tty_prompt[i] = prompt[i];
    i++;
  }
  g_tty_prompt[i] = '\0';
}

void tty_show_prompt(void) {
  extern void fbcon_print(const char *s);
  fbcon_print(g_tty_prompt);
}

void tty_set_echo(int enabled) { g_tty_echo = enabled ? 1 : 0; }
void tty_set_echo_mask(char mask) { g_tty_echo_mask = mask; }

size_t tty_readline(char *out, size_t max_len) {
  int mask = 0;
  if (g_tty_echo_mask != '\0') {
    mask = 1;
  } else if (!g_tty_echo) {
    mask = 1;
  }
  return kernel_input_readline(out, max_len, mask);
}

char tty_getc(void) {
  char ch = 0;
  if (kernel_input_getc(&ch)) {
    return ch;
  }
  return 0;
}
void tty_handle_char(char ch) { (void)ch; }
void tty_handle_backspace(void) {}
void tty_handle_enter(void) {}
void tty_inject_line(const char *line, int echo) {
  (void)line;
  (void)echo;
}

/* ============== Keyboard stubs ============== */
void keyboard_set_help_callback(void (*cb)(void)) { (void)cb; }
void keyboard_init(void) {}
int keyboard_get_layout(void) { return 0; }
void keyboard_set_layout(int layout) { (void)layout; }

/* ============== System settings stubs ============== */
#include "core/system_init.h"

static struct system_settings g_mock_settings = {.hostname = "capyos64",
                                                 .theme = "noir",
                                                 .keyboard_layout = "us",
                                                 .splash_enabled = 0,
                                                 .diagnostics_enabled = 0};

int system_load_settings(struct system_settings *out) {
  if (out) {
    *out = g_mock_settings;
  }
  return 0;
}

int system_save_settings(const struct system_settings *settings) {
  if (settings) {
    g_mock_settings = *settings;
  }
  return 0;
}

int system_save_keyboard_layout(const char *name) {
  if (!name)
    return -1;
  size_t i = 0;
  while (name[i] && i < sizeof(g_mock_settings.keyboard_layout) - 1) {
    g_mock_settings.keyboard_layout[i] = name[i];
    i++;
  }
  g_mock_settings.keyboard_layout[i] = '\0';
  return 0;
}
