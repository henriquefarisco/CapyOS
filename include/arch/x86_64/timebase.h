#ifndef ARCH_X86_64_TIMEBASE_H
#define ARCH_X86_64_TIMEBASE_H

#include <stdint.h>

void x64_timebase_init(void);
uint64_t x64_timebase_ticks_100hz(void);
uint64_t x64_timebase_hz(void);
const char *x64_timebase_source(void);

#endif /* ARCH_X86_64_TIMEBASE_H */
