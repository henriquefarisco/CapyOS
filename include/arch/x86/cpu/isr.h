#ifndef ISR_H
#define ISR_H
#include <stdint.h>

typedef void (*irq_handler_t)(void);

struct isr_stack_state {
    uint32_t eax;
    uint32_t ecx;
    uint32_t edx;
    uint32_t ebx;
    uint32_t esp0;    // valor original de ESP antes do pusha
    uint32_t ebp;
    uint32_t esi;
    uint32_t edi;
};

void isr_dispatch(uint32_t int_no, uint32_t err_code, struct isr_stack_state *stack);

void irq_install_handler(int irq, irq_handler_t handler);
void irq_uninstall_handler(int irq);

void pic_remap(uint8_t offset1, uint8_t offset2);
void pic_set_mask(uint8_t master_mask, uint8_t slave_mask);

#endif
