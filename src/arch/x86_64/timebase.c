#include "arch/x86_64/timebase.h"

#include <stdint.h>

#define PIT_CH2 0x42
#define PIT_CMD 0x43
#define PIT_SPKR 0x61
#define PIT_INPUT_HZ 1193182ULL
#define PIT_CALIBRATION_HZ 20ULL

struct timebase_state {
  uint64_t tsc_hz;
  uint64_t cycles_per_tick;
  uint64_t tsc_start;
  const char *source;
  int initialized;
};

static struct timebase_state g_timebase = {0, 0, 0, "uninitialized", 0};

static inline void io_wait_local(void) {
  __asm__ volatile("outb %0, %1" : : "a"((uint8_t)0), "Nd"((uint16_t)0x80));
}

static inline void outb_local(uint16_t port, uint8_t value) {
  __asm__ volatile("outb %0, %1" : : "a"(value), "Nd"(port));
}

static inline uint8_t inb_local(uint16_t port) {
  uint8_t value = 0;
  __asm__ volatile("inb %1, %0" : "=a"(value) : "Nd"(port));
  return value;
}

static inline uint64_t rdtsc_local(void) {
  uint32_t lo = 0;
  uint32_t hi = 0;
  __asm__ volatile("rdtsc" : "=a"(lo), "=d"(hi));
  return ((uint64_t)hi << 32) | lo;
}

static inline void cpu_relax_local(void) {
  __asm__ volatile("pause" ::: "memory");
}

static void cpuid_local(uint32_t leaf, uint32_t subleaf, uint32_t *eax,
                        uint32_t *ebx, uint32_t *ecx, uint32_t *edx) {
  uint32_t a = 0;
  uint32_t b = 0;
  uint32_t c = 0;
  uint32_t d = 0;
  __asm__ volatile("cpuid"
                   : "=a"(a), "=b"(b), "=c"(c), "=d"(d)
                   : "a"(leaf), "c"(subleaf));
  if (eax) {
    *eax = a;
  }
  if (ebx) {
    *ebx = b;
  }
  if (ecx) {
    *ecx = c;
  }
  if (edx) {
    *edx = d;
  }
}

static uint32_t cpuid_max_basic_leaf(void) {
  uint32_t eax = 0;
  cpuid_local(0, 0, &eax, 0, 0, 0);
  return eax;
}

static uint64_t detect_tsc_hz_cpuid_0x15(void) {
  if (cpuid_max_basic_leaf() < 0x15U) {
    return 0;
  }
  uint32_t eax = 0;
  uint32_t ebx = 0;
  uint32_t ecx = 0;
  cpuid_local(0x15U, 0, &eax, &ebx, &ecx, 0);
  if (eax == 0U || ebx == 0U || ecx == 0U) {
    return 0;
  }
  return ((uint64_t)ecx * (uint64_t)ebx) / (uint64_t)eax;
}

static uint64_t detect_tsc_hz_cpuid_0x16(void) {
  if (cpuid_max_basic_leaf() < 0x16U) {
    return 0;
  }
  uint32_t eax = 0;
  cpuid_local(0x16U, 0, &eax, 0, 0, 0);
  if (eax == 0U) {
    return 0;
  }
  return (uint64_t)eax * 1000000ULL;
}

static uint64_t calibrate_tsc_hz_pit(void) {
  uint16_t divisor = (uint16_t)(PIT_INPUT_HZ / PIT_CALIBRATION_HZ);
  uint8_t speaker = inb_local(PIT_SPKR);
  uint64_t start = 0;
  uint64_t end = 0;
  uint32_t spin_guard = 50000000U;

  outb_local(PIT_SPKR, (uint8_t)(speaker & ~0x01U));
  io_wait_local();
  outb_local(PIT_CMD, 0xB0U);
  outb_local(PIT_CH2, (uint8_t)(divisor & 0xFFU));
  outb_local(PIT_CH2, (uint8_t)(divisor >> 8));
  outb_local(PIT_SPKR, (uint8_t)((speaker & ~0x02U) | 0x01U));
  io_wait_local();

  start = rdtsc_local();
  while (((inb_local(PIT_SPKR) & 0x20U) == 0U) && spin_guard-- != 0U) {
    cpu_relax_local();
  }
  end = rdtsc_local();

  outb_local(PIT_SPKR, speaker);
  if (spin_guard == 0U || end <= start) {
    return 0;
  }
  return (end - start) * PIT_CALIBRATION_HZ;
}

void x64_timebase_init(void) {
  uint64_t detected_hz = 0;
  const char *source = "assumed-tsc";

  if (g_timebase.initialized) {
    return;
  }

  detected_hz = calibrate_tsc_hz_pit();
  if (detected_hz != 0) {
    source = "pit-calibrated-tsc";
  } else {
    detected_hz = detect_tsc_hz_cpuid_0x15();
    if (detected_hz != 0) {
      source = "cpuid-0x15-tsc";
    } else {
      detected_hz = detect_tsc_hz_cpuid_0x16();
      if (detected_hz != 0) {
        source = "cpuid-0x16-tsc";
      }
    }
  }

  if (detected_hz == 0) {
    detected_hz = 1000000000ULL;
  }

  g_timebase.tsc_hz = detected_hz;
  g_timebase.cycles_per_tick = detected_hz / 100ULL;
  if (g_timebase.cycles_per_tick == 0) {
    g_timebase.cycles_per_tick = 1ULL;
  }
  g_timebase.tsc_start = rdtsc_local();
  g_timebase.source = source;
  g_timebase.initialized = 1;
}

uint64_t x64_timebase_ticks_100hz(void) {
  uint64_t now = 0;
  if (!g_timebase.initialized) {
    x64_timebase_init();
  }
  now = rdtsc_local();
  if (now <= g_timebase.tsc_start) {
    return 0;
  }
  return (now - g_timebase.tsc_start) / g_timebase.cycles_per_tick;
}

uint64_t x64_timebase_hz(void) {
  if (!g_timebase.initialized) {
    x64_timebase_init();
  }
  return g_timebase.tsc_hz;
}

const char *x64_timebase_source(void) {
  if (!g_timebase.initialized) {
    x64_timebase_init();
  }
  return g_timebase.source;
}
