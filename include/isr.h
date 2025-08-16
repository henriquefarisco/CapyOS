#ifndef ISR_H
#define ISR_H
#include <stdint.h>

typedef void (*irq_handler_t)(void);

void isr_dispatch(uint32_t int_no, uint32_t err_code);

void irq_install_handler(int irq, irq_handler_t handler);
void irq_uninstall_handler(int irq);

void pic_remap(uint8_t offset1, uint8_t offset2);
void pic_set_mask(uint8_t master_mask, uint8_t slave_mask);

#endif
