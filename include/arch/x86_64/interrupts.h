#ifndef ARCH_X86_64_INTERRUPTS_H
#define ARCH_X86_64_INTERRUPTS_H

#include <stdint.h>

void gdt_init(void);
void idt_install(void);
void irq_install_handler(int irq, void (*handler)(void));
void irq_uninstall_handler(int irq);
void pic_remap(uint8_t master_offset, uint8_t slave_offset);
void pic_set_mask(uint8_t master_mask, uint8_t slave_mask);
void x64_irq_unmask(int irq);
void x64_irq_mask(int irq);
void x64_interrupts_enable(void);
void x64_interrupts_disable(void);

void x64_platform_tables_init(int native_runtime_ready);
int x64_platform_tables_active(void);
int x64_platform_tables_prepare_bridge(void);
int x64_platform_tables_bridge_active(void);
const char *x64_platform_tables_status(void);

#endif /* ARCH_X86_64_INTERRUPTS_H */
