#ifndef TTY_H
#define TTY_H

#include <stddef.h>

#define TTY_BUFFER_MAX 128
#define TTY_PROMPT_MAX 64

void tty_init(void);
void tty_set_prompt(const char *prompt);
void tty_show_prompt(void);
void tty_set_echo(int enabled);
void tty_set_echo_mask(char mask);
size_t tty_readline(char *out, size_t max_len);

void tty_handle_char(char ch);
void tty_handle_backspace(void);
void tty_handle_enter(void);

#endif
