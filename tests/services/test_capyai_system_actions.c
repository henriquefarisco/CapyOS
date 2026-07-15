#include <stdio.h>
#include <string.h>

#include "core/work_queue.h"
#include "kernel/spinlock.h"
#include "services/capyai_system_actions.h"

static int failures;
static int opens[CAPYAI_SYSTEM_APP_COUNT];
static int closes[CAPYAI_SYSTEM_APP_COUNT];
static int sleeps;
static int reboot_calls;

#define CHECK(condition, label)                                               \
  do {                                                                        \
    if (condition) printf("  ok   %s\n", label);                             \
    else { printf("  FAIL %s\n", label); failures++; }                      \
  } while (0)

void spin_lock(struct spinlock *lock) { lock->locked = 1u; }
void spin_unlock(struct spinlock *lock) { lock->locked = 0u; }
void spin_lock_irqsave(struct spinlock *lock, uint64_t *flags) {
  *flags = 0u;
  spin_lock(lock);
}
void spin_unlock_irqrestore(struct spinlock *lock, uint64_t flags) {
  (void)flags;
  spin_unlock(lock);
}
int spin_trylock(struct spinlock *lock) {
  if (lock->locked) return -1;
  lock->locked = 1u;
  return 0;
}
void task_sleep(uint64_t ticks) {
  (void)ticks;
  sleeps++;
}

static int fake_open(void *ctx) {
  size_t index = (size_t)(uintptr_t)ctx;
  opens[index]++;
  return 0;
}

static int fake_close(void *ctx) {
  size_t index = (size_t)(uintptr_t)ctx;
  closes[index]++;
  return 0;
}

static int fake_reboot(void *ctx) {
  (void)ctx;
  reboot_calls++;
  return 0;
}

static void install_complete_registry(void) {
  static const char *const ids[CAPYAI_SYSTEM_APP_COUNT] = {
      "calculator", "files", "editor", "tasks", "settings", "capyai",
      "browser", "terminal"};
  struct capyai_system_app_binding bindings[CAPYAI_SYSTEM_APP_COUNT];
  size_t i;
  for (i = 0u; i < CAPYAI_SYSTEM_APP_COUNT; ++i) {
    bindings[i].app_id = ids[i];
    bindings[i].open = fake_open;
    bindings[i].close = fake_close;
    bindings[i].ctx = (void *)(uintptr_t)i;
  }
  CHECK(capyai_system_apps_install(bindings, CAPYAI_SYSTEM_APP_COUNT) == 0,
        "complete canonical app registry installs");
}

int main(void) {
  uint64_t ticket = 0u;
  uint64_t second_ticket = 0u;
  uint32_t delay = 0u;
  struct capyai_system_action_snapshot action;
  struct capyai_power_schedule_snapshot power;

  printf("[test_capyai_system_actions]\n");
  capyai_system_actions_reset_for_tests();
  work_queue_reset();
  memset(opens, 0, sizeof(opens));
  memset(closes, 0, sizeof(closes));

  CHECK(capyai_system_app_id_valid("calculator") == 1 &&
            capyai_system_app_id_valid("browser") == 1 &&
            capyai_system_app_id_valid("terminal") == 1,
        "known app IDs are accepted");
  CHECK(capyai_system_app_id_valid("../bin/capysh") == 0 &&
            capyai_system_app_id_valid("calculator;shutdown") == 0,
        "unknown and injected app IDs are rejected");

  install_complete_registry();
  CHECK(capyai_system_app_execute_foreground(CAPYAI_SYSTEM_APP_OPEN,
                                              "settings") == 0 &&
            opens[4] == 1,
        "foreground shell path invokes fixed callback without queue wait");
  CHECK(capyai_system_app_submit(CAPYAI_SYSTEM_APP_OPEN, "calculator",
                                 &ticket) == 0 && ticket != 0u,
        "worker queues typed app-open request");
  CHECK(capyai_system_app_submit(CAPYAI_SYSTEM_APP_CLOSE, "browser",
                                 &second_ticket) ==
            CAPYAI_SYSTEM_ACTION_ERR_BUSY,
        "single-slot queue rejects overlapping mutations");
  CHECK(capyai_system_action_snapshot(&action) == 0 &&
            action.state == CAPYAI_SYSTEM_REQUEST_QUEUED &&
            strcmp(action.app_id, "calculator") == 0,
        "queued request is observable");
  CHECK(capyai_system_actions_pump() == 1 && opens[0] == 1,
        "foreground pump performs exactly one app mutation");
  CHECK(capyai_system_app_wait(ticket, 0u) == 0,
        "worker observes foreground result by ticket");

  CHECK(capyai_system_app_submit(CAPYAI_SYSTEM_APP_CLOSE, "browser",
                                 &second_ticket) == 0,
        "completed slot accepts next request");
  CHECK(capyai_system_actions_pump() == 1 && closes[6] == 1 &&
            capyai_system_app_wait(second_ticket, 0u) == 0,
        "registered browser close callback executes");

  CHECK(capyai_system_app_submit(CAPYAI_SYSTEM_APP_OPEN, "terminal",
                                 &second_ticket) == 0 &&
            capyai_system_actions_pump() == 1 && opens[7] == 1 &&
            capyai_system_app_wait(second_ticket, 0u) == 0,
        "every desktop launcher app, including Terminal, is registered");

  sleeps = 0;
  CHECK(capyai_system_app_submit(CAPYAI_SYSTEM_APP_OPEN, "editor", &ticket) ==
            0 &&
            capyai_system_app_wait(ticket, 2u) ==
                CAPYAI_SYSTEM_ACTION_ERR_TIMEOUT &&
            sleeps == 2,
        "worker wait is bounded and sleeps instead of busy-waiting");
  CHECK(capyai_system_actions_pump() == 0 && opens[2] == 0,
        "timed-out queued request is revoked before foreground mutation");

  CHECK(capyai_power_parse_delay_ticks("30s", &delay) == 0 && delay == 3000u,
        "seconds duration parses");
  CHECK(capyai_power_parse_delay_ticks("5m", &delay) == 0 && delay == 30000u,
        "minutes duration parses");
  CHECK(capyai_power_parse_delay_ticks("2h", &delay) == 0 && delay == 720000u,
        "hours duration parses");
  CHECK(capyai_power_parse_delay_ticks("0m", &delay) != 0 &&
            capyai_power_parse_delay_ticks("8d", &delay) != 0 &&
            capyai_power_parse_delay_ticks("1m;reboot", &delay) != 0,
        "zero, excessive and injected durations fail closed");

  CHECK(capyai_power_schedule_configure(fake_reboot, NULL) == 0,
        "power callback registers in work queue");
  CHECK(capyai_power_schedule_reboot_after(100u, 300u) == 0 &&
            capyai_power_schedule_snapshot(&power) == 0 &&
            power.state == CAPYAI_POWER_SCHEDULE_SCHEDULED &&
            power.due_tick == 400u,
        "reboot schedule exposes exact due tick");
  CHECK(work_queue_poll_due(399u) == 0 && reboot_calls == 0,
        "scheduled reboot does not fire early");
  CHECK(work_queue_poll_due(400u) == 1 && reboot_calls == 1 &&
            capyai_power_schedule_snapshot(&power) == 0 &&
            power.state == CAPYAI_POWER_SCHEDULE_TRIGGERED,
        "scheduled reboot fires once through system work queue");

  CHECK(capyai_power_schedule_reboot_after(500u, 100u) == 0 &&
            capyai_power_schedule_cancel() == 0 &&
            work_queue_poll_due(600u) == 0 && reboot_calls == 1 &&
            capyai_power_schedule_snapshot(&power) == 0 &&
            power.state == CAPYAI_POWER_SCHEDULE_CANCELLED,
        "cancelled schedule is observable and never fires");

  if (failures == 0) {
    printf("[test_capyai_system_actions] all passed\n");
    return 0;
  }
  printf("[test_capyai_system_actions] %d FAILED\n", failures);
  return 1;
}
