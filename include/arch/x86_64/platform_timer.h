#ifndef ARCH_X86_64_PLATFORM_TIMER_H
#define ARCH_X86_64_PLATFORM_TIMER_H

#include <stdint.h>

void x64_platform_timer_init(int native_runtime_ready);
int x64_platform_timer_active(void);
const char *x64_platform_timer_status(void);
uint32_t x64_platform_timer_hz(void);

#endif /* ARCH_X86_64_PLATFORM_TIMER_H */
