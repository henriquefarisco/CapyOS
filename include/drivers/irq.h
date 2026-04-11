#ifndef DRIVERS_IRQ_H
#define DRIVERS_IRQ_H
/* IRQ handler registration abstraction.
 * On the x86_64 kernel the real IDT is managed by arch/x86_64/interrupts.c;
 * this header provides the legacy callback signature so drivers compiled in
 * both 32-bit and 64-bit builds can register IRQ handlers portably. */
#include <stdint.h>

typedef void (*irq_handler_t)(void);

void irq_install_handler(int irq, irq_handler_t handler);
void irq_uninstall_handler(int irq);

void pic_remap(uint8_t offset1, uint8_t offset2);
void pic_set_mask(uint8_t master_mask, uint8_t slave_mask);

#endif /* DRIVERS_IRQ_H */
