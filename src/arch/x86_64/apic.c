#include "arch/x86_64/apic.h"
#include <stddef.h>

static volatile uint32_t *apic_base = NULL;
static uint64_t apic_tick_count = 0;
static void (*apic_timer_callback)(void) = NULL;
static int apic_is_available = 0;

static inline uint64_t rdmsr(uint32_t msr) {
  uint32_t lo, hi;
  __asm__ volatile("rdmsr" : "=a"(lo), "=d"(hi) : "c"(msr));
  return ((uint64_t)hi << 32) | lo;
}

static inline void wrmsr(uint32_t msr, uint64_t val) {
  __asm__ volatile("wrmsr" : : "a"((uint32_t)val), "d"((uint32_t)(val >> 32)), "c"(msr));
}

static inline void apic_write(uint32_t reg, uint32_t val) {
  if (apic_base) apic_base[reg / 4] = val;
}

static inline uint32_t apic_read(uint32_t reg) {
  return apic_base ? apic_base[reg / 4] : 0;
}

static int detect_apic(void) {
  uint32_t eax, ebx, ecx, edx;
  __asm__ volatile("cpuid" : "=a"(eax), "=b"(ebx), "=c"(ecx), "=d"(edx) : "a"(1));
  return (edx & (1 << 9)) ? 1 : 0;
}

uint64_t apic_base_address(void) {
  uint64_t base = rdmsr(APIC_BASE_MSR);
  return base & 0xFFFFF000ULL;
}

int apic_available(void) {
  return apic_is_available;
}

void apic_init(void) {
  if (!detect_apic()) {
    apic_is_available = 0;
    return;
  }

  uint64_t base_phys = apic_base_address();
  apic_base = (volatile uint32_t *)(uintptr_t)base_phys;

  uint64_t msr = rdmsr(APIC_BASE_MSR);
  msr |= (1 << 11);
  wrmsr(APIC_BASE_MSR, msr);

  apic_write(APIC_REG_SPURIOUS, APIC_SPURIOUS_ENABLE | APIC_SPURIOUS_VECTOR);

  apic_write(APIC_REG_TIMER_LVT, APIC_TIMER_MASKED);

  apic_is_available = 1;
  apic_tick_count = 0;
}

void apic_eoi(void) {
  if (apic_base) apic_write(APIC_REG_EOI, 0);
}

uint32_t apic_id(void) {
  return apic_base ? (apic_read(APIC_REG_ID) >> 24) : 0;
}

void apic_timer_start(uint32_t frequency_hz) {
  if (!apic_base) return;

  apic_write(APIC_REG_TIMER_DIV, 0x03);

  apic_write(APIC_REG_TIMER_INIT, 0xFFFFFFFF);
  for (volatile int i = 0; i < 1000000; i++) {}
  uint32_t elapsed = 0xFFFFFFFF - apic_read(APIC_REG_TIMER_CURRENT);
  apic_write(APIC_REG_TIMER_INIT, 0);

  uint32_t ticks_per_tick = elapsed / 10;
  if (frequency_hz == 0) frequency_hz = 100;
  uint32_t init_count = (ticks_per_tick * 10) / (1000 / frequency_hz);
  if (init_count == 0) init_count = 1;

  apic_write(APIC_REG_TIMER_LVT, APIC_TIMER_PERIODIC | APIC_TIMER_VECTOR);
  apic_write(APIC_REG_TIMER_INIT, init_count);
}

void apic_timer_stop(void) {
  if (!apic_base) return;
  apic_write(APIC_REG_TIMER_LVT, APIC_TIMER_MASKED);
  apic_write(APIC_REG_TIMER_INIT, 0);
}

uint64_t apic_timer_ticks(void) {
  return apic_tick_count;
}

void apic_timer_set_callback(void (*callback)(void)) {
  apic_timer_callback = callback;
}

void apic_timer_irq_handler(void) {
  apic_tick_count++;
  if (apic_timer_callback) apic_timer_callback();
  apic_eoi();
}
