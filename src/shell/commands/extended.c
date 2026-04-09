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
#include "drivers/gpu/gpu_core.h"
#include "drivers/rtc/rtc.h"
#include "net/socket.h"
#include "net/dns_cache.h"
#include "fs/fsck.h"
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

static const struct shell_command extended_commands[] = {
  {"print-tasks",     "Show running tasks",        cmd_print_tasks},
  {"print-mem",       "Show memory statistics",     cmd_print_mem},
  {"print-cpus",      "Show CPU/SMP info",          cmd_print_cpus},
  {"print-gpu",       "Show GPU info",              cmd_print_gpu},
  {"print-clock",     "Show real-time clock",       cmd_print_clock},
  {"print-boot-times","Show boot stage timings",    cmd_print_boot_times},
  {"print-sockets",   "Show socket statistics",     cmd_print_sockets},
  {"print-dns-cache", "Show DNS cache stats",       cmd_print_dns_cache},
  {"print-boot-slot", "Show boot slot A/B status",  cmd_print_boot_slot},
  {"auth-status",     "Show auth lockout policy",   cmd_auth_status},
  {"scheduler-stats", "Show scheduler statistics",  cmd_scheduler_stats},
};

const struct shell_command *shell_commands_extended(size_t *count) {
  if (count) *count = sizeof(extended_commands) / sizeof(extended_commands[0]);
  return extended_commands;
}
