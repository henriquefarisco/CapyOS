#ifndef IDT_H
#define IDT_H
#include <stdint.h>

typedef struct __attribute__((packed)) {
    uint16_t base_lo;
    uint16_t sel;
    uint8_t  always0;
    uint8_t  flags;      // P DPL S Type
    uint16_t base_hi;
} idt_entry_t;

typedef struct __attribute__((packed)) {
    uint16_t limit;
    uint32_t base;
} idt_ptr_t;

void idt_install(void);
void idt_set_gate(uint8_t num, uint32_t base, uint16_t sel, uint8_t flags);

#endif
