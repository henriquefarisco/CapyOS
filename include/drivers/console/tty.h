/* TTY shim for 64-bit kernel.
 * Provides minimal TTY functions using framebuffer console.
 */
#ifndef TTY_H
#define TTY_H

#include <stddef.h>

#define TTY_BUFFER_MAX 128
#define TTY_PROMPT_MAX 64

#ifdef __x86_64__
/* For 64-bit, TTY functions are stubs - shell uses framebuffer directly */
#include "core/kcon.h"

static inline void tty_init(void) {}
static inline void tty_set_prompt(const char *prompt) { (void)prompt; }
static inline void tty_show_prompt(void) { k_puts("noir64> "); }
static inline void tty_set_echo(int enabled) { (void)enabled; }
static inline void tty_set_echo_mask(char mask) { (void)mask; }
static inline char tty_getc(void) { return 0; } /* Stub - will be implemented */
static inline void tty_inject_line(const char *line, int echo) {
  (void)line;
  (void)echo;
}
static inline void tty_handle_char(char ch) { k_putc(ch); }
static inline void tty_handle_backspace(void) { k_puts("\b \b"); }
static inline void tty_handle_enter(void) { k_putc('\n'); }

/* Blocking readline - stub for now */
static inline size_t tty_readline(char *out, size_t max_len) {
  (void)out;
  (void)max_len;
  return 0; /* Will be implemented with keyboard integration */
}

#else
/* 32-bit uses full TTY implementation */
void tty_init(void);
void tty_set_prompt(const char *prompt);
void tty_show_prompt(void);
void tty_set_echo(int enabled);
void tty_set_echo_mask(char mask);
size_t tty_readline(char *out, size_t max_len);
char tty_getc(void);
void tty_inject_line(const char *line, int echo_line);
void tty_handle_char(char ch);
void tty_handle_backspace(void);
void tty_handle_enter(void);
#endif

#endif /* TTY_H */
