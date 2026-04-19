#include "shell/commands_extended.h"
#include "shell/core.h"
#include "kernel/task.h"
#include "kernel/scheduler.h"
#include "memory/pmm.h"
#include "memory/vmm.h"
#include "boot/boot_metrics.h"
#include "boot/boot_slot.h"
#include "auth/auth_policy.h"
#include "arch/x86_64/smp.h"
#include "arch/x86_64/apic.h"
#include "arch/x86_64/kernel_shell_dispatch.h"
#include "drivers/gpu/gpu_core.h"
#include "drivers/rtc/rtc.h"
#include "drivers/timer/pit.h"
#include "net/socket.h"
#include "net/dns_cache.h"
#include "fs/fsck.h"
#include "gui/desktop.h"
#include "gui/desktop_runtime.h"
#include "drivers/input/mouse.h"
#include "drivers/input/keyboard_layout.h"
#include "apps/calculator.h"
#include "apps/file_manager.h"
#include "apps/text_editor.h"
#include "apps/task_manager.h"
#include "apps/settings.h"
#include "apps/html_viewer.h"
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

/* --- print-boot-slot: show A/B slot status --- */
static int cmd_print_boot_slot(struct shell_context *ctx, int argc, char **argv) {
  (void)ctx; (void)argc; (void)argv;
  boot_slot_status(print_adapter);
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

static int ensure_desktop(struct shell_context *ctx) {
  if (desktop_is_active()) return 0;
  if (!ctx) { fbcon_print("No shell context for desktop.\n"); return -1; }
  return desktop_runtime_start(ctx);
}

static int cmd_desktop_start(struct shell_context *ctx, int argc, char **argv) {
  (void)argc; (void)argv;
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
static int cmd_open_settings(struct shell_context *c, int a, char **v) {
  (void)a;(void)v;
  if (!desktop_is_active() && ensure_desktop(c) != 0) { return -1; }
  settings_open(); return 0;
}
static int cmd_open_browser(struct shell_context *c, int a, char **v) {
  (void)a;(void)v;
  if (!desktop_is_active() && ensure_desktop(c) != 0) { return -1; }
  html_viewer_open(); return 0;
}

#define EXT_CMD_COUNT 23
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
  set_cmd(&g_extended_commands[i++], "desktop",          cmd_desktop_start);
  set_cmd(&g_extended_commands[i++], "desktopstart",     cmd_desktop_start);
  set_cmd(&g_extended_commands[i++], "desktop-start",    cmd_desktop_start);
  set_cmd(&g_extended_commands[i++], "clock",            cmd_print_clock);
  set_cmd(&g_extended_commands[i++], "printclock",       cmd_print_clock);
  set_cmd(&g_extended_commands[i++], "open-calculator",  cmd_open_calc);
  set_cmd(&g_extended_commands[i++], "open-files",       cmd_open_files);
  set_cmd(&g_extended_commands[i++], "open-editor",      cmd_open_editor);
  set_cmd(&g_extended_commands[i++], "open-tasks",       cmd_open_tasks);
  set_cmd(&g_extended_commands[i++], "open-settings",    cmd_open_settings);
  set_cmd(&g_extended_commands[i++], "open-browser",     cmd_open_browser);
  set_cmd(&g_extended_commands[i++], "print-tasks",      cmd_print_tasks);
  set_cmd(&g_extended_commands[i++], "print-mem",        cmd_print_mem);
  set_cmd(&g_extended_commands[i++], "print-cpus",       cmd_print_cpus);
  set_cmd(&g_extended_commands[i++], "print-gpu",        cmd_print_gpu);
  set_cmd(&g_extended_commands[i++], "print-clock",      cmd_print_clock);
  set_cmd(&g_extended_commands[i++], "print-boot-times", cmd_print_boot_times);
  set_cmd(&g_extended_commands[i++], "print-sockets",    cmd_print_sockets);
  set_cmd(&g_extended_commands[i++], "print-dns-cache",  cmd_print_dns_cache);
  set_cmd(&g_extended_commands[i++], "print-boot-slot",  cmd_print_boot_slot);
  set_cmd(&g_extended_commands[i++], "auth-status",      cmd_auth_status);
  set_cmd(&g_extended_commands[i++], "scheduler-stats",  cmd_scheduler_stats);
  set_cmd(&g_extended_commands[i++], "print-pci",        cmd_print_pci);

  i = 0;
  set_cmd(&g_extended_early_commands[i++], "clock",          cmd_print_clock);
  set_cmd(&g_extended_early_commands[i++], "printclock",     cmd_print_clock);
  set_cmd(&g_extended_early_commands[i++], "print-clock",    cmd_print_clock);
  set_cmd(&g_extended_early_commands[i++], "desktop",        cmd_desktop_start);
  set_cmd(&g_extended_early_commands[i++], "desktopstart",   cmd_desktop_start);
  set_cmd(&g_extended_early_commands[i++], "desktop-start",  cmd_desktop_start);

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
