#include "arch/x86_64/platform_timer.h"

#include <stdint.h>

#include "arch/x86_64/interrupts.h"
#include "arch/x86_64/timebase.h"
#include "drivers/timer/pit.h"
#include "security/csprng.h"

#define PIT_CH0 0x40
#define PIT_CMD 0x43
#define PIT_INPUT_HZ 1193182u

static volatile uint64_t g_pit_ticks = 0;
static uint32_t g_pit_hz = 100u;
static int g_pit_programmed = 0;
static int g_platform_timer_active = 0;
static const char *g_platform_timer_status = "not-initialized";

static inline void outb_local(uint16_t port, uint8_t value) {
  __asm__ volatile("outb %0, %1" : : "a"(value), "Nd"(port));
}

static void pit_irq0_handler(void) {
  ++g_pit_ticks;
  csprng_feed_entropy((uint32_t)g_pit_ticks);
}

void pit_init(uint32_t hz) {
  uint16_t divisor = 0;
  if (hz == 0u) {
    hz = 100u;
  }

  divisor = (uint16_t)(PIT_INPUT_HZ / hz);
  if (divisor == 0u) {
    divisor = 1u;
  }

  irq_install_handler(0, pit_irq0_handler);
  outb_local(PIT_CMD, 0x34u);
  outb_local(PIT_CH0, (uint8_t)(divisor & 0xFFu));
  outb_local(PIT_CH0, (uint8_t)(divisor >> 8));

  g_pit_ticks = 0;
  g_pit_hz = hz;
  g_pit_programmed = 1;
}

uint64_t pit_ticks(void) {
  if (g_platform_timer_active && g_pit_programmed) {
    return g_pit_ticks;
  }
  return x64_timebase_ticks_100hz();
}

void x64_platform_timer_init(int native_runtime_ready) {
  if (g_platform_timer_active) {
    return;
  }

  if (!native_runtime_ready) {
    g_platform_timer_status = "deferred-firmware-runtime";
    return;
  }

  if (!x64_platform_tables_active()) {
    g_platform_timer_status = "waiting-for-native-idt";
    return;
  }

  pit_init(100u);
  x64_irq_unmask(0);
  x64_interrupts_enable();
  g_platform_timer_active = 1;
  g_platform_timer_status = "pit-irq0-active";
}

int x64_platform_timer_active(void) { return g_platform_timer_active; }

const char *x64_platform_timer_status(void) { return g_platform_timer_status; }

uint32_t x64_platform_timer_hz(void) {
  if (g_platform_timer_active && g_pit_programmed) {
    return g_pit_hz;
  }
  return 100u;
}
