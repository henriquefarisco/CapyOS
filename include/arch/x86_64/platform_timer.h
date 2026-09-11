#ifndef ARCH_X86_64_PLATFORM_TIMER_H
#define ARCH_X86_64_PLATFORM_TIMER_H

#include <stdint.h>

void x64_platform_timer_init(int native_runtime_ready);
/* Late native desktop activation, after scheduler/task initialization.
 * Refuses firmware/deferred IDT or APIC-owned IRQ0; preserves caller IF. */
int x64_platform_timer_start_scheduler(void);
/* A single bounded native service, after PIC EOI, before task scheduling.
 * No waits, allocation, logging, callbacks or firmware access in the hook. */
void x64_platform_timer_set_service_hook(void (*hook)(void));
int x64_platform_timer_active(void);
const char *x64_platform_timer_status(void);
uint32_t x64_platform_timer_hz(void);

#endif /* ARCH_X86_64_PLATFORM_TIMER_H */
