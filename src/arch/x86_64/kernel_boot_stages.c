/* kernel_boot_stages.c — boot-stage bodies extracted from kernel_main64().
 *
 * This file is part of the kernel_main translation-unit group (see
 * include/arch/x86_64/kernel_main_internal.h). It exists ONLY to keep
 * the kernel_main.c entry point under the source-layout size budget; it
 * holds verbatim copies of boot-stage bodies that previously lived inline
 * inside kernel_main64().
 *
 * Behavior-preservation contract:
 *   - Only stages that run AFTER the fragile pre-COM1 / early-post-EBS
 *     window are extracted here. Handoff/framebuffer validation and
 *     Stage 1-3 (kcon/kinit/PMM/VMM/syscall/SMP/preemptive bring-up,
 *     plus the CAPYOS_BOOT_RUN_HELLO / CAPYOS_PREEMPTIVE_* blocks) stay
 *     inline in kernel_main.c because that window is sensitive to
 *     call/return corruption.
 *   - This TU carries the SAME `#pragma GCC optimize("O0")` as
 *     kernel_main.c so the extracted code keeps identical codegen
 *     characteristics. Do NOT remove the pragma.
 *   - The dbgcon_putc('1'..'8') progress markers and the per-stage
 *     boot_ui_splash and boot_metrics brackets remain in kernel_main64();
 *     none are moved here, so the debug-console marker order is
 *     unchanged.
 */
#pragma GCC optimize("O0")
#include <stddef.h>
#include <stdint.h>

#include "arch/x86_64/kernel_main_internal.h"
#include "arch/x86_64/input_runtime.h"
#include "arch/x86_64/storage_runtime.h"
#include "auth/login_runtime.h"
#include "boot/boot_ui.h"
#include "core/system_init.h"
#include "core/work_queue.h"
#include "drivers/hyperv/hyperv.h"
#include "drivers/input/keyboard.h"
#include "drivers/input/keyboard_layout.h"
#include "kernel/log/klog.h"
#include "kernel/log/klog_persist.h"
#include "net/network_bootstrap.h"
#include "net/stack.h"
#include "services/service_boot_policy.h"
#include "services/service_manager.h"
#include "services/update_agent.h"

/* Defined in framebuffer_console.c (same kernel_main TU group); declared as
 * a bare extern in kernel_main.c, mirrored here for the login_ops builder. */
void login_render_window_layout(void);

/* ── Stage 3 tail: Linux-ABI shim registration ───────────────────────────
 * Pure idempotent pointer-store registrations; no ordering dependency on
 * the fragile early path other than running after x64_timebase_init().
 * Verbatim from kernel_main64() (between markers 'g' and 'h'). */
void kernel_boot_register_linux_abi_shims(void) {
  /* Linux-ABI clock_gettime shim: captures TSC as the boot epoch so
   * future userland calls to clock_gettime(CLOCK_MONOTONIC) see monotonic
   * time relative to this point. Cheap and idempotent; x64_timebase_init
   * has already calibrated tsc_hz. */
  extern void linux_clock_init_boot(void);
  linux_clock_init_boot();
  /* Linux-ABI getrandom shim: install csprng as the source. Done here
   * (before pmm/vmm) so even early callers see a ready pool. */
  extern void linux_random_init_boot(void);
  linux_random_init_boot();
  /* Linux-ABI process/thread/affinity shim: install task accessors so
   * gettid/sched_yield/sched_*affinity/prctl/prlimit64 can resolve.
   * Only registers function pointers; safe to call before the task
   * system fully boots. */
  extern void linux_process_init_boot(void);
  linux_process_init_boot();
  /* Linux-ABI fd shim: pipe2/dup3 wrappers around the kernel pipe
   * primitive. */
  extern void linux_fd_init_boot(void);
  linux_fd_init_boot();
  /* Linux-ABI mmap shim: anonymous private mappings via the VMM
   * demand-page registry. Used by portable userland allocators/JITs. */
  extern void linux_mmap_init_boot(void);
  linux_mmap_init_boot();
  /* Linux-ABI futex shim: pthread mutex/cond wait/wake against
   * the scheduler's task_block / task_unblock_channel. */
  extern void linux_futex_init_boot(void);
  linux_futex_init_boot();
  /* Linux-ABI eventfd2/signalfd4/timerfd shims. eventfd2 and
   * timerfd are functional; signalfd4 is storage-only until
   * signal delivery infrastructure lands. */
  extern void linux_eventfd_init_boot(void);
  linux_eventfd_init_boot();
  /* Linux-ABI net shim (accept4/recvmmsg/sendmmsg). Returns
   * -ENOSYS until BSD sockets land. */
  extern void linux_net_init_boot(void);
  linux_net_init_boot();
  /* Linux-ABI epoll shim: per-epfd interest list with fd_ready
   * polling. yield wired to task_yield. */
  extern void linux_epoll_init_boot(void);
  linux_epoll_init_boot();
  /* Linux-ABI memfd/pidfd shim: pid_exists callback for
   * pidfd_open validation. */
  extern void linux_memfd_init_boot(void);
  linux_memfd_init_boot();
  /* Linux-ABI procfs backend: renders /proc/cpuinfo,
   * /proc/meminfo, /proc/self/{maps,exe,cmdline,status} into
   * per-fd buffers via the linux_proc/linux_cpuinfo formatters.
   * Must be wired BEFORE linux_vfs_init_boot because the router
   * dispatches /proc/<x> paths here. */
  extern void linux_procfs_init_boot(void);
  linux_procfs_init_boot();
  /* Linux-ABI tmpfs backend: in-memory filesystem for /tmp/.
   * Owns its own state; resets the slot table at boot so the
   * kernel starts with an empty /tmp/. Must precede
   * linux_vfs_init_boot for the same reason as procfs. */
  extern void linux_tmpfs_init_boot(void);
  linux_tmpfs_init_boot();
  /* Linux-ABI VFS front-door: file I/O syscalls (open/close/
   * read/write/lseek) routed via linux_vfs_router to
   * linux_devfs, linux_shm, linux_procfs and linux_tmpfs. */
  extern void linux_vfs_init_boot(void);
  linux_vfs_init_boot();
  /* Linux-ABI brk shim: heap region tracker delegating page
   * reservation to vmm_register_anon_region. Used by musl's
   * early malloc before mmap is ready. */
  extern void linux_brk_init_boot(void);
  linux_brk_init_boot();
  /* Linux-ABI arch_prctl shim: x86_64-specific TLS register
   * setup (ARCH_SET_FS / GET_FS / SET_GS / GET_GS) writing the
   * IA32_FS_BASE / IA32_KERNEL_GS_BASE MSRs. Critical for musl
   * TLS bootstrap. */
  extern void linux_arch_prctl_init_boot(void);
  linux_arch_prctl_init_boot();
  /* Linux-ABI exit/exit_group shim: terminates the calling
   * task via task_exit(). Marco M1 single-thread model treats
   * exit and exit_group as equivalent. */
  extern void linux_exit_init_boot(void);
  linux_exit_init_boot();
  /* Note: linux_signal, linux_inotify, linux_clone, linux_shm
   * carry no external state; their `_register_syscalls` hook
   * (or pure formatter API in shm's case) is enough. */
  /* Linux-ABI syscall dispatcher: populate the table by calling
   * the weak `_register` hook of every linux_compat module that
   * linked in (clock, random, process, fd, mmap, futex, eventfd,
   * net, epoll, signal, memfd, inotify). Idempotent. */
  extern void linux_syscall_init(void);
  linux_syscall_init();
}

/* ── Stage 4/8: Keyboard layout + installer setup config ──────────────────
 * Verbatim from kernel_main64() (between boot_metrics "boot-config" begin
 * and end). Wipes the admin-password stack buffer with a volatile loop;
 * that wipe is preserved exactly. */
void kernel_boot_stage_keyboard_setup(void) {
  keyboard_set_layout_by_name("us");
  {
    char handoff_layout_name[16];
    char handoff_language_name[16];
    local_copy(handoff_layout_name, sizeof(handoff_layout_name), "us");
    local_copy(handoff_language_name, sizeof(handoff_language_name), "en");
    if (handoff_keyboard_layout(handoff_layout_name,
                                sizeof(handoff_layout_name)) == 0) {
      (void)keyboard_set_layout_by_name(handoff_layout_name);
    }
    if (handoff_language(handoff_language_name,
                         sizeof(handoff_language_name)) == 0) {
      /* language loaded silently */
    }
    system_set_boot_defaults(handoff_layout_name, handoff_language_name);
  }
  {
    char ho_hostname[32] = {0};
    char ho_theme[16] = {0};
    char ho_admin_user[32] = {0};
    char ho_admin_pass[64] = {0};
    int ho_splash = 1;
    int have_setup = 0;
    if (handoff_hostname(ho_hostname, sizeof(ho_hostname)) == 0 &&
        handoff_admin_username(ho_admin_user, sizeof(ho_admin_user)) == 0 &&
        handoff_admin_password(ho_admin_pass, sizeof(ho_admin_pass)) == 0) {
      have_setup = 1;
      (void)handoff_theme(ho_theme, sizeof(ho_theme));
      int sp = handoff_splash_enabled();
      if (sp >= 0) ho_splash = sp;
      system_set_installer_config(ho_hostname, ho_theme, ho_admin_user,
                                  ho_admin_pass, ho_splash);
      klog(KLOG_INFO, "[setup] Installer config provisioned from handoff.");
    }
    /* Zero password from stack regardless */
    for (size_t zi = 0; zi < sizeof(ho_admin_pass); ++zi)
      ((volatile char *)ho_admin_pass)[zi] = 0;
    (void)have_setup;
  }
  if (load_handoff_volume_key() == 0) {
    klog(KLOG_INFO, "[security] Volume key provisioned from installer.");
  }
}

/* ── Stage 7/8: Input device probe + runtime selection ────────────────────
 * Verbatim from kernel_main64() (the input-probe scoped block). The only
 * transform is `&g_boot_warnings` -> `warnings` (the warnings accumulator
 * stays a kernel_main64 local, passed in by pointer). */
void kernel_boot_stage_input(struct boot_warnings *warnings) {
  int is_hyperv = hyperv_detect();
  struct x64_input_probe_result input_probe;
  x64_input_probe_backends(&input_probe, handoff_has_firmware_input(),
                           handoff_boot_services_active(),
                           g_h->efi_system_table, is_hyperv,
                           klog_print_adapter);

  int has_efi = input_probe.has_efi;
  int has_ps2 = input_probe.has_ps2;
  int has_com1 = input_probe.has_com1;
  int has_hyperv = input_probe.has_hyperv_ready;
  int has_usb = input_probe.has_usb;

  g_com1_ready = has_com1 ? 1 : 0;
  g_serial_mirror = has_com1 ? 1 : 0;

  {
    struct x64_input_config input_config;
    input_config.prefer_native = handoff_boot_services_active() ? 0 : 1;
    input_config.has_efi = has_efi;
    input_config.has_ps2 = has_ps2;
    input_config.has_hyperv = has_hyperv;
    input_config.hyperv_deferred = input_probe.has_hyperv_deferred;
    input_config.has_com1 = has_com1;
    input_config.has_usb = has_usb;
    input_config.efi_system_table = input_probe.efi_system_table;
    x64_input_runtime_init(&g_input_runtime, &input_config);
  }
  update_system_runtime_platform_status();

  if (!x64_input_has_any(&g_input_runtime)) {
    boot_warnings_add(warnings,
                      "No input device detected (keyboard/serial)");
  }
}

/* ── Stage 8/8: Shell runtime + storage policy + network + boot policy ────
 * Verbatim from kernel_main64() (the runtime-network scoped block). The
 * only transform is `&g_boot_warnings` -> `warnings`. */
void kernel_boot_stage_network_and_policy(struct boot_warnings *warnings) {
  int shell_runtime_rc;
  int validated_storage_ready = 0;
  int network_status_available = 0;
  int validated_network_supported = 0;
  struct system_update_status update_status;
  struct system_service_boot_policy_input boot_policy_input;

  fbcon_set_visual_muted(0);
  shell_runtime_rc = prepare_shell_runtime();

  /* Retry runtime preparation once as autocorrection before we proceed
   * to the remaining boot stages. */
  if (shell_runtime_rc != 0) {
    fbcon_print("[setup] Autocorrecao: retentando preparacao do runtime...\n");
    shell_runtime_rc = prepare_shell_runtime();
  }

  if (shell_runtime_rc == 0) {
    int log_flush_rc = klog_persist_flush_default();
    kernel_update_logger_service_status(log_flush_rc);
    (void)service_manager_set_poll(SYSTEM_SERVICE_LOGGER,
                                   kernel_service_poll_logger, NULL);
    (void)service_manager_set_control(SYSTEM_SERVICE_LOGGER,
                                      kernel_service_start_logger,
                                      kernel_service_stop_logger, NULL);
    (void)service_manager_set_poll_interval(SYSTEM_SERVICE_LOGGER, 300u);
    (void)service_manager_set_restart_limit(SYSTEM_SERVICE_LOGGER, 0u);
    kernel_update_recovery_snapshot_work(0);
  } else {
    (void)service_manager_set_state(
        SYSTEM_SERVICE_LOGGER, SYSTEM_SERVICE_STATE_DEGRADED,
        shell_runtime_rc, "filesystem unavailable; ring buffer only");
    (void)work_queue_disable(SYSTEM_WORK_RECOVERY_SNAPSHOT);
  }
  fbcon_set_visual_muted(1);

  if (shell_runtime_rc != 0) {
    klog(KLOG_INFO, "[net] Shell runtime unavailable; using defaults.");
    boot_warnings_add(warnings,
                      "Storage runtime unavailable; persistence may fail");
  } else if (!x64_storage_runtime_has_device()) {
    if (x64_storage_runtime_hyperv_present()) {
      validated_storage_ready = 0;
      boot_warnings_add(
          warnings,
          "Hyper-V persistent storage unavailable; setup blocked until DATA is persistent");
    } else {
      validated_storage_ready = 1;
      boot_warnings_add(
          warnings,
          "Persistent storage unavailable; configuration will NOT survive reboot");
    }
  } else if (!g_shell_persistent_storage) {
    if (x64_storage_runtime_hyperv_present()) {
      validated_storage_ready = 0;
      boot_warnings_add(
          warnings,
          "Hyper-V persistent volume not mounted; maintenance prevents non-persistent setup");
    } else {
      validated_storage_ready = 1;
      boot_warnings_add(
          warnings,
          "Persistent volume not mounted; running in RAM (no persistence)");
    }
  } else {
    validated_storage_ready = 1;
  }
  struct network_bootstrap_io net_io;
  net_io.print = klog_print_adapter;
  net_io.print_dec_u32 = fbcon_print_dec_u32;
  net_io.print_hex16 = fbcon_print_hex16;
  net_io.print_ipv4 = fbcon_print_ipv4;
  net_io.print_mac = fbcon_print_mac;
  net_io.putc = fbcon_putc;
  network_bootstrap_run(&net_io, &g_shell_settings);
  g_network_runtime_refresh_enabled = 1;
  (void)service_manager_set_poll(SYSTEM_SERVICE_NETWORKD,
                                 kernel_service_poll_networkd, NULL);
  (void)service_manager_set_control(SYSTEM_SERVICE_NETWORKD,
                                    kernel_service_start_networkd,
                                    kernel_service_stop_networkd, NULL);
  (void)service_manager_set_poll_interval(SYSTEM_SERVICE_NETWORKD, 10u);
  (void)service_manager_set_restart_limit(SYSTEM_SERVICE_NETWORKD, 3u);
  kernel_maybe_refresh_network_runtime();
  (void)service_manager_set_poll(SYSTEM_SERVICE_UPDATE_AGENT,
                                 kernel_service_poll_update_agent, NULL);
  (void)service_manager_set_control(SYSTEM_SERVICE_UPDATE_AGENT,
                                    kernel_service_start_update_agent,
                                    kernel_service_stop_update_agent, NULL);
  (void)service_manager_set_poll_interval(SYSTEM_SERVICE_UPDATE_AGENT, 1800u);
  (void)service_manager_set_dependencies(
      SYSTEM_SERVICE_UPDATE_AGENT, (1u << SYSTEM_SERVICE_LOGGER));
  (void)service_manager_set_restart_limit(SYSTEM_SERVICE_UPDATE_AGENT, 3u);
  (void)service_manager_set_poll(SYSTEM_SERVICE_CAPYPKG,
                                 kernel_service_poll_capypkg, NULL);
  (void)service_manager_set_control(SYSTEM_SERVICE_CAPYPKG,
                                    kernel_service_start_capypkg,
                                    kernel_service_stop_capypkg, NULL);
  /* 60s poll lets the auto-bootstrap (`kernel_capypkg_maybe_bootstrap`)
   * retry quickly while the network warms up after boot. Once the
   * `/system/install/bootstrap.done` marker is written, each poll
   * is essentially a stat() + early return. */
  (void)service_manager_set_poll_interval(SYSTEM_SERVICE_CAPYPKG, 60u);
  (void)service_manager_set_dependencies(
      SYSTEM_SERVICE_CAPYPKG, (1u << SYSTEM_SERVICE_LOGGER));
  (void)service_manager_set_restart_limit(SYSTEM_SERVICE_CAPYPKG, 3u);
  update_agent_status_get(&update_status);

  {
    struct net_stack_status net_status = {0};
    kernel_update_network_service_status();
    if (net_stack_status(&net_status) != 0) {
      boot_warnings_add(warnings,
                        "Network status unavailable; continuing offline");
      network_status_available = 0;
    } else if (!net_status.nic.found) {
      network_status_available = 1;
      boot_warnings_add(warnings,
                        "No network adapter detected");
    } else if (!net_status.runtime_supported) {
      network_status_available = 1;
      if (net_status.nic.kind == NET_NIC_KIND_VMXNET3) {
        boot_warnings_add(warnings,
                          "VMXNET3 detected; use VMware E1000 for now");
      } else {
        boot_warnings_add(warnings,
                          "Detected network adapter has no validated driver");
      }
    } else if (!net_status.ready) {
      network_status_available = 1;
      validated_network_supported = 1;
      boot_warnings_add(warnings,
                        "Validated network driver detected but not ready");
    } else {
      network_status_available = 1;
      validated_network_supported = 1;
    }
  }
  if (update_status.last_result < 0) {
    boot_warnings_add(warnings,
                      "Local update state is inconsistent; review staging");
  } else if (update_status.pending_activation) {
    boot_warnings_add(
        warnings,
        "Staged update activation is armed; payload activation is still manual");
  } else if (update_status.stage_ready) {
    boot_warnings_add(warnings,
                      "A staged update is present in persistent storage");
  }

  boot_policy_input.requested_target =
      kernel_service_target_from_settings(&g_shell_settings);
  boot_policy_input.shell_runtime_ready = shell_runtime_rc == 0 ? 1u : 0u;
  boot_policy_input.validated_storage_ready =
      validated_storage_ready ? 1u : 0u;
  boot_policy_input.network_status_available =
      network_status_available ? 1u : 0u;
  boot_policy_input.validated_network_supported =
      validated_network_supported ? 1u : 0u;
  service_boot_policy_evaluate(&boot_policy_input, &g_boot_policy_decision);
  kernel_log_boot_policy_decision(&g_boot_policy_decision);
  if (g_boot_policy_decision.degraded) {
    boot_warnings_add(warnings,
                      service_boot_policy_reason_summary(
                          g_boot_policy_decision.reason));
  }
  (void)service_manager_target_apply(g_boot_policy_decision.final_target);
  g_runtime_maintenance_mode =
      (g_boot_policy_decision.final_target ==
       SYSTEM_SERVICE_TARGET_MAINTENANCE)
          ? 1
          : 0;
  g_recovery_login_requested = 0;
  kernel_persist_recovery_artifacts("boot-policy");
  kernel_update_recovery_snapshot_work(0);
  kernel_schedule_background_boot_work(shell_runtime_rc == 0);
}

/* ── Post-splash: build the login_runtime_ops dispatch table ──────────────
 * Verbatim field assignments from kernel_main64(); the only transform is
 * `login_ops.` -> `out->`. The caller still owns the struct on its stack
 * and still runs login_runtime_run() + the 'L' marker inline. */
void kernel_boot_build_login_ops(struct login_runtime_ops *out) {
  out->has_any_input = x64_input_has_any(&g_input_runtime);
  out->maintenance_mode = kernel_boots_in_maintenance_mode();
  out->shell_ctx = &g_shell_ctx;
  out->session_ctx = &g_session_ctx;
  out->settings = &g_shell_settings;
  out->maintenance_reason = kernel_boot_maintenance_reason();
  out->prepare_shell_runtime = prepare_shell_runtime;
  out->maintenance_session_start = kernel_start_maintenance_session;
  out->init_shell_context_user = init_shell_context;
  out->dispatch_shell_command = try_shell_command;
  out->run_shell_alias = run_shell_alias;
  out->is_equal = streq;
  out->readline = kernel_readline;
  out->session_reset = login_session_reset;
  out->session_set_active = login_session_set_active;
  out->shell_context_init = login_shell_context_init;
  out->system_login = login_system_login;
  out->session_user = login_session_user;
  out->session_cwd = login_session_cwd;
  out->shell_context_should_logout = login_shell_context_should_logout;
  out->print = fbcon_print;
  out->putc = fbcon_putc;
  out->clear_view = fbcon_clear_view;
  out->show_splash = login_show_splash;
  out->ui_banner = ui_banner;
  out->render_window_layout = login_render_window_layout;
  out->cmd_info = cmd_info;
  out->service_poll = kernel_service_poll;
  out->maintenance_mode_active = login_maintenance_mode_active;
  out->consume_recovery_login_request =
      login_consume_recovery_login_request;
}

__asm__(".section .note.GNU-stack,\"\",@progbits");
