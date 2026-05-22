/* kernel_services_work.c — Background work-queue items + dispatcher.
 *
 * Split from kernel_services.c to keep each TU ≤ 900 lines.
 * Owns:
 *   - kernel_work_* callbacks (recovery snapshot, GPU discovery,
 *     USB bring-up + poll, update-agent warmup).
 *   - kernel_update_recovery_snapshot_work() — re-arming policy.
 *   - kernel_schedule_background_boot_work() — initial registration.
 *   - kernel_service_poll() + x64_kernel_runtime_poll_background() —
 *     service-runner dispatcher (M4 phase 1 cooperative path).
 */
#pragma GCC optimize("O0")
#include <stddef.h>
#include <stdint.h>

#include "arch/x86_64/kernel_main_internal.h"
#include "core/work_queue.h"
#include "drivers/gpu/gpu_core.h"
#include "drivers/timer/pit.h"
#include "drivers/usb/usb_core.h"
#include "drivers/usb/usb_hid.h"
#include "kernel/log/klog.h"
#include "services/service_manager.h"
#include "services/service_runner.h"
#include "services/update_agent.h"

/* ── recovery snapshot work item ─────────────────────────────────────── */

int kernel_work_recovery_snapshot(void *ctx) {
  (void)ctx;
  return kernel_persist_recovery_report();
}

int kernel_work_gpu_discovery(void *ctx) {
  (void)ctx;
  klog(KLOG_INFO, "[boot] Background GPU discovery started.");
  (void)gpu_detect();
  klog(KLOG_INFO, "[boot] Background GPU discovery finished.");
  return 0;
}

int kernel_work_usb_bringup(void *ctx) {
  int devices = 0;
  int hid_rc = 0;

  (void)ctx;
  klog(KLOG_INFO, "[boot] Background USB bring-up started.");
  usb_core_init();
  devices = usb_enumerate_devices();
  klog_dec(KLOG_INFO, "[boot] Background USB devices detected: ",
           (uint32_t)devices);
  /* Etapa 3 — Slice 3D: wire the HID class on top of the freshly
   * enumerated USB device table. Without this call the HID keyboard
   * stream never reaches the in-kernel buffer, and the
   * `[smoke] usb-hid-keyboard ready` external gate cannot fire. */
  hid_rc = usb_hid_init();
  if (hid_rc == 0) {
    klog(KLOG_INFO, "[boot] USB HID class initialised.");
    /* Schedule the periodic interrupt-endpoint pump. The interval is
     * deliberately small so HID keyboard latency stays low; the
     * underlying poll is non-blocking and returns immediately when no
     * Transfer Event is pending. */
    (void)work_queue_set_interval(SYSTEM_WORK_USB_POLL, 1u);
    (void)work_queue_schedule_now(SYSTEM_WORK_USB_POLL, pit_ticks());
  } else {
    klog(KLOG_INFO,
         "[boot] USB HID class init skipped (no HID device).");
    (void)work_queue_disable(SYSTEM_WORK_USB_POLL);
  }
  return 0;
}

int kernel_work_usb_poll(void *ctx) {
  (void)ctx;
  /* Etapa 3 — Slice 3D §15.1: detect hot-plug/unplug events first.
   * `usb_hotplug_check` reads PORTSC.CSC for every root-hub port; on
   * change it acks CSC (preserving other change bits via
   * xhci_port_ack_csc) and re-runs enumeration. The cost is N MMIO
   * reads per tick, negligible. Re-enumeration only fires when CSC
   * was actually set, so steady-state is constant work. */
  usb_hotplug_check();
  /* Drain pending Transfer Events from the xHCI interrupter ring into
   * the HID class buffer. Each successful keyboard report ends up in
   * `usb_hid_handle_keyboard_report`, which buffers ASCII into the
   * keyboard ring and observes the Slice 3D smoke gate. */
  usb_poll_all();
  return 0;
}

int kernel_work_update_agent_warmup(void *ctx) {
  int rc = 0;

  (void)ctx;
  if (!g_shell_fs_ready) {
    (void)service_manager_set_state(SYSTEM_SERVICE_UPDATE_AGENT,
                                    SYSTEM_SERVICE_STATE_STOPPED, 0,
                                    "waiting for storage runtime");
    return 0;
  }

  klog(KLOG_INFO, "[boot] Background update-agent warmup started.");
  rc = update_agent_poll();
  kernel_update_update_agent_service_status(rc);
  return rc;
}

void kernel_update_recovery_snapshot_work(int schedule_now) {
  uint64_t now_ticks = pit_ticks();

  if (!g_shell_fs_ready || !g_shell_persistent_storage ||
      g_shell_recovery_ram_fallback) {
    (void)work_queue_disable(SYSTEM_WORK_RECOVERY_SNAPSHOT);
    return;
  }

  (void)work_queue_set_interval(SYSTEM_WORK_RECOVERY_SNAPSHOT, 600u);
  if (schedule_now) {
    (void)work_queue_schedule_now(SYSTEM_WORK_RECOVERY_SNAPSHOT, now_ticks);
  } else {
    (void)work_queue_schedule_after(SYSTEM_WORK_RECOVERY_SNAPSHOT, now_ticks,
                                    600u);
  }
}

void kernel_schedule_background_boot_work(int shell_runtime_ready) {
  uint64_t now_ticks = pit_ticks();

  (void)work_queue_register(SYSTEM_WORK_GPU_DISCOVERY, "gpu-discovery",
                            kernel_work_gpu_discovery, NULL);
  (void)work_queue_register(SYSTEM_WORK_USB_BRINGUP, "usb-bringup",
                            kernel_work_usb_bringup, NULL);
  /* Etapa 3 — Slice 3D: registered idle here, then armed by
   * kernel_work_usb_bringup once usb_hid_init succeeds. Keeping the
   * registration centralised guarantees the binding exists even when
   * USB enumeration finds no HID device (the work stays disabled in
   * that case). */
  (void)work_queue_register(SYSTEM_WORK_USB_POLL, "usb-poll",
                            kernel_work_usb_poll, NULL);
  (void)work_queue_disable(SYSTEM_WORK_USB_POLL);
  (void)work_queue_register(SYSTEM_WORK_UPDATE_AGENT_WARMUP,
                            "update-agent-warmup",
                            kernel_work_update_agent_warmup, NULL);

  (void)work_queue_schedule_after(SYSTEM_WORK_GPU_DISCOVERY, now_ticks, 8u);
  (void)work_queue_schedule_after(SYSTEM_WORK_USB_BRINGUP, now_ticks, 48u);
  if (shell_runtime_ready) {
    (void)service_manager_set_state(SYSTEM_SERVICE_UPDATE_AGENT,
                                    SYSTEM_SERVICE_STATE_STARTING, 0,
                                    "background warmup scheduled");
    (void)work_queue_schedule_after(SYSTEM_WORK_UPDATE_AGENT_WARMUP,
                                    now_ticks, 96u);
  } else {
    (void)service_manager_set_state(SYSTEM_SERVICE_UPDATE_AGENT,
                                    SYSTEM_SERVICE_STATE_STOPPED, 0,
                                    "waiting for storage runtime");
    (void)work_queue_disable(SYSTEM_WORK_UPDATE_AGENT_WARMUP);
  }
}

void kernel_service_poll(void) {
  uint64_t now_ticks = pit_ticks();
  /* M4 phase 1: route through the service-runner so the same step()
   * counter is updated whether we are still cooperative (this path)
   * or already preemptive (phase 8 will dispatch the runner task body
   * directly). The runner step itself drives both service_manager and
   * work_queue polls, so behaviour is preserved bit-for-bit. */
  service_runner_init();
  (void)service_runner_step(now_ticks);
}

void x64_kernel_runtime_poll_background(void) {
  kernel_service_poll();
}
