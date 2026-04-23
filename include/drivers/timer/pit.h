#ifndef PIT_H
#define PIT_H
#include <stdint.h>
void pit_init(uint32_t hz);          // programa o PIT e instala IRQ0
uint64_t pit_ticks(void);            // contador de ticks
#endif
