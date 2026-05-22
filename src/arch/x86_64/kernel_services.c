/* kernel_services.c — Network + logger + update-agent service hooks.
 *
 * Split from kernel_main.c, then further split (2026-05-21) across:
 *   - kernel_services_capypkg.c   (CapyPKG adapters + bootstrap + hooks)
 *   - kernel_services_work.c      (background work-queue items + dispatch)
 *   - kernel_services_recovery.c  (boot policy + maintenance + recovery)
 * to keep each TU ≤ 900 lines per layout-audit.
 */
#pragma GCC optimize("O0")
#include <stddef.h>
#include <stdint.h>

#include "arch/x86_64/kernel_main_internal.h"
#include "arch/x86_64/kernel_runtime_control.h"
#include "core/system_init.h"
#include "kernel/log/klog.h"
#include "kernel/log/klog_persist.h"
#include "net/stack.h"
#include "services/service_manager.h"
#include "services/update_agent.h"

/* ── network service ─────────────────────────────────────────────────── */

static uint32_t g_networkd_dhcp_failures = 0;
static uint64_t g_networkd_dhcp_next_tick = 0;
static int g_networkd_dhcp_last_error = 0;

static int kernel_network_mode_is_dhcp(void) {
  const char *mode = g_shell_settings.network_mode;
  return mode && mode[0] == 'd' && mode[1] == 'h' && mode[2] == 'c' &&
         mode[3] == 'p' && mode[4] == '\0';
}

static uint64_t kernel_networkd_tick(void) {
  struct system_service_status svc;
  if (service_manager_get(SYSTEM_SERVICE_NETWORKD, &svc) == 0) {
    return svc.polls;
  }
  return 0u;
}

static uint32_t kernel_networkd_dhcp_backoff_ticks(void) {
  uint32_t shift = g_networkd_dhcp_failures;
  if (shift > 5u) {
    shift = 5u;
  }
  return 12u << shift;
}

static void kernel_networkd_maybe_dhcp(const struct net_stack_status *status) {
  uint64_t now = kernel_networkd_tick();

  if (!kernel_network_mode_is_dhcp()) {
    g_networkd_dhcp_failures = 0;
    g_networkd_dhcp_next_tick = 0;
    g_networkd_dhcp_last_error = 0;
    return;
  }
  if (!status || !status->initialized || !status->ready ||
      !status->runtime_supported || !status->nic.found) {
    return;
  }
  if (status->dhcp_lease_acquired || now < g_networkd_dhcp_next_tick) {
    return;
  }

  if (net_stack_dhcp_acquire(500u) == 0) {
    g_networkd_dhcp_failures = 0;
    g_networkd_dhcp_next_tick = 0;
    g_networkd_dhcp_last_error = 0;
    klog(KLOG_INFO, "[net] networkd: DHCP lease acquired.");
    return;
  }

  g_networkd_dhcp_failures++;
  g_networkd_dhcp_last_error = -1;
  g_networkd_dhcp_next_tick = now + kernel_networkd_dhcp_backoff_ticks();
  klog(KLOG_WARN, "[net] networkd: DHCP retry failed; backing off.");
}

void kernel_update_network_service_status(void) {
  struct net_stack_status net_status = {0};

  if (net_stack_status(&net_status) != 0) {
    (void)service_manager_set_state(
        SYSTEM_SERVICE_NETWORKD, SYSTEM_SERVICE_STATE_BLOCKED, -1,
        "network status unavailable");
    return;
  }
  if (!net_status.nic.found) {
    (void)service_manager_set_state(
        SYSTEM_SERVICE_NETWORKD, SYSTEM_SERVICE_STATE_BLOCKED, -2,
        "no network adapter detected");
    return;
  }
  if (!net_status.runtime_supported) {
    (void)service_manager_set_state(
        SYSTEM_SERVICE_NETWORKD, SYSTEM_SERVICE_STATE_BLOCKED, -3,
        "adapter detected but driver is not validated");
    return;
  }
  if (!net_status.ready) {
    (void)service_manager_set_state(
        SYSTEM_SERVICE_NETWORKD, SYSTEM_SERVICE_STATE_DEGRADED, -4,
        "validated network driver detected but not ready");
    return;
  }
  if (kernel_network_mode_is_dhcp() && !net_status.dhcp_lease_acquired) {
    (void)service_manager_set_state(
        SYSTEM_SERVICE_NETWORKD, SYSTEM_SERVICE_STATE_DEGRADED,
        g_networkd_dhcp_last_error,
        "network stack ready; DHCP lease pending");
    return;
  }
  (void)service_manager_set_state(SYSTEM_SERVICE_NETWORKD,
                                  SYSTEM_SERVICE_STATE_READY, 0,
                                  kernel_network_mode_is_dhcp()
                                      ? "network stack ready; DHCP lease acquired"
                                      : "network stack ready");
}

int kernel_service_poll_networkd(void *ctx) {
  struct net_stack_status net_status = {0};

  (void)ctx;
  kernel_maybe_refresh_network_runtime();
  if (net_stack_status(&net_status) == 0) {
    kernel_networkd_maybe_dhcp(&net_status);
  }
  kernel_update_network_service_status();
  return 0;
}

int kernel_service_start_networkd(void *ctx) {
  (void)ctx;
  g_network_runtime_refresh_enabled = 1;
  return kernel_service_poll_networkd(NULL);
}

int kernel_service_stop_networkd(void *ctx) {
  (void)ctx;
  g_network_runtime_refresh_enabled = 0;
  return 0;
}

/* ── logger service ──────────────────────────────────────────────────── */

int kernel_service_poll_logger(void *ctx) {
  int rc = 0;

  (void)ctx;
  rc = klog_persist_flush_default();
  kernel_update_logger_service_status(rc);
  return rc;
}

int kernel_service_start_logger(void *ctx) {
  return kernel_service_poll_logger(ctx);
}

int kernel_service_stop_logger(void *ctx) {
  (void)ctx;
  return klog_persist_flush_default();
}

/* ── update agent service ────────────────────────────────────────────── */

void kernel_update_update_agent_service_status(int rc) {
  struct system_update_status status;
  update_agent_status_get(&status);
  if (rc < 0) {
    (void)service_manager_set_state(SYSTEM_SERVICE_UPDATE_AGENT,
                                    SYSTEM_SERVICE_STATE_DEGRADED, rc,
                                    status.summary);
    return;
  }
  (void)service_manager_set_state(SYSTEM_SERVICE_UPDATE_AGENT,
                                  SYSTEM_SERVICE_STATE_READY, status.last_result,
                                  status.summary);
}

int kernel_service_poll_update_agent(void *ctx) {
  int rc = 0;
  (void)ctx;
  rc = update_agent_poll();
  kernel_update_update_agent_service_status(rc);
  return rc;
}

int kernel_service_start_update_agent(void *ctx) {
  return kernel_service_poll_update_agent(ctx);
}

int kernel_service_stop_update_agent(void *ctx) {
  (void)ctx;
  (void)service_manager_set_state(SYSTEM_SERVICE_UPDATE_AGENT,
                                  SYSTEM_SERVICE_STATE_STOPPED, 0,
                                  "update catalog idle");
  return 0;
}
