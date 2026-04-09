#ifndef ARCH_X86_64_APIC_H
#define ARCH_X86_64_APIC_H

#include <stdint.h>

#define APIC_BASE_MSR 0x1B
#define APIC_REG_ID 0x020
#define APIC_REG_EOI 0x0B0
#define APIC_REG_SPURIOUS 0x0F0
#define APIC_REG_TIMER_LVT 0x320
#define APIC_REG_TIMER_INIT 0x380
#define APIC_REG_TIMER_CURRENT 0x390
#define APIC_REG_TIMER_DIV 0x3E0

#define APIC_TIMER_PERIODIC 0x00020000
#define APIC_TIMER_MASKED 0x00010000
#define APIC_TIMER_VECTOR 0x20

#define APIC_SPURIOUS_ENABLE 0x100
#define APIC_SPURIOUS_VECTOR 0xFF

void apic_init(void);
void apic_eoi(void);
uint32_t apic_id(void);
void apic_timer_start(uint32_t frequency_hz);
void apic_timer_stop(void);
uint64_t apic_timer_ticks(void);
void apic_timer_set_callback(void (*callback)(void));

int apic_available(void);
uint64_t apic_base_address(void);

#endif /* ARCH_X86_64_APIC_H */
