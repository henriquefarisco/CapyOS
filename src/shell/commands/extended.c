#include "shell/commands_extended.h"
#include "shell/core.h"
#include "kernel/task.h"
#include "kernel/scheduler.h"
#include "memory/pmm.h"
#include "memory/vmm.h"
#include "core/boot_metrics.h"
#include "core/boot_slot.h"
#include "core/auth_policy.h"
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
#include "drivers/input/mouse.h"
#include "drivers/input/keyboard_layout.h"
#include "apps/calculator.h"
#include "apps/file_manager.h"
#include "apps/text_editor.h"
#include "apps/task_manager.h"
#include "apps/settings.h"
#include "apps/html_viewer.h"
#include "drivers/pcie.h"
#include <stddef.h>

extern void fbcon_print(const char *s);
extern void fbcon_putc(char c);

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

extern uint32_t *kernel_desktop_get_fb(void);
extern uint32_t kernel_desktop_get_width(void);
extern uint32_t kernel_desktop_get_height(void);
extern uint32_t kernel_desktop_get_pitch(void);
extern int kernel_input_trygetc(char *out_char);

static inline void desktop_frame_delay(void) {
  uint64_t start = pit_ticks();
  uint32_t spins = 0;
  while (pit_ticks() == start && spins++ < 200000u) {
    if (mouse_pending()) break;
    __asm__ volatile("pause");
  }
}

static struct desktop_session g_desktop;
static int g_desktop_active = 0;
static struct shell_context *g_desktop_shell_ctx = NULL;

int desktop_is_active(void) { return g_desktop_active; }

int kernel_desktop_dispatch_shell_command(char *line) {
  if (!g_desktop_shell_ctx || !line) return 0;
  return x64_kernel_try_shell_command(g_desktop_shell_ctx, 1, line);
}

void desktop_stop(void) {
  if (g_desktop_active) {
    g_desktop_active = 0;
  }
}

static int cmd_desktop_start(struct shell_context *ctx, int argc, char **argv) {
  (void)ctx; (void)argc; (void)argv;
  if (g_desktop_active) { fbcon_print("Desktop already running.\n"); return 0; }
  uint32_t *fb = kernel_desktop_get_fb();
  uint32_t w = kernel_desktop_get_width();
  uint32_t h = kernel_desktop_get_height();
  uint32_t pitch = kernel_desktop_get_pitch();
  if (!fb || w == 0 || h == 0) { fbcon_print("Error: no framebuffer.\n"); return -1; }

  mouse_ps2_init();
  g_desktop_shell_ctx = ctx;
  desktop_init(&g_desktop, fb, w, h, pitch, ctx ? ctx->settings : NULL);
  desktop_open_terminal(&g_desktop);
  g_desktop_active = 1;

  /* Small state machine to distinguish a bare ESC press (exit desktop)
   * from a VT100 arrow-key escape sequence (ESC [ A/B/C/D).
   * escape_state: 0 = idle, 1 = saw ESC, 2 = saw ESC+[ */
  int escape_state = 0;

  while (g_desktop_active) {
    char ch = 0;
    int had_activity = 0;
    while (kernel_input_trygetc(&ch)) {
      had_activity = 1;
      if (escape_state == 2) {
        /* We already consumed ESC + '['.  The next char is the direction. */
        escape_state = 0;
        switch (ch) {
          case 'A': desktop_handle_input(&g_desktop, KEY_UP, 0); break;
          case 'B': desktop_handle_input(&g_desktop, KEY_DOWN, 0); break;
          case 'C': desktop_handle_input(&g_desktop, KEY_RIGHT, 0); break;
          case 'D': desktop_handle_input(&g_desktop, KEY_LEFT, 0); break;
          default:  /* Unknown sequence — drop it */                break;
        }
        continue;
      }
      if (escape_state == 1) {
        escape_state = 0;
        if (ch == '[') { escape_state = 2; continue; }
        /* Bare ESC followed by something other than '[' → exit */
        g_desktop_active = 0;
        break;
      }
      if (ch == 0x1B) { escape_state = 1; continue; }
      desktop_handle_input(&g_desktop, (uint32_t)(uint8_t)ch, ch);
    }
    /* If we ended the poll loop while waiting for the second byte of an
     * escape sequence, give it one more frame to arrive before treating
     * it as a bare ESC press.  Only a truly bare ESC (no follow-up byte
     * after a full frame delay) should exit the desktop. */
    if (escape_state == 1 && !kernel_input_trygetc(&ch)) {
      /* No follow-up → bare ESC → exit */
      g_desktop_active = 0;
    } else if (escape_state == 1) {
      /* Follow-up arrived during the extra poll */
      if (ch == '[') { escape_state = 2; }
      else { g_desktop_active = 0; }
    }
    if (!g_desktop_active) break;
    if (desktop_run_frame(&g_desktop)) had_activity = 1;
    if (!had_activity && !mouse_pending()) desktop_frame_delay();
  }

  desktop_shutdown(&g_desktop);
  g_desktop_active = 0;
  g_desktop_shell_ctx = NULL;

  extern void fbcon_clear_view(void);
  fbcon_clear_view();
  return 0;
}

static int cmd_open_calc(struct shell_context *c, int a, char **v) {
  (void)c;(void)a;(void)v;
  if (!g_desktop_active) { fbcon_print("Run desktop-start first\n"); return -1; }
  calculator_open(); return 0;
}
static int cmd_open_files(struct shell_context *c, int a, char **v) {
  (void)c;(void)a;(void)v;
  if (!g_desktop_active) { fbcon_print("Run desktop-start first\n"); return -1; }
  file_manager_open(); return 0;
}
static int cmd_open_editor(struct shell_context *c, int a, char **v) {
  (void)c;(void)v;
  if (!g_desktop_active) { fbcon_print("Run desktop-start first\n"); return -1; }
  text_editor_open(a > 1 ? v[1] : NULL); return 0;
}
static int cmd_open_tasks(struct shell_context *c, int a, char **v) {
  (void)c;(void)a;(void)v;
  if (!g_desktop_active) { fbcon_print("Run desktop-start first\n"); return -1; }
  task_manager_open(); return 0;
}
static int cmd_open_settings(struct shell_context *c, int a, char **v) {
  (void)c;(void)a;(void)v;
  if (!g_desktop_active) { fbcon_print("Run desktop-start first\n"); return -1; }
  settings_open(); return 0;
}
static int cmd_open_browser(struct shell_context *c, int a, char **v) {
  (void)c;(void)a;(void)v;
  if (!g_desktop_active) { fbcon_print("Run desktop-start first\n"); return -1; }
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
