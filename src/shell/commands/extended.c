#include "shell/commands_extended.h"
#include "shell/core.h"
#include "kernel/task.h"
#include "kernel/scheduler.h"
#include "memory/pmm.h"
#include "memory/vmm.h"
#include "boot/boot_metrics.h"
#include "boot/boot_slot.h"
#include "auth/auth_policy.h"
#include "auth/privilege.h"
#include "arch/x86_64/smp.h"
#include "arch/x86_64/apic.h"
#include "arch/x86_64/kernel_shell_dispatch.h"
#include "arch/x86_64/storage_boot_provider_policy.h"
#include "arch/x86_64/storage_runtime.h"
#include "drivers/gpu/gpu_core.h"
#include "drivers/rtc/rtc.h"
#include "drivers/timer/pit.h"
#include "net/socket.h"
#include "net/dns_cache.h"
#include "fs/fsck.h"
#include "drivers/input/mouse.h"
#include "drivers/input/keyboard_layout.h"
#include "kernel/module_gate.h"
#include "kernel/user_init.h"
#include "gui/desktop_runtime.h"
#include "services/capyai_system_actions.h"
#ifdef CAPYOS_HAVE_CAPYAI
#include "services/capyai.h"
#endif
#ifndef CAPYOS_PROFILE_CORE_ONLY
#include "gui/desktop.h"
#include "apps/calculator.h"
#include "apps/file_manager.h"
#include "apps/text_editor.h"
#include "apps/task_manager.h"
#include "apps/capyai_chat.h"
#include "apps/settings.h"
#endif
#include "security/tls.h"
#include "arch/x86_64/framebuffer_console.h"
#include "drivers/pcie.h"
#include <stddef.h>

static void print_adapter(const char *s) { fbcon_print(s); }

static void print_u32(uint32_t v) {
  char buf[12]; int p = 0;
  if (v == 0) { fbcon_putc('0'); return; }
  char tmp[12]; int tp = 0;
  while (v > 0) { tmp[tp++] = '0' + (v % 10); v /= 10; }
  for (int i = tp - 1; i >= 0; i--) buf[p++] = tmp[i];
  buf[p] = 0;
  fbcon_print(buf);
}

static void print_u64(uint64_t v) {
  char buf[24]; int p = 0;
  if (v == 0) { fbcon_putc('0'); return; }
  char tmp[24]; int tp = 0;
  while (v > 0) { tmp[tp++] = '0' + (char)(v % 10); v /= 10; }
  for (int i = tp - 1; i >= 0; i--) buf[p++] = tmp[i];
  buf[p] = 0;
  fbcon_print(buf);
}

static void print_hex16(uint16_t v) {
  static const char hex[] = "0123456789ABCDEF";
  fbcon_putc(hex[(v >> 12) & 0xF]);
  fbcon_putc(hex[(v >> 8) & 0xF]);
  fbcon_putc(hex[(v >> 4) & 0xF]);
  fbcon_putc(hex[v & 0xF]);
}

static void print_hex8(uint8_t v) {
  static const char hex[] = "0123456789ABCDEF";
  fbcon_putc(hex[(v >> 4) & 0xF]);
  fbcon_putc(hex[v & 0xF]);
}

static const char *pci_class_name(uint8_t cls) {
  switch (cls) {
  case 0x00: return "Unclassified";
  case 0x01: return "Storage";
  case 0x02: return "Network";
  case 0x03: return "Display";
  case 0x04: return "Multimedia";
  case 0x05: return "Memory";
  case 0x06: return "Bridge";
  case 0x07: return "Communication";
  case 0x08: return "System";
  case 0x09: return "Input";
  case 0x0C: return "Serial Bus";
  case 0x0D: return "Wireless";
  default:   return "Other";
  }
}

/* --- print-pci: scan and list all PCI devices --- */
static int cmd_print_pci(struct shell_context *ctx, int argc, char **argv) {
  (void)ctx; (void)argc; (void)argv;
  int count = 0;
  fbcon_print("PCI devices:\n");
  fbcon_print("  Bus Dev Fn  Vendor Device Class    Name\n");
  for (uint16_t bus = 0; bus < 256; bus++) {
    for (uint8_t dev = 0; dev < 32; dev++) {
      for (uint8_t func = 0; func < 8; func++) {
        uint16_t vendor = pci_config_read16((uint8_t)bus, dev, func, PCI_VENDOR_ID);
        if (vendor == 0xFFFF || vendor == 0x0000) {
          if (func == 0) break;
          continue;
        }
        uint16_t device_id = pci_config_read16((uint8_t)bus, dev, func, PCI_DEVICE_ID);
        uint32_t class_rev = pci_config_read32((uint8_t)bus, dev, func, PCI_CLASS_REVISION);
        uint8_t class_code = (uint8_t)(class_rev >> 24);
        uint8_t subclass = (uint8_t)(class_rev >> 16);
        fbcon_print("  ");
        print_hex8((uint8_t)bus); fbcon_print("  ");
        print_hex8(dev); fbcon_print("  ");
        print_hex8(func); fbcon_print("  ");
        print_hex16(vendor); fbcon_print("  ");
        print_hex16(device_id); fbcon_print("  ");
        print_hex8(class_code); fbcon_putc('.'); print_hex8(subclass);
        fbcon_print("  "); fbcon_print(pci_class_name(class_code));
        fbcon_putc('\n');
        count++;
        if (func == 0) {
          uint8_t hdr = pci_config_read8((uint8_t)bus, dev, func, PCI_HEADER_TYPE);
          if ((hdr & 0x80) == 0) break;
        }
      }
    }
  }
  fbcon_print("Total: "); print_u32((uint32_t)count); fbcon_print(" devices\n");
  return 0;
}

/* --- print-tasks: show running tasks --- */
static int cmd_print_tasks(struct shell_context *ctx, int argc, char **argv) {
  (void)ctx; (void)argc; (void)argv;
  task_list(print_adapter);
  return 0;
}

/* --- print-mem: show memory stats --- */
static int cmd_print_mem(struct shell_context *ctx, int argc, char **argv) {
  (void)ctx; (void)argc; (void)argv;
  struct pmm_stats ps;
  pmm_stats_get(&ps);
  fbcon_print("Physical memory:\n");
  fbcon_print("  total pages: "); print_u64(ps.total_pages); fbcon_putc('\n');
  fbcon_print("  free pages:  "); print_u64(ps.free_pages); fbcon_putc('\n');
  fbcon_print("  used pages:  "); print_u64(ps.used_pages); fbcon_putc('\n');
  fbcon_print("  free bytes:  "); print_u64(ps.free_bytes); fbcon_putc('\n');
  return 0;
}

/* --- print-cpus: show SMP info --- */
static int cmd_print_cpus(struct shell_context *ctx, int argc, char **argv) {
  (void)ctx; (void)argc; (void)argv;
  struct smp_info si;
  smp_get_info(&si);
  fbcon_print("CPUs detected: "); print_u32(si.cpu_count); fbcon_putc('\n');
  fbcon_print("CPUs online:   "); print_u32(si.online_count); fbcon_putc('\n');
  fbcon_print("BSP APIC ID:   "); print_u32(si.bsp_apic_id); fbcon_putc('\n');
  for (uint32_t i = 0; i < si.cpu_count; i++) {
    fbcon_print("  CPU "); print_u32(i);
    fbcon_print("  apic="); print_u32(si.cpus[i].apic_id);
    fbcon_print(si.cpus[i].is_bsp ? " [BSP]" : "      ");
    const char *st = "?";
    switch (si.cpus[i].state) {
      case CPU_STATE_OFFLINE: st = "offline"; break;
      case CPU_STATE_STARTING: st = "starting"; break;
      case CPU_STATE_ONLINE: st = "online"; break;
      case CPU_STATE_IDLE: st = "idle"; break;
      case CPU_STATE_HALTED: st = "halted"; break;
    }
    fbcon_print(" "); fbcon_print(st); fbcon_putc('\n');
  }
  return 0;
}

/* --- print-gpu: show GPU info --- */
static int cmd_print_gpu(struct shell_context *ctx, int argc, char **argv) {
  (void)ctx; (void)argc; (void)argv;
  struct gpu_info gi;
  gpu_get_info(&gi);
  fbcon_print("GPU driver: "); fbcon_print(gpu_driver_name()); fbcon_putc('\n');
  if (gi.device.detected) {
    fbcon_print("  vendor: 0x"); print_u32(gi.device.vendor_id); fbcon_putc('\n');
    fbcon_print("  device: 0x"); print_u32(gi.device.device_id); fbcon_putc('\n');
    fbcon_print("  NVIDIA: "); fbcon_print(gpu_is_nvidia() ? "yes" : "no"); fbcon_putc('\n');
  } else {
    fbcon_print("  No discrete GPU detected (using UEFI GOP)\n");
  }
  return 0;
}

/* --- print-clock: show real time --- */
static int cmd_print_clock(struct shell_context *ctx, int argc, char **argv) {
  (void)ctx; (void)argc; (void)argv;
  struct rtc_time rt;
  rtc_read(&rt);
  char tbuf[16], dbuf[16];
  rtc_format_time(&rt, tbuf, sizeof(tbuf));
  rtc_format_date(&rt, dbuf, sizeof(dbuf));
  fbcon_print(dbuf); fbcon_putc(' '); fbcon_print(tbuf); fbcon_putc('\n');
  return 0;
}

/* --- print-boot-times: show boot stage timings --- */
static int cmd_print_boot_times(struct shell_context *ctx, int argc, char **argv) {
  (void)ctx; (void)argc; (void)argv;
  boot_metrics_print(print_adapter);
  return 0;
}

/* --- print-sockets: show socket stats --- */
static int cmd_print_sockets(struct shell_context *ctx, int argc, char **argv) {
  (void)ctx; (void)argc; (void)argv;
  struct socket_stats ss;
  socket_stats_get(&ss);
  fbcon_print("Sockets:\n");
  fbcon_print("  active:   "); print_u32(ss.active_sockets); fbcon_putc('\n');
  fbcon_print("  sent:     "); print_u64(ss.bytes_sent); fbcon_print(" bytes\n");
  fbcon_print("  received: "); print_u64(ss.bytes_received); fbcon_print(" bytes\n");
  return 0;
}

/* --- print-dns-cache: show DNS cache --- */
static int cmd_print_dns_cache(struct shell_context *ctx, int argc, char **argv) {
  (void)ctx; (void)argc; (void)argv;
  struct dns_cache_stats ds;
  dns_cache_stats_get(&ds);
  fbcon_print("DNS cache:\n");
  fbcon_print("  entries: "); print_u32(ds.entries); fbcon_putc('\n');
  fbcon_print("  hits:    "); print_u64(ds.hits); fbcon_putc('\n');
  fbcon_print("  misses:  "); print_u64(ds.misses); fbcon_putc('\n');
  fbcon_print("  expired: "); print_u64(ds.expired); fbcon_putc('\n');
  return 0;
}

static int cmd_print_last_tls(struct shell_context *ctx, int argc, char **argv) {
  struct tls_security_info info;
  (void)ctx; (void)argc; (void)argv;

  fbcon_print("TLS last session:\n");
  fbcon_print("  state: "); fbcon_print(tls_state_name(tls_last_state())); fbcon_putc('\n');
  fbcon_print("  error: "); fbcon_print(tls_alert_name(tls_last_error())); fbcon_putc('\n');
  if (tls_get_last_security_info(&info) != 0 || info.protocol_version == 0) {
    fbcon_print("  no negotiated session data\n");
    return 0;
  }
  fbcon_print("  version: "); fbcon_print(tls_version_name(info.protocol_version)); fbcon_putc('\n');
  fbcon_print("  cipher:  "); fbcon_print(tls_cipher_suite_name(info.cipher_suite)); fbcon_putc('\n');
  fbcon_print("  anchors: "); print_u32(info.trust_anchor_count); fbcon_putc('\n');
  fbcon_print("  peer verified: "); fbcon_print(info.peer_verified ? "yes" : "no"); fbcon_putc('\n');
  fbcon_print("  hostname ok:   "); fbcon_print(info.hostname_validated ? "yes" : "no"); fbcon_putc('\n');
  fbcon_print("  custom anchor: "); fbcon_print(info.custom_anchor_loaded ? "yes" : "no"); fbcon_putc('\n');
  fbcon_print("  alpn: "); fbcon_print(info.alpn[0] ? info.alpn : "(none)"); fbcon_putc('\n');
  return 0;
}

/* --- print-boot-slot: show A/B slot status --- */
static int cmd_print_boot_slot(struct shell_context *ctx, int argc, char **argv) {
  struct x64_storage_boot_provider_status provider;
  (void)ctx; (void)argc; (void)argv;
  boot_slot_status(print_adapter);
  /* The persistent A/B capability is what turns `update-apply` from -60 into a
   * real transition, so its verdict belongs next to the slot table. */
  if (x64_storage_runtime_boot_provider_status(&provider) != 0) {
    fbcon_print("Boot provider: ready=no reason=unavailable\n");
    return 0;
  }
  fbcon_print("Boot provider: ready=");
  fbcon_print(provider.provider_ready ? "yes" : "no");
  fbcon_print(" reason=");
  fbcon_print(x64_storage_boot_provider_reason_label(provider.reason));
  fbcon_putc('\n');
  return 0;
}

/* --- auth-status: show auth lockout policy --- */
static int cmd_auth_status(struct shell_context *ctx, int argc, char **argv) {
  (void)ctx; (void)argc; (void)argv;
  auth_policy_status(print_adapter);
  return 0;
}

/* --- scheduler-stats: show scheduler stats --- */
static int cmd_scheduler_stats(struct shell_context *ctx, int argc, char **argv) {
  (void)ctx; (void)argc; (void)argv;
  struct scheduler_stats ss;
  scheduler_stats_get(&ss);
  fbcon_print("Scheduler:\n");
  fbcon_print("  switches: "); print_u64(ss.total_switches); fbcon_putc('\n');
  fbcon_print("  ticks:    "); print_u64(ss.total_ticks); fbcon_putc('\n');
  fbcon_print("  idle:     "); print_u64(ss.idle_ticks); fbcon_putc('\n');
  fbcon_print("  runnable: "); print_u32(ss.runnable_count); fbcon_putc('\n');
  fbcon_print("  blocked:  "); print_u32(ss.blocked_count); fbcon_putc('\n');
  fbcon_print("  sleeping: "); print_u32(ss.sleeping_count); fbcon_putc('\n');
  return 0;
}

#ifndef CAPYOS_PROFILE_CORE_ONLY
/* alpha.241 activation gate: refuse to start the desktop session
 * unless the org.capyos.ui.desktop-session module has actually been
 * staged by capypkg. The kernel ELF carries the desktop code when
 * PROFILE=full but exposing it before the user opted in (via the
 * first-boot wizard or `capy install`) would defeat the whole
 * modular install story. The message instructs the operator on the
 * minimal next step. */
static int desktop_gate_block_message(void) {
  fbcon_print(
      "Desktop module not installed. Run `capy install "
      "org.capyos.ui.desktop-session` or rerun `capy wizard`.\n");
  return -1;
}

static int ensure_desktop(struct shell_context *ctx) {
  if (desktop_is_active()) return 0;
  if (!ctx) { fbcon_print("No shell context for desktop.\n"); return -1; }
  if (!kernel_module_desktop_session_available()) {
    return desktop_gate_block_message();
  }
  return desktop_runtime_start(ctx);
}

static int cmd_desktop_start(struct shell_context *ctx, int argc, char **argv) {
  (void)argc; (void)argv;
  if (!kernel_module_desktop_session_available()) {
    return desktop_gate_block_message();
  }
  return desktop_runtime_start(ctx);
}

static int cmd_open_calc(struct shell_context *c, int a, char **v) {
  (void)a;(void)v;
  if (!desktop_is_active() && ensure_desktop(c) != 0) { return -1; }
  calculator_open(); return 0;
}
static int cmd_open_files(struct shell_context *c, int a, char **v) {
  (void)a;(void)v;
  if (!desktop_is_active() && ensure_desktop(c) != 0) { return -1; }
  file_manager_open(); return 0;
}
static int cmd_open_editor(struct shell_context *c, int a, char **v) {
  (void)v;
  if (!desktop_is_active() && ensure_desktop(c) != 0) { return -1; }
  text_editor_open(a > 1 ? v[1] : NULL); return 0;
}
static int cmd_open_tasks(struct shell_context *c, int a, char **v) {
  (void)a;(void)v;
  if (!desktop_is_active() && ensure_desktop(c) != 0) { return -1; }
  task_manager_open(); return 0;
}
static int cmd_open_capyai(struct shell_context *c, int a, char **v) {
  (void)a;(void)v;
  if (!desktop_is_active()) {
    /* Queue the app before entering the synchronous desktop loop; the runtime
     * consumes it immediately after compositor initialization. */
    (void)desktop_launch_capyai();
    return ensure_desktop(c);
  }
  if (desktop_launch_capyai() != 0) {
    shell_print_error("CapyAI nao abriu: memoria de superficie insuficiente.");
    return -1;
  }
  return 0;
}
static int cmd_open_settings(struct shell_context *c, int a, char **v) {
  (void)a;(void)v;
  if (!desktop_is_active() && ensure_desktop(c) != 0) { return -1; }
  settings_open(); return 0;
}
/* Etapa 7 / Slice 7.5 (alpha.304): launch the ring-3 graphical browser
 * (capygfx) as a REAL process alongside the running desktop session, unlike
 * the apps above (in-kernel functions, no process). Only present when the
 * kernel was built with the blob embedded (CAPYOS_DESKTOP_GRAPHICAL_BROWSER)
 * -- reports a clear message otherwise instead of silently failing. See
 * kernel_spawn_capygfx_desktop (include/kernel/user_init.h) for the spawn
 * mechanics (armed for first dispatch + scheduler_add, no noreturn). */
static int cmd_open_browser_graphical(struct shell_context *c, int a, char **v) {
  (void)a; (void)v;
#ifdef CAPYOS_DESKTOP_GRAPHICAL_BROWSER
  int rc;
  if (!desktop_is_active() && ensure_desktop(c) != 0) { return -1; }
  rc = kernel_spawn_capygfx_desktop();
  if (rc != KERNEL_SPAWN_OK) {
    fbcon_print("Failed to launch the graphical browser (spawn error).\n");
    return -1;
  }
  return 0;
#else
  (void)c;
  fbcon_print(
      "Graphical browser not built into this kernel (rebuild with "
      "CAPYOS_DESKTOP_GRAPHICAL_BROWSER=1).\n");
  return -1;
#endif
}

/* Governed application bridge used by CapyAI.  The shell-visible command has
 * one argument only because capyai_system_actions validates it against the
 * fixed seven-ID registry before any callback is selected.  Graphical work is
 * queued here and performed later by capyai_system_actions_pump() on the
 * desktop foreground frame. */
struct capyai_window_close_context {
  const char *title;
};

static int app_open_calculator(void *ctx) {
  (void)ctx;
  calculator_open();
  return compositor_find_window_by_title("Calculator") ? 0 : -1;
}

static int app_open_files(void *ctx) {
  (void)ctx;
  file_manager_open();
  return compositor_find_window_by_title("File Manager") ? 0 : -1;
}

static int app_open_editor(void *ctx) {
  (void)ctx;
  text_editor_open(NULL);
  return compositor_find_window_by_title("Text Editor") ? 0 : -1;
}

static int app_open_tasks(void *ctx) {
  (void)ctx;
  task_manager_open();
  return compositor_find_window_by_title("Task Manager") ? 0 : -1;
}

static int app_open_settings(void *ctx) {
  (void)ctx;
  settings_open();
  return compositor_find_window_by_title("Settings") ? 0 : -1;
}

static int app_open_capyai(void *ctx) {
  (void)ctx;
  return desktop_launch_capyai();
}

static int app_open_browser(void *ctx) {
  (void)ctx;
#ifdef CAPYOS_DESKTOP_GRAPHICAL_BROWSER
  return kernel_spawn_capygfx_desktop() == KERNEL_SPAWN_OK ? 0 : -1;
#else
  return -1;
#endif
}

static int app_open_terminal(void *ctx) {
  (void)ctx;
  return desktop_open_terminal_window();
}

static int app_close_window(void *ctx) {
  const struct capyai_window_close_context *close_ctx =
      (const struct capyai_window_close_context *)ctx;
  struct gui_window *window;
  uint32_t window_id;
  if (!close_ctx || !close_ctx->title) return -1;
  window = compositor_find_window_by_title(close_ctx->title);
  if (!window) return -1;
  window_id = window->id;
  compositor_destroy_window(window_id);
  return compositor_window_exists(window_id) ? -1 : 0;
}

static int app_close_browser(void *ctx) {
  (void)ctx;
#ifdef CAPYOS_DESKTOP_GRAPHICAL_BROWSER
  return kernel_close_capygfx_desktop();
#else
  return -1;
#endif
}

static int capyai_install_desktop_app_bindings(void) {
  static const struct capyai_window_close_context close_calculator = {"Calculator"};
  static const struct capyai_window_close_context close_files = {"File Manager"};
  static const struct capyai_window_close_context close_editor = {"Text Editor"};
  static const struct capyai_window_close_context close_tasks = {"Task Manager"};
  static const struct capyai_window_close_context close_settings = {"Settings"};
  static const struct capyai_window_close_context close_capyai = {"CapyAI"};
  static const struct capyai_window_close_context close_terminal = {"Terminal"};
  static const struct capyai_system_app_binding bindings[CAPYAI_SYSTEM_APP_COUNT] = {
      {"calculator", app_open_calculator, app_close_window, (void *)&close_calculator},
      {"files", app_open_files, app_close_window, (void *)&close_files},
      {"editor", app_open_editor, app_close_window, (void *)&close_editor},
      {"tasks", app_open_tasks, app_close_window, (void *)&close_tasks},
      {"settings", app_open_settings, app_close_window, (void *)&close_settings},
      {"capyai", app_open_capyai, app_close_window, (void *)&close_capyai},
      {"browser", app_open_browser, app_close_browser, NULL},
      {"terminal", app_open_terminal, app_close_window, (void *)&close_terminal},
  };
  if (capyai_system_apps_install(bindings, CAPYAI_SYSTEM_APP_COUNT) != 0) {
    return -1;
  }
  return 0;
}

static int cmd_capyai_app_action(struct shell_context *ctx, int argc,
                                 char **argv, uint32_t action) {
  int rc;
  const char *verb;
  (void)ctx;
  verb = action == CAPYAI_SYSTEM_APP_OPEN ? "app-open" : "app-close";
  if (shell_help_requested(argc, argv)) {
    fbcon_print("Usage: ");
    fbcon_print(verb);
    fbcon_print(" <calculator|files|editor|tasks|settings|capyai|browser|terminal>\n");
    return 0;
  }
  if (argc != 2 || !capyai_system_app_id_valid(argv[1])) {
    shell_print_error("Aplicativo nao registrado. Use um ID exibido no help.");
    return -1;
  }
  if (!desktop_is_active() || capyai_install_desktop_app_bindings() != 0) {
    shell_print_error("Desktop indisponivel para a acao solicitada.");
    return -1;
  }
  /* Shell commands dispatched by the graphical terminal already run on the
   * desktop foreground task. Waiting on the worker queue here deadlocks the
   * per-frame pump, so this compatibility path executes the same fixed
   * registry callback synchronously. CapyAI workers still use the queued
   * native dispatch below. */
  rc = capyai_system_app_execute_foreground(action, argv[1]);
  if (rc != CAPYAI_SYSTEM_ACTION_OK) {
    shell_print_error("O aplicativo nao aceitou a acao solicitada.");
    return -1;
  }
  shell_print_ok(action == CAPYAI_SYSTEM_APP_OPEN ? "Aplicativo aberto."
                                                  : "Aplicativo fechado.");
  return 0;
}

static int cmd_capyai_app_open(struct shell_context *ctx, int argc,
                               char **argv) {
  return cmd_capyai_app_action(ctx, argc, argv, CAPYAI_SYSTEM_APP_OPEN);
}

static int cmd_capyai_app_close(struct shell_context *ctx, int argc,
                                char **argv) {
  return cmd_capyai_app_action(ctx, argc, argv, CAPYAI_SYSTEM_APP_CLOSE);
}

#ifdef CAPYOS_HAVE_CAPYAI
static void capyai_system_detail(char *detail, size_t detail_size,
                                 const char *prefix, const char *app_id) {
  size_t n = 0u;
  size_t i = 0u;
  if (!detail || detail_size == 0u) return;
  while (prefix && prefix[i] && n + 1u < detail_size) detail[n++] = prefix[i++];
  i = 0u;
  while (app_id && app_id[i] && n + 1u < detail_size) detail[n++] = app_id[i++];
  detail[n] = '\0';
}

int capyai_native_system_dispatch(void *ctx,
                                  const struct capy_ai_output *tool_call,
                                  char *detail, size_t detail_size) {
  struct shell_context *shell_ctx = (struct shell_context *)ctx;
  uint32_t action;
  int rc;
  (void)ctx;
  if (!tool_call || !detail || detail_size == 0u) return 127;
  detail[0] = '\0';
  if (shell_string_equal(tool_call->action, "app_open")) {
    action = CAPYAI_SYSTEM_APP_OPEN;
  } else if (shell_string_equal(tool_call->action, "app_close")) {
    action = CAPYAI_SYSTEM_APP_CLOSE;
  } else {
    const struct session_context *session =
        shell_ctx ? shell_context_session(shell_ctx) : NULL;
    if (!shell_string_equal(tool_call->action, "power_schedule_status") &&
        !privilege_session_is_admin(session)) {
      privilege_log_denied("capyai-power", session_user(session));
      capyai_system_detail(detail, detail_size,
                           "permission denied: admin elevation required", "");
      return 126;
    }
    return capyai_native_power_dispatch(tool_call, detail, detail_size);
  }
  if (!capyai_system_app_id_valid(tool_call->path)) {
    capyai_system_detail(detail, detail_size,
                         "application is not registered: ", tool_call->path);
    return 127;
  }
  if (!desktop_is_active()) {
    capyai_system_detail(detail, detail_size, "desktop is not active", "");
    return 127;
  }
  if (capyai_install_desktop_app_bindings() != 0) {
    capyai_system_detail(detail, detail_size,
                         "application registry is unavailable", "");
    return 127;
  }
  rc = capyai_system_app_request(action, tool_call->path, 300u);
  if (rc != CAPYAI_SYSTEM_ACTION_OK) {
    capyai_system_detail(
        detail, detail_size,
        rc == CAPYAI_SYSTEM_ACTION_ERR_TIMEOUT ? "application action timed out: " :
        rc == CAPYAI_SYSTEM_ACTION_ERR_BUSY ? "application queue is busy: " :
        "application action failed: ", tool_call->path);
    return 127;
  }
  capyai_system_detail(detail, detail_size,
                       action == CAPYAI_SYSTEM_APP_OPEN ? "opened app="
                                                        : "closed app=",
                       tool_call->path);
  return 0;
}
#endif
#else /* CAPYOS_PROFILE_CORE_ONLY */
/* core-only profile: desktop/apps symbols are not linked. Provide a
 * single explanatory stub for the shell so registry references stay
 * valid; the desktop/apps commands themselves are excluded from the
 * registry below. */
static int cmd_desktop_unavailable(struct shell_context *ctx, int argc, char **argv) {
  (void)ctx; (void)argc; (void)argv;
  fbcon_print(
      "core-only build: desktop and GUI apps are not part of this kernel ELF.\n"
      "Rebuild with PROFILE=full or use a profile=full installer.\n");
  return -1;
}
#ifdef CAPYOS_HAVE_CAPYAI
static void capyai_core_only_detail(char *detail, size_t detail_size,
                                    const char *text) {
  size_t i = 0u;
  if (!detail || detail_size == 0u) return;
  while (text && text[i] && i + 1u < detail_size) {
    detail[i] = text[i];
    ++i;
  }
  detail[i] = '\0';
}

int capyai_native_system_dispatch(void *ctx,
                                  const struct capy_ai_output *tool_call,
                                  char *detail, size_t detail_size) {
  struct shell_context *shell_ctx = (struct shell_context *)ctx;
  const struct session_context *session =
      shell_ctx ? shell_context_session(shell_ctx) : NULL;
  if (!tool_call || !detail || detail_size == 0u) return 127;
  if (shell_string_equal(tool_call->action, "app_open") ||
      shell_string_equal(tool_call->action, "app_close")) {
    capyai_core_only_detail(detail, detail_size,
                            "desktop is unavailable in core-only profile");
    return 127;
  }
  if (!shell_string_equal(tool_call->action, "power_schedule_status") &&
      !privilege_session_is_admin(session)) {
    privilege_log_denied("capyai-power", session_user(session));
    capyai_core_only_detail(detail, detail_size,
                            "permission denied: admin elevation required");
    return 126;
  }
  return capyai_native_power_dispatch(tool_call, detail, detail_size);
}
#endif
#endif /* CAPYOS_PROFILE_CORE_ONLY */

/* Etapa 7 / Slice 7.5: hook estavel do launcher (CapyUI "Navegador").
 * Sempre definido, em qualquer perfil/flag, para o desktop linkar contra um
 * simbolo unico; fora do full profile com blob embutido devolve -1 e o
 * chamador mostra o erro ao usuario. */
int kernel_desktop_open_browser_graphical(void) {
#if !defined(CAPYOS_PROFILE_CORE_ONLY) && defined(CAPYOS_DESKTOP_GRAPHICAL_BROWSER)
  return kernel_spawn_capygfx_desktop() == KERNEL_SPAWN_OK ? 0 : -1;
#else
  return -1;
#endif
}

/* `cmd_open_browser` erradicado na sessao 6 (2026-05-05). O
 * browser legado foi removido; o sucessor deve voltar como adaptador
 * versionado na etapa correta. */

#define EXT_CMD_COUNT 28
#define EXT_EARLY_COUNT 6

static struct shell_command g_extended_commands[EXT_CMD_COUNT];
static struct shell_command g_extended_early_commands[EXT_EARLY_COUNT];
static int g_extended_initialized = 0;

static void set_cmd(struct shell_command *c, const char *n, shell_command_handler h) {
  c->name = n;
  c->handler = h;
}

static void extended_init(void) {
  if (g_extended_initialized) return;
  int i = 0;
#ifndef CAPYOS_PROFILE_CORE_ONLY
  set_cmd(&g_extended_commands[i++], "desktop",          cmd_desktop_start);
  set_cmd(&g_extended_commands[i++], "desktopstart",     cmd_desktop_start);
  set_cmd(&g_extended_commands[i++], "desktop-start",    cmd_desktop_start);
#else
  set_cmd(&g_extended_commands[i++], "desktop",          cmd_desktop_unavailable);
  set_cmd(&g_extended_commands[i++], "desktopstart",     cmd_desktop_unavailable);
  set_cmd(&g_extended_commands[i++], "desktop-start",    cmd_desktop_unavailable);
#endif
  set_cmd(&g_extended_commands[i++], "clock",            cmd_print_clock);
  set_cmd(&g_extended_commands[i++], "printclock",       cmd_print_clock);
#ifndef CAPYOS_PROFILE_CORE_ONLY
  set_cmd(&g_extended_commands[i++], "open-calculator",  cmd_open_calc);
  set_cmd(&g_extended_commands[i++], "open-files",       cmd_open_files);
  set_cmd(&g_extended_commands[i++], "open-editor",      cmd_open_editor);
  set_cmd(&g_extended_commands[i++], "open-tasks",       cmd_open_tasks);
  set_cmd(&g_extended_commands[i++], "open-capyai",      cmd_open_capyai);
  set_cmd(&g_extended_commands[i++], "open-settings",    cmd_open_settings);
  set_cmd(&g_extended_commands[i++], "open-browser-graphical", cmd_open_browser_graphical);
  set_cmd(&g_extended_commands[i++], "app-open",         cmd_capyai_app_open);
  set_cmd(&g_extended_commands[i++], "app-close",        cmd_capyai_app_close);
#else
  set_cmd(&g_extended_commands[i++], "open-calculator",  cmd_desktop_unavailable);
  set_cmd(&g_extended_commands[i++], "open-files",       cmd_desktop_unavailable);
  set_cmd(&g_extended_commands[i++], "open-editor",      cmd_desktop_unavailable);
  set_cmd(&g_extended_commands[i++], "open-tasks",       cmd_desktop_unavailable);
  set_cmd(&g_extended_commands[i++], "open-capyai",      cmd_desktop_unavailable);
  set_cmd(&g_extended_commands[i++], "open-settings",    cmd_desktop_unavailable);
  set_cmd(&g_extended_commands[i++], "open-browser-graphical", cmd_desktop_unavailable);
  set_cmd(&g_extended_commands[i++], "app-open",         cmd_desktop_unavailable);
  set_cmd(&g_extended_commands[i++], "app-close",        cmd_desktop_unavailable);
#endif
  set_cmd(&g_extended_commands[i++], "print-tasks",      cmd_print_tasks);
  set_cmd(&g_extended_commands[i++], "print-mem",        cmd_print_mem);
  set_cmd(&g_extended_commands[i++], "print-cpus",       cmd_print_cpus);
  set_cmd(&g_extended_commands[i++], "print-gpu",        cmd_print_gpu);
  set_cmd(&g_extended_commands[i++], "print-clock",      cmd_print_clock);
  set_cmd(&g_extended_commands[i++], "print-boot-times", cmd_print_boot_times);
  set_cmd(&g_extended_commands[i++], "print-sockets",    cmd_print_sockets);
  set_cmd(&g_extended_commands[i++], "print-dns-cache",  cmd_print_dns_cache);
  set_cmd(&g_extended_commands[i++], "print-last-tls",   cmd_print_last_tls);
  set_cmd(&g_extended_commands[i++], "print-boot-slot",  cmd_print_boot_slot);
  set_cmd(&g_extended_commands[i++], "auth-status",      cmd_auth_status);
  set_cmd(&g_extended_commands[i++], "scheduler-stats",  cmd_scheduler_stats);
  set_cmd(&g_extended_commands[i++], "print-pci",        cmd_print_pci);
  capyai_command_register(&g_extended_commands[i++]);

  i = 0;
  set_cmd(&g_extended_early_commands[i++], "clock",          cmd_print_clock);
  set_cmd(&g_extended_early_commands[i++], "printclock",     cmd_print_clock);
  set_cmd(&g_extended_early_commands[i++], "print-clock",    cmd_print_clock);
#ifndef CAPYOS_PROFILE_CORE_ONLY
  set_cmd(&g_extended_early_commands[i++], "desktop",        cmd_desktop_start);
  set_cmd(&g_extended_early_commands[i++], "desktopstart",   cmd_desktop_start);
  set_cmd(&g_extended_early_commands[i++], "desktop-start",  cmd_desktop_start);
#else
  set_cmd(&g_extended_early_commands[i++], "desktop",        cmd_desktop_unavailable);
  set_cmd(&g_extended_early_commands[i++], "desktopstart",   cmd_desktop_unavailable);
  set_cmd(&g_extended_early_commands[i++], "desktop-start",  cmd_desktop_unavailable);
#endif

  g_extended_initialized = 1;
}

const struct shell_command *shell_commands_extended(size_t *count) {
  extended_init();
  if (count) *count = EXT_CMD_COUNT;
  return g_extended_commands;
}

const struct shell_command *shell_commands_extended_early(size_t *count) {
  extended_init();
  if (count) *count = EXT_EARLY_COUNT;
  return g_extended_early_commands;
}
