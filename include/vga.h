#ifndef VGA_H
#define VGA_H
#include <stdint.h>

#define VGA_WIDTH  80
#define VGA_HEIGHT 25

void vga_init(void);
void vga_clear(void);
void vga_set_color(uint8_t fg, uint8_t bg);
void vga_putc(char c);
void vga_write(const char *s);
void vga_newline(void);
void vga_backspace(void);
void vga_update_hw_cursor(void);  // move o cursor de hardware para (cur_row,cur_col)
void vga_get_cursor(int *row, int *col);
void vga_set_cursor(int row, int col);

#endif
