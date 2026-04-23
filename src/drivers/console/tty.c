/* tty.c: line buffer, echo control, and keyboard-driven input. */
#include "drivers/console/tty.h"

#include "drivers/video/vga.h"

static char input_buffer[TTY_BUFFER_MAX];
static volatile size_t input_len = 0;
static char line_buffer[TTY_BUFFER_MAX];
static volatile size_t line_length = 0;
static volatile int line_ready = 0;
static volatile char char_buffer = 0;
static volatile int char_ready = 0;

static char current_prompt[TTY_PROMPT_MAX];
static int echo_enabled = 1;
static char echo_mask = 0;

void tty_init(void) {
    input_len = 0;
    line_length = 0;
    line_ready = 0;
    char_ready = 0;
    echo_enabled = 1;
    echo_mask = 0;
    current_prompt[0] = '\0';
}

void tty_set_prompt(const char *prompt) {
    size_t i = 0;
    if (prompt) {
        while (prompt[i] && i < TTY_PROMPT_MAX - 1) {
            current_prompt[i] = prompt[i];
            ++i;
        }
    }
    current_prompt[i] = '\0';
}

void tty_show_prompt(void) {
    input_len = 0;
    line_length = 0;
    line_ready = 0;
    if (current_prompt[0] != '\0') {
        vga_write(current_prompt);
    }
}

void tty_set_echo(int enabled) {
    echo_enabled = enabled ? 1 : 0;
    echo_mask = 0;
}

void tty_set_echo_mask(char mask) {
    echo_enabled = 1;
    echo_mask = mask;
}

size_t tty_readline(char *out, size_t max_len) {
    while (!line_ready) {
        __asm__ volatile("hlt");
    }

    size_t len = line_length;
    if (out && max_len > 0) {
        if (len >= max_len) {
            len = max_len - 1;
        }
        for (size_t i = 0; i < len; ++i) {
            out[i] = line_buffer[i];
        }
        out[len] = '\0';
    }

    line_ready = 0;
    line_length = 0;
    return len;
}

char tty_getc(void) {
    char_ready = 0;
    while (!char_ready) {
        __asm__ volatile("hlt");
    }
    return char_buffer;
}

void tty_handle_char(char ch) {
    if (ch == '\r') { // Ignora o caractere de retorno de carro
        return;
    }

    char_buffer = ch;
    char_ready = 1;

    if (line_ready) {
        return;
    }
    if (input_len >= TTY_BUFFER_MAX - 1) {
        return;
    }
    input_buffer[input_len++] = ch;
    if (echo_enabled) {
        char out = echo_mask ? echo_mask : ch;
        vga_putc(out);
    }
}

void tty_handle_backspace(void) {
    if (line_ready) {
        return;
    }
    if (input_len == 0) {
        return;
    }
    --input_len;
    // Atualize a tela mesmo se o eco estiver momentaneamente desabilitado
    vga_backspace();
}

void tty_handle_enter(void) {
    if (line_ready) {
        return;
    }
    if (echo_enabled) {
        vga_newline();
    } else {
        vga_newline();
    }

    if (input_len >= TTY_BUFFER_MAX) {
        input_len = TTY_BUFFER_MAX - 1;
    }
    for (size_t i = 0; i < input_len; ++i) {
        line_buffer[i] = input_buffer[i];
    }
    line_buffer[input_len] = '\0';
    line_length = input_len;
    line_ready = 1;
    input_len = 0;
    // Garante que o cursor esteja sempre visível em coluna 0 da próxima linha
    vga_update_hw_cursor();
}

void tty_inject_line(const char *line, int echo_line) {
    if (!line || line_ready) {
        return;
    }

    size_t len = 0;
    while (line[len] && len < TTY_BUFFER_MAX - 1) {
        ++len;
    }

    if (echo_line && echo_enabled && input_len > 0) {
        vga_newline();
    }

    input_len = 0;
    for (size_t i = 0; i < len; ++i) {
        input_buffer[input_len++] = line[i];
        if (echo_line && echo_enabled) {
            vga_putc(line[i]);
        }
    }

    tty_handle_enter();
}
