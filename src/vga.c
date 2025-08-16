#include <stdint.h>
#include "vga.h"

static volatile uint16_t *const VGA_MEM = (uint16_t *)0xB8000;
static uint8_t vga_color = 0x07; // light gray on black
static int cur_row = 0, cur_col = 0;

static inline uint16_t vga_entry(char c, uint8_t color) {
    return ((uint16_t)color << 8) | (uint8_t)c;
}

void vga_set_color(uint8_t fg, uint8_t bg) {
    vga_color = (bg << 4) | (fg & 0x0F);
}

static void vga_put_at(char c, int row, int col) {
    VGA_MEM[row * VGA_WIDTH + col] = vga_entry(c, vga_color);
}

void vga_clear(void) {
    for (int r = 0; r < VGA_HEIGHT; ++r)
        for (int c = 0; c < VGA_WIDTH; ++c)
            vga_put_at(' ', r, c);
    cur_row = 0;
    cur_col = 0;
}

static void vga_scroll_if_needed(void) {
    if (cur_row < VGA_HEIGHT)
        return;
    // scroll 1 linha para cima
    for (int r = 1; r < VGA_HEIGHT; ++r) {
        for (int c = 0; c < VGA_WIDTH; ++c) {
            VGA_MEM[(r-1)*VGA_WIDTH + c] = VGA_MEM[r*VGA_WIDTH + c];
        }
    }
    // limpa última linha
    for (int c = 0; c < VGA_WIDTH; ++c)
        VGA_MEM[(VGA_HEIGHT-1)*VGA_WIDTH + c] = vga_entry(' ', vga_color);
    cur_row = VGA_HEIGHT - 1;
}

void vga_newline(void) {
    cur_col = 0;
    cur_row++;
    vga_scroll_if_needed();
}

void vga_backspace(void) {
    if (cur_col > 0) {
        cur_col--;
        vga_put_at(' ', cur_row, cur_col);
    } else if (cur_row > 0) {
        cur_row--;
        cur_col = VGA_WIDTH - 1;
        vga_put_at(' ', cur_row, cur_col);
    }
}

void vga_putc(char c) {
    if (c == '\n') { vga_newline(); return; }
    vga_put_at(c, cur_row, cur_col);
    cur_col++;
    if (cur_col >= VGA_WIDTH) {
        cur_col = 0;
        cur_row++;
        vga_scroll_if_needed();
    }
}

void vga_write(const char *s) {
    while (*s) vga_putc(*s++);
}

void vga_init(void) {
    vga_set_color(7, 0); // cinza claro em preto
    vga_clear();
}
