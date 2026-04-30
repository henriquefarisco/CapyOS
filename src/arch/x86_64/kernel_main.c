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
 */
#pragma GCC optimize("O0")
#include <stddef.h>
#include <stdint.h>

#include "arch/x86_64/cpu_local.h"
#include "arch/x86_64/kernel_main_internal.h"
#include "arch/x86_64/hyperv_runtime_coordinator.h"
#include "arch/x86_64/input_runtime.h"
#include "arch/x86_64/interrupts.h"
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
#include "kernel/process.h"
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

static void dbgcon_write(const char *s) {
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

  /* Never leave early exception handling on firmware descriptors.
     Hybrid boots need the bridge path; normal boots after ExitBootServices
     can install native tables immediately because PIC IRQs remain masked. */
  if (handoff_boot_services_active()) {
    (void)x64_platform_tables_prepare_bridge();
  } else {
    x64_platform_tables_init(1);
  }
  dbgcon_putc('B');

  /* Register framebuffer with panic handler once the bridge descriptors are
     active, so an early fault while touching high .bss lands in the kernel
     handler instead of firmware fallback paths. */
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
  dbgcon_putc('y');
  dbgcon_putc(kmem_debug_header_ok() ? 'b' : 'B');
  cpu_local_init((uint64_t)(uintptr_t)
                     (g_syscall_kernel_stack +
                      sizeof(g_syscall_kernel_stack)));
  dbgcon_putc('z');
  dbgcon_putc(kmem_debug_header_ok() ? 'c' : 'C');
  syscall_init();
  dbgcon_putc('q');
  dbgcon_putc(kmem_debug_header_ok() ? 'd' : 'D');
  dbgcon_putc('w');
  {
    int hello_rc = kernel_boot_run_embedded_hello();
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
    syscall_init();
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
      int hello_rc = kernel_boot_run_embedded_hello();
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
  if (apic_available() && !handoff_boot_services_active()) {
    apic_timer_set_callback(scheduler_tick);
    apic_timer_start(100);
    klog(KLOG_INFO, "[scheduler] Preemptive tick armed at 100Hz.");
  }
  dbgcon_putc('3');

  /* Stage 4/8: Keyboard */
  boot_ui_splash_set_status("Configuring keyboard...");
  boot_ui_splash_advance(4, 8);
  boot_metrics_stage_begin("boot-config");
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
  {
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
      input_config.efi_system_table = input_probe.efi_system_table;
      x64_input_runtime_init(&g_input_runtime, &input_config);
    }
    update_system_runtime_platform_status();

    if (!x64_input_has_any(&g_input_runtime)) {
      boot_warnings_add(&g_boot_warnings,
                        "No input device detected (keyboard/serial)");
    }
  }
  boot_metrics_stage_end();
  dbgcon_putc('7');

  /* Stage 8/8: Network */
  boot_ui_splash_set_status("Configuring network...");
  boot_ui_splash_advance(8, 8);
  boot_metrics_stage_begin("runtime-network");
  {
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
      boot_warnings_add(&g_boot_warnings,
                        "Storage runtime unavailable; persistence may fail");
    } else if (!x64_storage_runtime_has_device()) {
      /* RAM-based runtime is active; storage is not persistent but the
       * shell runtime is fully functional.  Mark storage as ready so the
       * boot policy does not force maintenance on every ISO / QEMU boot. */
      validated_storage_ready = 1;
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
    update_agent_status_get(&update_status);

    {
      struct net_stack_status net_status = {0};
      kernel_update_network_service_status();
      if (net_stack_status(&net_status) != 0) {
        boot_warnings_add(&g_boot_warnings,
                          "Network status unavailable; continuing offline");
        network_status_available = 0;
      } else if (!net_status.nic.found) {
        network_status_available = 1;
        boot_warnings_add(&g_boot_warnings,
                          "No network adapter detected");
      } else if (!net_status.runtime_supported) {
        network_status_available = 1;
        if (net_status.nic.kind == NET_NIC_KIND_VMXNET3) {
          boot_warnings_add(&g_boot_warnings,
                            "VMXNET3 detected; use VMware E1000 for now");
        } else {
          boot_warnings_add(&g_boot_warnings,
                            "Detected network adapter has no validated driver");
        }
      } else if (!net_status.ready) {
        network_status_available = 1;
        validated_network_supported = 1;
        boot_warnings_add(&g_boot_warnings,
                          "Validated network driver detected but not ready");
      } else {
        network_status_available = 1;
        validated_network_supported = 1;
      }
    }
    if (update_status.last_result < 0) {
      boot_warnings_add(&g_boot_warnings,
                        "Local update state is inconsistent; review staging");
    } else if (update_status.pending_activation) {
      boot_warnings_add(
          &g_boot_warnings,
          "Staged update activation is armed; payload activation is still manual");
    } else if (update_status.stage_ready) {
      boot_warnings_add(&g_boot_warnings,
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
      boot_warnings_add(&g_boot_warnings,
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

  {
    struct login_runtime_ops login_ops;
    login_ops.has_any_input = x64_input_has_any(&g_input_runtime);
    login_ops.maintenance_mode = kernel_boots_in_maintenance_mode();
    login_ops.shell_ctx = &g_shell_ctx;
    login_ops.session_ctx = &g_session_ctx;
    login_ops.settings = &g_shell_settings;
    login_ops.maintenance_reason = kernel_boot_maintenance_reason();
    login_ops.prepare_shell_runtime = prepare_shell_runtime;
    login_ops.maintenance_session_start = kernel_start_maintenance_session;
    login_ops.init_shell_context_user = init_shell_context;
    login_ops.dispatch_shell_command = try_shell_command;
    login_ops.run_shell_alias = run_shell_alias;
    login_ops.is_equal = streq;
    login_ops.readline = kernel_readline;
    login_ops.session_reset = login_session_reset;
    login_ops.session_set_active = login_session_set_active;
    login_ops.shell_context_init = login_shell_context_init;
    login_ops.system_login = login_system_login;
    login_ops.session_user = login_session_user;
    login_ops.session_cwd = login_session_cwd;
    login_ops.shell_context_should_logout = login_shell_context_should_logout;
    login_ops.print = fbcon_print;
    login_ops.putc = fbcon_putc;
    login_ops.clear_view = fbcon_clear_view;
    login_ops.show_splash = login_show_splash;
    login_ops.ui_banner = ui_banner;
    login_ops.cmd_info = cmd_info;
    login_ops.service_poll = kernel_service_poll;
    login_ops.maintenance_mode_active = login_maintenance_mode_active;
    login_ops.consume_recovery_login_request =
        login_consume_recovery_login_request;
    dbgcon_putc('L');

    if (login_runtime_run(&login_ops) != 0) {
      kernel_halt_forever();
    }
  }
  kernel_halt_forever();
}
__asm__(".section .note.GNU-stack,\"\",@progbits");
