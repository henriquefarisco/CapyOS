#include "arch/x86_64/platform_timer.h"

#include <stdint.h>

#include "arch/x86_64/interrupts.h"
#include "arch/x86_64/timebase.h"
#include "arch/x86_64/apic.h"
#include "kernel/scheduler.h"
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
static void (*g_timer_service_hook)(void);

void x64_platform_timer_set_service_hook(void (*hook)(void)) {
  __atomic_store_n(&g_timer_service_hook, hook, __ATOMIC_RELEASE);
}

static void platform_scheduler_tick(void) {
  void (*hook)(void) = __atomic_load_n(&g_timer_service_hook, __ATOMIC_ACQUIRE);
  if (hook) hook();
  scheduler_tick();
}

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
  /* Keep IRQ0 masked during early boot. The startup path is still bringing
   * core subsystems online, and periodic timer interrupts here have been
   * causing re-entrant faults and corrupted returns on UEFI/VM guests.
   * Consumers fall back to the calibrated timebase until the timer is
   * explicitly activated later. */
  g_platform_timer_active = 0;
  g_platform_timer_status = "pit-programmed-irq-deferred";
}

int x64_platform_timer_active(void) { return g_platform_timer_active; }

int x64_platform_timer_start_scheduler(void) {
#ifdef CAPYOS_PREEMPTIVE_SCHEDULER
  uint64_t flags;
  if (!g_pit_programmed || !x64_platform_tables_active() || apic_available() ||
      !scheduler_running() || !task_current()) return -1;
  __asm__ volatile("pushfq; popq %0; cli" : "=r"(flags) : : "memory");
  if (!g_platform_timer_active) {
    /* Preserve the time domain used by work-queue deadlines before IRQ0
     * activation. Do not reset pit_ticks() back to zero at the desktop. */
    g_pit_ticks = x64_timebase_ticks_100hz();
    x64_irq_set_after_eoi(0, platform_scheduler_tick);
    g_platform_timer_active = 1;
    g_platform_timer_status = "pit-scheduler-active";
    x64_irq_unmask(0);
  }
  if (flags & (1u << 9)) __asm__ volatile("sti" : : : "memory");
  return 0;
#else
  return -1;
#endif
}

const char *x64_platform_timer_status(void) { return g_platform_timer_status; }

uint32_t x64_platform_timer_hz(void) {
  if (g_platform_timer_active && g_pit_programmed) {
    return g_pit_hz;
  }
  return 100u;
}
