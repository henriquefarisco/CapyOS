/* com1.c: COM1 serial UART driver for debug output and input fallback.
 * Hyper-V Gen 2 has no PS/2 so serial is the primary fallback input. */
#include "drivers/serial/com1.h"
#include "drivers/io.h"

#define COM1_PORT 0x3F8

void com1_init(void) {
  outb(COM1_PORT + 1, 0x00); /* Disable interrupts */
  outb(COM1_PORT + 3, 0x80); /* Enable DLAB (set baud rate divisor) */
  outb(COM1_PORT + 0, 0x03); /* Divisor lo: 38400 baud */
  outb(COM1_PORT + 1, 0x00); /* Divisor hi */
  outb(COM1_PORT + 3, 0x03); /* 8 bits, no parity, one stop bit */
  outb(COM1_PORT + 2, 0xC7); /* Enable FIFO, clear, 14-byte threshold */
  outb(COM1_PORT + 4, 0x0B); /* IRQs enabled, RTS/DSR set */
}

void com1_putc(char c) {
  for (unsigned spin = 0; spin < 200000; ++spin) {
    if (inb(COM1_PORT + 5) & 0x20) {
      outb(COM1_PORT, (unsigned char)c);
      return;
    }
    __asm__ volatile("pause");
  }
}

void com1_puts(const char *s) {
  if (!s) return;
  while (*s) {
    if (*s == '\n')
      com1_putc('\r');
    com1_putc(*s++);
  }
}

int com1_data_ready(void) {
  return (inb(COM1_PORT + 5) & 0x01) ? 1 : 0;
}

char com1_getc(void) {
  return (char)inb(COM1_PORT);
}
