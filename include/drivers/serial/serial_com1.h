#ifndef DRIVERS_SERIAL_SERIAL_COM1_H
#define DRIVERS_SERIAL_SERIAL_COM1_H

void com1_init(void);
void com1_putc(char c);
void com1_puts(const char *s);
int com1_data_ready(void);
char com1_getc(void);

#endif /* DRIVERS_SERIAL_SERIAL_COM1_H */
