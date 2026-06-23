/* kernel_main.c — x86_64 kernel entry point.
 *
 * After the split this file contains ONLY:
 *   - kernel_main64()  — the entry point called by the UEFI loader
 *   - tiny inline helpers used exclusively by the entry point
 *   - globals that must live in this TU (g_h, g_input_runtime, EBS state)
 *
 * Everything else is in:
 *   framebuffer_console.c  — fbcon_* rendering + desktop accessors
 *   boot_splash.c          — splash screen, ASCII banner, ACPI RSDP
 *   kernel_io_helpers.c    — filesystem I/O, handoff queries, recovery reports
 *   kernel_services.c      — service poll/start/stop, boot policy helpers
 *   kernel_runtime_ops.c   — login wrappers, volume/shell runtime, EBS logic
 *   kernel_boot_stages.c   — late boot-stage bodies (Linux-ABI shims, Stage 4
 *                            keyboard/setup, Stage 7 input, Stage 8 network/
 *                            policy, login_runtime_ops builder)
 */
#pragma GCC optimize("O0")
#include <stddef.h>
#include <stdint.h>

#include "arch/x86_64/cpu_local.h"
#include "arch/x86_64/kernel_main_internal.h"
#include "arch/x86_64/hyperv_runtime_coordinator.h"
#include "arch/x86_64/input_runtime.h"
#include "arch/x86_64/interrupts.h"
#include "arch/x86_64/preemptive_boot.h"
#include "arch/x86_64/preemptive_demo.h"
#include "arch/x86_64/tss.h"
#include "arch/x86_64/kernel_platform_runtime.h"
#include "arch/x86_64/kernel_runtime_control.h"
#include "arch/x86_64/native_runtime_gate.h"
#include "arch/x86_64/platform_timer.h"
#include "arch/x86_64/storage_runtime.h"
#include "arch/x86_64/timebase.h"
#include "boot/boot_config.h"
#include "boot/boot_menu.h"
#include "boot/boot_ui.h"
#include "boot/handoff.h"
#include "boot/boot_metrics.h"
#include "core/kcon.h"
#include "core/system_init.h"
#include "core/version.h"
#include "core/work_queue.h"
#include "drivers/hyperv/hyperv.h"
#include "drivers/input/keyboard.h"
#include "drivers/input/keyboard_layout.h"
#include "drivers/nvme.h"
#include "drivers/acpi/acpi.h"
#include "drivers/serial/serial_com1.h"
#include "drivers/timer/pit.h"
#include "kernel/log/klog.h"
#include "kernel/log/klog_persist.h"
#include "auth/login_runtime.h"
#include "net/network_bootstrap.h"
#include "net/stack.h"
#include "services/service_boot_policy.h"
#include "services/service_manager.h"
#include "services/update_agent.h"
#include "arch/x86_64/panic.h"
#include "arch/x86_64/apic.h"
#include "arch/x86_64/smp.h"
#include "kernel/task.h"
#include "kernel/scheduler.h"
#include "kernel/syscall.h"
#include "kernel/syscall_net.h"
#include "kernel/process.h"
#include "kernel/pipe.h"
#include "kernel/stdin_buf.h"
#include "kernel/user_init.h"
#include "memory/pmm.h"
#include "memory/vmm.h"
#include "memory/kmem.h"
#include "net/dns_cache.h"
#include "net/socket.h"
#include "auth/auth_policy.h"
#include "auth/user.h"
#include "drivers/rtc/rtc.h"
#include "drivers/gpu/gpu_core.h"
#include "drivers/usb/usb_core.h"

extern int kmem_debug_header_ok(void);
extern uintptr_t kmem_debug_free_list_addr(void);
extern uintptr_t kmem_debug_kheap_addr(void);
extern uint8_t kmem_debug_header_magic(void);
extern uint8_t kmem_debug_header_is_free(void);
extern uint8_t __kernel_image_start[];
extern uint8_t __kernel_image_end[];
void login_render_window_layout(void);

/* Per-CPU syscall kernel stack (M4 phase 3.5). Used as the kernel
 * stack pointer that the syscall path loads from
 * `%gs:CPU_LOCAL_KERNEL_RSP_OFFSET`. Sized at 16 KiB which matches
 * the per-task kernel_stack_size; phase 8 will replace this static
 * with a per-task stack swapped in by the scheduler. */
static uint8_t g_syscall_kernel_stack[16 * 1024]
    __attribute__((aligned(16)));

/* ── globals owned by this TU ────────────────────────────────────────── */

const struct boot_handoff *g_h = NULL;
struct x64_input_runtime g_input_runtime;
int g_exit_boot_services_attempted = 0;
int g_exit_boot_services_done = 0;
EFI_STATUS_K g_exit_boot_services_status = EFI_SUCCESS_K;
int g_network_runtime_refresh_enabled = 0;

/* ── tiny helpers used only in the entry point ───────────────────────── */

static int range_ok(uint64_t addr, uint64_t size) {
  if (addr == 0 || size == 0)
    return 0;
  if (addr + size < addr)
    return 0;
  return 1;
}

static void dbg_hex64(uint64_t v) {
  for (int i = 60; i >= 0; i -= 4) {
    uint8_t nib = (v >> i) & 0xF;
    dbgcon_putc((uint8_t)(nib < 10 ? ('0' + nib) : ('A' + (nib - 10))));
  }
}

static void dbg_hex8(uint8_t v) {
  uint8_t hi = (uint8_t)((v >> 4) & 0xFu);
  uint8_t lo = (uint8_t)(v & 0xFu);
  dbgcon_putc((uint8_t)(hi < 10 ? ('0' + hi) : ('A' + (hi - 10))));
  dbgcon_putc((uint8_t)(lo < 10 ? ('0' + lo) : ('A' + (lo - 10))));
}

static uint64_t read_rsp_local(void) {
  uint64_t value;
  __asm__ volatile("mov %%rsp, %0" : "=r"(value));
  return value;
}

static void kmem_boot_debug_dump(uint8_t tag) {
  dbgcon_putc(tag);
  dbgcon_putc('s');
  dbg_hex64(read_rsp_local());
  dbgcon_putc('f');
  dbg_hex64(kmem_debug_free_list_addr());
  dbgcon_putc('h');
  dbg_hex64(kmem_debug_kheap_addr());
  dbgcon_putc('i');
  dbg_hex8(kmem_debug_header_is_free());
  dbgcon_putc('m');
  dbg_hex8(kmem_debug_header_magic());
}

static void __attribute__((unused)) dbgcon_write(const char *s) {
  if (!s) {
    return;
  }
  while (*s) {
    dbgcon_putc((uint8_t)*s++);
  }
}

struct efi_memory_descriptor64 {
  uint32_t type;
  uint32_t pad;
  uint64_t physical_start;
  uint64_t virtual_start;
  uint64_t number_of_pages;
  uint64_t attribute;
};

static void pmm_init_from_handoff(const struct boot_handoff *h) {
  enum { EFI_CONVENTIONAL_MEMORY = 7 };
  struct pmm_region regions[PMM_MAX_REGIONS];
  size_t count = 0;

  if (!h || !h->memmap || h->memmap_desc_size < sizeof(struct efi_memory_descriptor64) ||
      !h->memmap_entries) {
    return;
  }

  const uint8_t *map = (const uint8_t *)(uintptr_t)h->memmap;
  for (uint32_t i = 0; i < h->memmap_entries && count < PMM_MAX_REGIONS; i++) {
    const struct efi_memory_descriptor64 *desc =
        (const struct efi_memory_descriptor64 *)(const void *)
            (map + (uint64_t)i * h->memmap_desc_size);
    if (desc->type != EFI_CONVENTIONAL_MEMORY || desc->number_of_pages == 0) {
      continue;
    }
    regions[count].base = desc->physical_start;
    regions[count].length = desc->number_of_pages * PMM_PAGE_SIZE;
    regions[count].type = 1;
    count++;
  }

  if (count > 0) {
    pmm_init(regions, count);
    pmm_reserve_range((uint64_t)(uintptr_t)__kernel_image_start,
                      (uint64_t)((uintptr_t)__kernel_image_end -
                                 (uintptr_t)__kernel_image_start));
  }
}

static __attribute__((noreturn)) void kernel_halt_forever(void) {
  __asm__ volatile("cli");
  for (;;) {
    __asm__ volatile("hlt");
  }
}

static void kernel_log_boot_warnings(const struct boot_warnings *warnings) {
  if (!warnings || warnings->count == 0) {
    return;
  }

  fbcon_print("\n[boot] Compatibility warnings:\n");
  for (uint32_t i = 0; i < warnings->count; ++i) {
    fbcon_print("  - ");
    fbcon_print(warnings->messages[i]);
    fbcon_putc('\n');
  }
  fbcon_print("[boot] Continuing startup.\n\n");
}

/* ── kernel entry point ──────────────────────────────────────────────── */

__attribute__((noreturn)) void kernel_main64(const struct boot_handoff *h) {
  dbgcon_putc('H');
  dbgcon_putc('O');
  dbgcon_putc('K');

  g_h = h;
  if (!h || h->magic != BOOT_HANDOFF_MAGIC) {
    dbgcon_putc('!');
    for (;;)
      cpu_relax();
  }

  if (!range_ok(h->fb.base, h->fb.size) || h->fb.bpp != 32 ||
      h->fb.pitch == 0) {
    dbgcon_putc('F');
    dbg_hex64(h->fb.base);
    for (;;)
      cpu_relax();
  }

  /* Initialise the framebuffer console from handoff data.
   * The kernel keeps the firmware's PML4 (vmm_init adopts CR3 verbatim),
   * so any physical address the firmware identity-mapped is reachable
   * here -- including high-memory GOP framebuffers (Hyper-V/VMware
   * place them around 0x80000000-0xFC000000). A simple non-NULL,
   * non-overflow range check is therefore the correct guard. */
  g_con.fb = range_ok(h->fb.base, h->fb.size)
                 ? (uint32_t *)(uintptr_t)h->fb.base
                 : NULL;
  g_con.width = h->fb.width;
  g_con.height = h->fb.height;
  g_con.stride = h->fb.pitch / 4;
  g_con.size_bytes = h->fb.size;
  if (g_con.stride == 0 || g_con.width == 0 || g_con.height == 0) {
    dbgcon_putc('F');
    for (;;)
      cpu_relax();
  }
  if (g_con.width > g_con.stride) {
    g_con.width = g_con.stride;
  }
  if (h->fb.pitch > 0 && h->fb.size > 0) {
    uint32_t max_rows = h->fb.size / h->fb.pitch;
    if (max_rows < g_con.height) {
      g_con.height = max_rows;
    }
  }
  if (g_con.height == 0) {
    dbgcon_putc('F');
    for (;;)
      cpu_relax();
  }
  g_con.bg = 0x00102030;
  g_con.fg = 0x00F0F0F0;
  g_con.origin_y = 0;
  g_con.cols = (g_con.width / CELL_W);
  g_con.rows = (g_con.height > g_con.origin_y)
                   ? ((g_con.height - g_con.origin_y) / CELL_H)
                   : 0;
  g_con.col = 0;
  g_con.row = 0;

  /* Keep firmware descriptors while BootServices remains active. Hyper-V
     firmware BlockIO/SNP can re-enable interrupts during calls; installing
     the kernel IDT first turns those firmware-owned vectors into panics. */
  if (!handoff_boot_services_active()) {
    x64_platform_tables_init(1);
  }
  dbgcon_putc('B');

  /* Register framebuffer for the native panic path when active. Hybrid
     firmware-runtime boots keep firmware descriptors until ExitBootServices. */
  panic_set_framebuffer(g_con.fb, g_con.width, g_con.height,
                        g_con.stride * 4);
  dbgcon_putc('P');

  dbgcon_putc('r');
  g_rsdp_override = h->rsdp;
  g_acpi_initialized = 0;
  dbgcon_putc('R');
  dbgcon_putc('e');
  g_efi_system_table = h->efi_system_table;
  dbgcon_putc('E');
  dbgcon_putc('t');
  g_con.bg = 0x00102030;
  g_con.fg = 0x00F0F0F0;
  g_theme_splash_bg = 0x000A1713;
  g_theme_splash_icon = 0x0000A651;
  g_theme_splash_bar_border = 0x00213A31;
  g_theme_splash_bar_bg = 0x0012221C;
  g_theme_splash_bar_fill = 0x0000C364;
  dbgcon_putc('T');

  /* --- Boot splash with live progress bar -------------------------------- */
  int boot_splash_enabled = handoff_splash_enabled();
#ifdef CAPYOS_BOOT_RUN_HELLO
  boot_splash_enabled = 0;
#endif
  {
    struct boot_ui_io bui;
    bui.screen_w = g_con.width;
    bui.screen_h = g_con.height;
    bui.splash_bg = g_theme_splash_bg;
    bui.splash_icon = g_theme_splash_icon;
    bui.splash_bar_border = g_theme_splash_bar_border;
    bui.splash_bar_bg = g_theme_splash_bar_bg;
    bui.splash_bar_fill = g_theme_splash_bar_fill;
    bui.text_fg = 0x00CCCCCC;
    bui.text_muted_fg = 0x0090A8A0;
    bui.console_bg = g_con.bg;
    bui.fill_rect = fbcon_fill_rect_px;
    bui.putch_at = fbcon_putch_at;
    bui.draw_icon = ui_draw_capyos_icon;
    boot_ui_init(&bui);
  }
  dbgcon_putc('U');
  fbcon_set_visual_muted(boot_splash_enabled);
  if (boot_splash_enabled) {
    boot_ui_splash_begin();
  }
  dbgcon_putc('S');

  struct boot_warnings g_boot_warnings;
  boot_warnings_init(&g_boot_warnings);
  service_boot_policy_evaluate(NULL, &g_boot_policy_decision);

  /* Stage 1/8: Platform tables */
  boot_ui_splash_set_status("Initializing platform...");
  boot_ui_splash_advance(1, 8);
  x64_platform_tables_init(!handoff_boot_services_active());
  dbgcon_putc('1');

  /* Stage 2/8: Core services */
  boot_ui_splash_set_status("Starting core services...");
  boot_ui_splash_advance(2, 8);
  kcon_init();
  dbgcon_putc('A');
  kinit();
  kmem_boot_debug_dump('I');
  dbgcon_putc('B');
#ifdef CAPYOS_BOOT_RUN_HELLO
  task_system_init();
  pmm_init_from_handoff(h);
  vmm_init();
  kinit();
  kmem_boot_debug_dump('X');
  dbgcon_putc(kmem_debug_header_ok() ? 'a' : 'A');
  process_system_init();
  pipe_system_init();
  stdin_buf_init();
  dbgcon_putc('y');
  dbgcon_putc(kmem_debug_header_ok() ? 'b' : 'B');
  cpu_local_init((uint64_t)(uintptr_t)
                     (g_syscall_kernel_stack +
                      sizeof(g_syscall_kernel_stack)));
  /* M4 phase 8f.1: TSS scaffolding for ring-3 IRQ safety. Same
   * call as the non-CAPYOS_BOOT_RUN_HELLO path below; idempotent
   * so issuing it twice is harmless. */
  tss_init((uint64_t)(uintptr_t)
               (g_syscall_kernel_stack +
                sizeof(g_syscall_kernel_stack)));
  dbgcon_putc('z');
  dbgcon_putc(kmem_debug_header_ok() ? 'c' : 'C');
  syscall_init();
  /* F4 seção c (2026-05-08): bridge SYS_SOCKET..SYS_RECV to the
   * real socket_* family in src/net/services/socket.c, and register
   * the socket close hook on the process FD lifecycle. Both calls
   * are idempotent (pointer stores), safe to repeat across the EBS
   * and post-EBS init paths. */
  syscall_net_install_default_ops();
  dbgcon_putc('q');
  dbgcon_putc(kmem_debug_header_ok() ? 'd' : 'D');
  dbgcon_putc('w');
  dbgcon_write("[user_init] CAPYOS_BOOT_RUN_HELLO defined; spawning hello.\n");
  {
#if defined(CAPYOS_BOOT_RUN_CAPYSH)
    int hello_rc = kernel_boot_run_capysh();
#elif defined(CAPYOS_BOOT_RUN_TWO_BUSY)
    int hello_rc = kernel_boot_run_two_busy_users();
#else
    int hello_rc = kernel_boot_run_embedded_hello();
#endif
    dbgcon_putc('E');
    if (hello_rc < 0) {
      dbgcon_putc('-');
      hello_rc = -hello_rc;
    }
    dbgcon_putc((uint8_t)('0' + (hello_rc % 10)));
  }
#endif
  service_manager_init();
  dbgcon_putc('C');
  work_queue_init();
  dbgcon_putc('D');
  /* Cold boot already starts with the task table in BSS-zeroed state and
   * next_pid initialized from .data, so avoid a redundant early reset here.
   * This keeps the boot path off the fragile return that was faulting before
   * COM1 came up. */
  dbgcon_putc('E');
  /* The scheduler runtime also starts in BSS-zeroed state and now defaults to
   * cooperative policy in .data, so the early boot path can skip the explicit
   * reset here and avoid another fragile call/return before COM1 is live. */
  dbgcon_putc('F');
  /* The DNS cache starts zeroed in .bss, so there is no need to walk it here
   * during cold boot before the runtime has finished coming up. */
  dbgcon_putc('G');
  /* Socket tables are BSS-zeroed too; the runtime only needs the "initialized"
   * gate opened, which is now provided by a static default. */
  dbgcon_putc('H');
  /* The update agent already self-seeds on first real use. */
  dbgcon_putc('I');
  (void)work_queue_register(SYSTEM_WORK_RECOVERY_SNAPSHOT,
                            "recovery-snapshot",
                            kernel_work_recovery_snapshot, NULL);
  dbgcon_putc('J');
  (void)work_queue_set_interval(SYSTEM_WORK_RECOVERY_SNAPSHOT, 600u);
  dbgcon_putc('K');
  (void)work_queue_disable(SYSTEM_WORK_RECOVERY_SNAPSHOT);
  dbgcon_putc('L');
#ifndef CAPYOS_BOOT_RUN_HELLO
  (void)service_manager_target_apply(g_boot_policy_decision.bootstrap_target);
#endif
  dbgcon_putc('2');

  /* Stage 3/8: Timers */
  boot_ui_splash_set_status("Calibrating timers...");
  boot_ui_splash_advance(3, 8);
  x64_timebase_init();
  dbgcon_putc('g');
  /* Linux-ABI shim registrations extracted to kernel_boot_stages.c
   * (kernel_boot_register_linux_abi_shims). Pure idempotent pointer
   * stores; see that TU for the per-shim ordering notes. */
  kernel_boot_register_linux_abi_shims();
  x64_platform_timer_init(!handoff_boot_services_active());
  dbgcon_putc('h');
  /* The early post-EBS path is still sensitive to return corruption while the
   * platform runtime settles. Keep PIT/timebase active and defer local APIC
   * bring-up until the native runtime is fully stable. */
  dbgcon_putc('i');
  boot_metrics_init();
  dbgcon_putc('j');
  boot_metrics_stage_begin("platform-core");
  if (apic_available()) {
    smp_detect_cpus(h->rsdp);
  }
  dbgcon_putc('k');
  pmm_init_from_handoff(h);
  vmm_init();
  dbgcon_putc('l');
  klog(KLOG_INFO, "[vmm] Virtual memory manager initialized.");
  process_system_init();
  pipe_system_init();
  stdin_buf_init();
  dbgcon_putc('m');
  if (!handoff_boot_services_active()
#ifdef CAPYOS_BOOT_RUN_HELLO
      || 1
#endif
  ) {
    dbgcon_putc('u');
    cpu_local_init((uint64_t)(uintptr_t)
                       (g_syscall_kernel_stack +
                        sizeof(g_syscall_kernel_stack)));
    klog(KLOG_INFO,
         "[cpu_local] Per-CPU area armed; IA32_GS_BASE = &cpu_local.");
    /* M4 phase 8f.1: install the TSS so ring 3 -> ring 0 IRQ
     * transitions have a defined kernel stack (RSP0). Reuses the
     * same shared kernel stack as the syscall path; phase 8f.2 will
     * swap RSP0 to a per-task kernel stack on every context_switch
     * to avoid clobbering when two ring-3 tasks preempt each other.
     * Idempotent: a later tss_init from a different code path
     * refreshes RSP0 without re-issuing LTR. */
    tss_init((uint64_t)(uintptr_t)
                 (g_syscall_kernel_stack +
                  sizeof(g_syscall_kernel_stack)));
    klog(KLOG_INFO, "[tss] TSS loaded; ring-3 -> ring-0 IRQs safe.");
    syscall_init();
    /* F4 seção c: see comment on the early EBS path above. */
    syscall_net_install_default_ops();
    dbgcon_putc('v');
    klog(KLOG_INFO, "[syscall] Syscall ABI registered.");
#ifdef CAPYOS_BOOT_RUN_HELLO
    /* M4 phase 5d: when the build defines CAPYOS_BOOT_RUN_HELLO,
     * spawn the embedded `hello` user binary and drop into Ring 3.
     * `kernel_boot_run_embedded_hello` is __attribute__((noreturn))
     * on the success path (it iretq's into user space); a return
     * means the spawn failed and we fall through to the regular
     * boot. Default builds skip this block so the kernel shell
     * stays reachable. The QEMU smoke for phase 5d defines the
     * macro on the cross-compiler command line. */
    dbgcon_write("[user_init] CAPYOS_BOOT_RUN_HELLO defined; spawning hello.\n");
    dbgcon_putc('w');
    klog(KLOG_INFO,
         "[user_init] CAPYOS_BOOT_RUN_HELLO defined; spawning hello.");
    {
#if defined(CAPYOS_BOOT_RUN_CAPYSH)
      /* M5 phase E.5: boot directly into the embedded interactive
       * shell `/bin/capysh`. Resolves through the embedded_progs
       * registry so any future shell binary picked up by the
       * registry takes over without further kernel changes. */
      dbgcon_write("[user_init] CAPYOS_BOOT_RUN_CAPYSH defined; spawning capysh.\n");
      int hello_rc = kernel_boot_run_capysh();
#elif defined(CAPYOS_BOOT_RUN_TWO_BUSY)
      /* M4 phase 8f.5: spawn TWO user processes that run the BUSY
       * arm of hello with rank=0/1 so each emits a distinct marker
       * ([busyU0] / [busyU1]). The smoke verifies BOTH appear,
       * proving end-to-end ring-3 preemption between two real user
       * tasks. The helper is `noreturn` on success. */
      dbgcon_write("[user_init] CAPYOS_BOOT_RUN_TWO_BUSY defined; spawning two.\n");
      int hello_rc = kernel_boot_run_two_busy_users();
#else
      int hello_rc = kernel_boot_run_embedded_hello();
#endif
      dbgcon_putc('E');
      if (hello_rc < 0) {
        dbgcon_putc('-');
        hello_rc = -hello_rc;
      }
      dbgcon_putc((uint8_t)('0' + (hello_rc % 10)));
      klog(KLOG_WARN,
           "[user_init] hello spawn returned without entering Ring 3.");
      (void)hello_rc;
    }
#endif
  }
  dbgcon_putc('n');
  auth_policy_init();
  dbgcon_putc('o');
  rtc_init();
  dbgcon_putc('Q');
  gpu_init();
  dbgcon_putc('R');
  dbgcon_putc('S');
  dbgcon_putc('p');
  boot_metrics_stage_end();
  /* M4 phase 8b/c/d preemptive boot wiring. Helpers are no-ops when
   * CAPYOS_PREEMPTIVE_SCHEDULER is undefined; see
   * include/arch/x86_64/preemptive_boot.h for the call-order contract. */
  capyos_preemptive_install_policy();
  if (apic_available() && !handoff_boot_services_active()) {
    apic_timer_set_callback(scheduler_tick);
    capyos_preemptive_install_irq0();
    apic_timer_start(100);
    klog(KLOG_INFO, "[scheduler] Preemptive tick armed at 100Hz.");
    capyos_preemptive_mark_running();
    capyos_preemptive_observe_ticks();
    /* M4 phase 8e: two-task kernel-mode preemption demo. No-op
     * unless CAPYOS_PREEMPTIVE_DEMO is defined. When the demo is
     * active this call NEVER RETURNS (it abandons the boot stack
     * via the first-task trampoline and proceeds inside busy_a's
     * body); the rest of kernel_main below is intentionally
     * unreachable for the demo build. */
    capyos_preemptive_demo_run();
  }
  dbgcon_putc('3');

  /* Stage 4/8: Keyboard */
  boot_ui_splash_set_status("Configuring keyboard...");
  boot_ui_splash_advance(4, 8);
  boot_metrics_stage_begin("boot-config");
  kernel_boot_stage_keyboard_setup();
  boot_metrics_stage_end();
  dbgcon_putc('4');

  /* Stage 5/8: Storage */
  boot_ui_splash_set_status("Detecting storage...");
  boot_ui_splash_advance(5, 8);
  boot_metrics_stage_begin("storage-probe");
  if (nvme_init() != 0) {
    klog(KLOG_INFO, "[nvme] No NVMe controller found.");
  }
  boot_metrics_stage_end();
  dbgcon_putc('5');

  /* Stage 6/8: Serial */
  boot_ui_splash_set_status("Initializing serial...");
  boot_ui_splash_advance(6, 8);
  boot_metrics_stage_begin("serial-console");
  com1_init();
  com1_puts("[COM1] CapyOS 64-bit serial console ready\r\n");
  boot_metrics_stage_end();
  dbgcon_putc('6');

  /* Stage 7/8: Input devices */
  boot_ui_splash_set_status("Detecting input devices...");
  boot_ui_splash_advance(7, 8);
  boot_metrics_stage_begin("input-probe");
  kernel_boot_stage_input(&g_boot_warnings);
  boot_metrics_stage_end();
  dbgcon_putc('7');

  /* Stage 8/8: Network */
  boot_ui_splash_set_status("Configuring network...");
  boot_ui_splash_advance(8, 8);
  boot_metrics_stage_begin("runtime-network");
  kernel_boot_stage_network_and_policy(&g_boot_warnings);
  boot_metrics_stage_end();
  dbgcon_putc('8');

  /* --- End splash -------------------------------------------------------- */
  boot_ui_splash_end();
  fbcon_set_visual_muted(0);
  dbgcon_putc('X');

  g_con.col = 0;
  g_con.row = 0;

  /* --- Hardware compatibility warnings ----------------------------------- */
  if (g_boot_warnings.count > 0) {
    kernel_log_boot_warnings(&g_boot_warnings);
    (void)klog_persist_flush_default();
  }

  (void)klog_persist_flush_default();

  /* --- Console ready ----------------------------------------------------- */
  fbcon_fill_rect_px(0, 0, g_con.width, g_con.height, g_con.bg);
  g_con.col = 0;
  g_con.row = 0;

#ifdef CAPYOS_TLS_HANDSHAKE_SMOKE
  /* Etapa 5 / Slice 5.6: the network stage (8/8) above brought up the net
   * stack, so boot directly into the userland TLS handshake smoke instead of
   * the login/desktop flow. `kernel_boot_run_tls_smoke` is noreturn on
   * success (drops to ring 3); the program retries its HTTPS GET until the
   * async DHCP lease lands (it yields/sleeps between attempts so net kernel
   * tasks get CPU), then exits 0 — which process_exit observes to emit
   * `[smoke] tls-handshake ready` on COM1. Gated so production boot is
   * unaffected. A return means the spawn failed; fall through to login. */
  dbgcon_write("[user_init] CAPYOS_TLS_HANDSHAKE_SMOKE; spawning tls_smoke.\n");
  {
    int tls_smoke_rc = kernel_boot_run_tls_smoke();
    (void)tls_smoke_rc;
    klog(KLOG_WARN,
         "[user_init] tls_smoke spawn returned without entering Ring 3.");
  }
#endif
#ifdef CAPYOS_CAPYBROWSE_SMOKE
  /* Etapa 6 / Slice 6.4: boot directly into the CapyBrowse Text smoke instead
   * of the login/desktop flow. `kernel_boot_run_capybrowse` is noreturn on
   * success (drops to ring 3); the program retries its HTTPS GET until the
   * async DHCP lease lands, fetches + renders the controlled page, then exits
   * 0 — which process_exit observes to emit `[smoke] capybrowse-text ready` on
   * COM1. Gated so production boot is unaffected; a return means the spawn
   * failed, so fall through to login. */
  dbgcon_write("[user_init] CAPYOS_CAPYBROWSE_SMOKE; spawning capybrowse.\n");
  {
    int capybrowse_rc = kernel_boot_run_capybrowse();
    (void)capybrowse_rc;
    klog(KLOG_WARN,
         "[user_init] capybrowse spawn returned without entering Ring 3.");
  }
#endif
#ifdef CAPYOS_APPS_ROUNDTRIP_SMOKE
  /* Etapa 6 / Slice 6.6: run the apps-basic-roundtrip orchestrator in-kernel
   * (the basic apps are in-kernel functions, not ring-3 processes). It runs
   * each app's headless primary-function smoke and the latch emits
   * `[smoke] apps-basic-roundtrip ready` on COM1 after the required clean
   * passes. Returns after running; falls through to login. Gated so production
   * boot is unaffected. */
  dbgcon_write("[user_init] CAPYOS_APPS_ROUNDTRIP_SMOKE; running apps roundtrip.\n");
  {
    int apps_roundtrip_rc = kernel_boot_run_apps_roundtrip();
    (void)apps_roundtrip_rc;
  }
#endif
  {
    struct login_runtime_ops login_ops;
    kernel_boot_build_login_ops(&login_ops);
    dbgcon_putc('L');

    if (login_runtime_run(&login_ops) != 0) {
      kernel_halt_forever();
    }
  }
  kernel_halt_forever();
}
__asm__(".section .note.GNU-stack,\"\",@progbits");
