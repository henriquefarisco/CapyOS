#include <stdint.h>
#include "drivers/video/vga.h"
#include "drivers/io.h"

// protótipos internos
static void vga_scroll_if_needed(void);

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

void vga_newline(void) {
    cur_col = 0;
    cur_row++;
    vga_scroll_if_needed();
    vga_update_hw_cursor();
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
    vga_update_hw_cursor();
}

static char vga_map_latin1_to_cp437(char c){
    unsigned char u = (unsigned char)c;
    if (u == 0xC7) return (char)0x80; // Ç
    if (u == 0xE7) return (char)0x87; // ç
    // Common PT-BR accents (best-effort mapping to CP437)
    if (u == 0xE1) return (char)0xA0; // á
    if (u == 0xE9) return (char)0x82; // é
    if (u == 0xED) return (char)0xA1; // í
    if (u == 0xF3) return (char)0xA2; // ó
    if (u == 0xFA) return (char)0xA3; // ú
    if (u == 0xC1) return (char)0xB5; // Á
    if (u == 0xC9) return (char)0x90; // É
    if (u == 0xCD) return (char)0xD6; // Í
    if (u == 0xD3) return (char)0xE0; // Ó
    if (u == 0xDA) return (char)0xE9; // Ú
    if (u == 0xE2) return (char)0x83; // â
    if (u == 0xEA) return (char)0x88; // ê
    if (u == 0xEE) return (char)0x8C; // î
    if (u == 0xF4) return (char)0x93; // ô
    if (u == 0xFB) return (char)0x96; // û
    if (u == 0xE3) return (char)0xC6; // ã
    if (u == 0xF5) return (char)0xE5; // õ
    if (u == 0xC3) return (char)0xC7; // Ã
    if (u == 0xD5) return (char)0xE4; // Õ
    if (u == 0xE0) return (char)0x85; // à
    if (u == 0xC0) return (char)0xB7; // À
    return c; // fallback
}

void vga_putc(char c) {
    if (c == '\n') { vga_newline(); return; }
    if (c == '\b') {
        if (cur_col > 0) {
            cur_col--;
            vga_put_at(' ', cur_row, cur_col);
        } else if (cur_row > 0) {
            cur_row--;
            cur_col = VGA_WIDTH - 1;
            vga_put_at(' ', cur_row, cur_col);
        }
        vga_update_hw_cursor();
        return;
    }
    if (c == '\r') {
        cur_col = 0;
        vga_update_hw_cursor();
        return;
    }
    c = vga_map_latin1_to_cp437(c);
    vga_put_at(c, cur_row, cur_col);
    cur_col++;
    if (cur_col >= VGA_WIDTH) {
        cur_col = 0;
        cur_row++;
        vga_scroll_if_needed();
    }
    vga_update_hw_cursor();
}

// Also send characters to COM1 (0x3F8) so text is available on QEMU -serial
static inline void serial_putc(char c) {
    // Write byte to COM1 data port. Best-effort; no polling for TX ready to keep code simple.
    outb(0x3F8, (uint8_t)c);
}

void vga_write(const char *s) {
    while (*s) {
        vga_putc(*s);
        serial_putc(*s);
        ++s;
    }
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
    vga_update_hw_cursor();
}

/* vga_write implemented above to also mirror output to serial */

void vga_init(void) {
    vga_set_color(7, 0); // cinza claro em preto
    vga_clear();
}

void vga_update_hw_cursor(void){
    uint16_t pos = (uint16_t)(cur_row * VGA_WIDTH + cur_col);
    outb(0x3D4, 0x0F);
    outb(0x3D5, (uint8_t)(pos & 0xFF));
    outb(0x3D4, 0x0E);
    outb(0x3D5, (uint8_t)((pos >> 8) & 0xFF));
}

void vga_get_cursor(int *row, int *col) {
    if (row) *row = cur_row;
    if (col) *col = cur_col;
}

void vga_set_cursor(int row, int col) {
    if (row < 0) row = 0;
    if (row >= VGA_HEIGHT) row = VGA_HEIGHT - 1;
    if (col < 0) col = 0;
    if (col >= VGA_WIDTH) col = VGA_WIDTH - 1;
    cur_row = row;
    cur_col = col;
    vga_update_hw_cursor();
}

void vga_backspace_multiple(int count) {
    for (int i = 0; i < count; ++i) {
        vga_backspace();
    }
}
