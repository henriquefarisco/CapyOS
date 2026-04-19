/* TTY shim for 64-bit kernel.
 * Provides minimal TTY functions using framebuffer console.
 */
#ifndef TTY_H
#define TTY_H

#include <stddef.h>

#define TTY_BUFFER_MAX 128
#define TTY_PROMPT_MAX 64

#ifdef __x86_64__
/* For 64-bit, TTY is implemented in stubs.c and wired to kernel input polling. */
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
