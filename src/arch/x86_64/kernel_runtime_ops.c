/* kernel_runtime_ops.c — Login wrappers, volume/shell runtime builders,
 * ExitBootServices logic, input/getc, klog adapter, x64_kernel_manual_*.
 *
 * Split from kernel_main.c to keep each TU ≤ 500 lines.
 */
#pragma GCC optimize("O0")
#include <stddef.h>
#include <stdint.h>

#include "arch/x86_64/kernel_main_internal.h"
#include "arch/x86_64/hyperv_runtime_coordinator.h"
#include "arch/x86_64/input_runtime.h"
#include "arch/x86_64/interrupts.h"
#include "arch/x86_64/kernel_platform_runtime.h"
#include "arch/x86_64/kernel_runtime_control.h"
#include "arch/x86_64/kernel_shell_dispatch.h"
#include "arch/x86_64/kernel_shell_runtime.h"
#include "arch/x86_64/kernel_volume_runtime.h"
#include "arch/x86_64/native_runtime_gate.h"
#include "arch/x86_64/platform_timer.h"
#include "arch/x86_64/storage_runtime.h"
#include "auth/session.h"
#include "auth/user.h"
#include "boot/handoff.h"
#include "core/system_init.h"
#include "drivers/hyperv/hyperv.h"
#include "fs/block.h"
#include "fs/capyfs.h"
#include "kernel/log/klog.h"
#include "kernel/log/klog_persist.h"
#include "shell/core.h"

/* ── klog print adapter ──────────────────────────────────────────────── */

static char g_klog_adapter_buf[256];
static uint32_t g_klog_adapter_len = 0;

static inline void dbg_runtime_putc(char ch) {
  __asm__ volatile("outb %0, %1" : : "a"((uint8_t)ch), "Nd"((uint16_t)0xE9));
}

static void dbg_runtime_puts(const char *text) {
  while (text && *text) {
    dbg_runtime_putc(*text++);
  }
}

void klog_print_adapter(const char *s) {
  if (!s) {
    return;
  }
  while (*s) {
    char c = *s++;
    if (c == '\n') {
      g_klog_adapter_buf[g_klog_adapter_len] = '\0';
      klog(KLOG_INFO, g_klog_adapter_buf);
      g_klog_adapter_len = 0;
      continue;
    }
    if (g_klog_adapter_len + 1 < sizeof(g_klog_adapter_buf)) {
      g_klog_adapter_buf[g_klog_adapter_len++] = c;
    }
  }
}

void klog_print_adapter_flush(void) {
  if (g_klog_adapter_len > 0) {
    g_klog_adapter_buf[g_klog_adapter_len] = '\0';
    klog(KLOG_INFO, g_klog_adapter_buf);
    g_klog_adapter_len = 0;
  }
}

/* ── input ───────────────────────────────────────────────────────────── */

int kernel_input_trygetc(char *out_char) {
  if (!out_char) return 0;
  return x64_input_poll_char(&g_input_runtime, out_char);
}

int kernel_input_getc(char *out_char) {
  if (!out_char)
    return 0;
  for (;;) {
    struct x64_hyperv_runtime_coordinator_ops ops;
    kernel_hyperv_runtime_coordinator_ops_init(&ops);
    int hyperv_was_ready = g_input_runtime.has_hyperv;
    kernel_service_poll();
    (void)x64_hyperv_runtime_poll_promotions(&g_input_runtime, &ops);
    if (x64_input_poll_char(&g_input_runtime, out_char)) {
      return 1;
    }
    if (hyperv_was_ready && !g_input_runtime.has_hyperv &&
        g_input_runtime.hyperv_deferred) {
      klog(KLOG_WARN,
           "[hyperv] Backend VMBus degradado; mantendo fallback atual e reagendando promocao controlada.");
    }
    cpu_relax();
  }
}

size_t kernel_input_readline(char *buf, size_t maxlen, int mask) {
  if (!buf || maxlen < 2) {
    return 0;
  }

  size_t len = 0;
  buf[0] = 0;

  for (;;) {
    char ch = 0;
    if (!kernel_input_getc(&ch)) {
      continue;
    }

    if (ch == 127 || ch == '\b') {
      if (len > 0) {
        len--;
        buf[len] = 0;
        fbcon_putc('\b');
      }
      continue;
    }

    if (ch == '\r') {
      ch = '\n';
    }
    if (ch == '\n') {
      fbcon_putc('\n');
      buf[len] = 0;
      return len;
    }
    if (len + 1 < maxlen) {
      buf[len++] = ch;
      buf[len] = 0;
      fbcon_putc(mask ? '*' : ch);
    }
  }
}

size_t kernel_readline(char *buf, size_t maxlen, int mask) {
  return kernel_input_readline(buf, maxlen, mask);
}

/* ── EFI ExitBootServices ────────────────────────────────────────────── */

static void print_active_efi_runtime_trace(void) {
  struct x64_platform_diag_io io;
  kernel_platform_diag_io_init(&io);
  x64_kernel_print_active_efi_runtime_trace(&io);
}

static EFI_STATUS_K kernel_exit_boot_services(void) {
  EFI_SYSTEM_TABLE_K *st = NULL;
  EFI_BOOT_SERVICES_K *bs = NULL;
  uint64_t map_key = 0;
  uint64_t desc_size = 0;
  uint32_t desc_ver = 0;

  if (!handoff_has_exit_boot_services_contract()) {
    return EFI_INVALID_PARAMETER_K;
  }

  st = (EFI_SYSTEM_TABLE_K *)(uintptr_t)g_h->efi_system_table;
  bs = st ? st->BootServices : NULL;
  if (!st || !bs || !bs->GetMemoryMap || !bs->ExitBootServices) {
    return EFI_INVALID_PARAMETER_K;
  }

  for (uint32_t attempt = 0; attempt < 4u; ++attempt) {
    uint64_t map_size = g_h->memmap_capacity;
    EFI_STATUS_K st_map =
        bs->GetMemoryMap(&map_size, (void *)(uintptr_t)g_h->memmap, &map_key,
                         &desc_size, &desc_ver);
    if (st_map == EFI_BUFFER_TOO_SMALL_K || map_size > g_h->memmap_capacity) {
      return EFI_BUFFER_TOO_SMALL_K;
    }
    if (st_map != EFI_SUCCESS_K) {
      return st_map;
    }

    {
      EFI_STATUS_K st_exit = bs->ExitBootServices(
          (EFI_HANDLE_K)(uintptr_t)g_h->efi_image_handle, map_key);
      if (st_exit == EFI_SUCCESS_K) {
        ((struct boot_handoff *)g_h)->memmap_size = map_size;
        ((struct boot_handoff *)g_h)->memmap_desc_size = (uint32_t)desc_size;
        ((struct boot_handoff *)g_h)->memmap_entries =
            desc_size ? (uint32_t)(map_size / desc_size) : 0;
        ((struct boot_handoff *)g_h)->efi_map_key = map_key;
        ((struct boot_handoff *)g_h)->runtime_flags &=
            ~(BOOT_HANDOFF_RUNTIME_BOOT_SERVICES_ACTIVE |
              BOOT_HANDOFF_RUNTIME_FIRMWARE_INPUT |
              BOOT_HANDOFF_RUNTIME_FIRMWARE_BLOCK_IO |
              BOOT_HANDOFF_RUNTIME_HYBRID_BOOT);
        return EFI_SUCCESS_K;
      }
      if (st_exit != EFI_INVALID_PARAMETER_K) {
        return st_exit;
      }
    }
  }

  return EFI_INVALID_PARAMETER_K;
}

void maybe_exit_boot_services_after_native_runtime(void) {
  struct x64_native_runtime_gate_status gate;

  if (g_exit_boot_services_done || g_exit_boot_services_attempted) {
    return;
  }
  x64_native_runtime_gate_eval(g_h, &g_input_runtime,
                               g_exit_boot_services_attempted,
                               g_exit_boot_services_done,
                               g_exit_boot_services_status, &gate);
  if (!handoff_boot_services_active()) {
    return;
  }
  if (!x64_native_runtime_gate_is_ready(&gate)) {
    return;
  }

  g_exit_boot_services_attempted = 1;
  klog(KLOG_INFO, "[boot] Tentando ExitBootServices no kernel.");
  g_exit_boot_services_status = kernel_exit_boot_services();
  if (g_exit_boot_services_status != EFI_SUCCESS_K) {
    klog_hex(KLOG_WARN, "[boot] ExitBootServices falhou/adiado. status=",
             g_exit_boot_services_status);
    update_system_runtime_platform_status();
    return;
  }

  g_exit_boot_services_done = 1;
  x64_input_retire_firmware_backend(&g_input_runtime);
  x64_platform_tables_init(1);
  if (g_input_runtime.hyperv_deferred) {
    klog(KLOG_INFO,
         "[hyperv] Teclado VMBus adiado; promocao sera tentada no input loop.");
    x64_input_enable_auto_promotion();
  }
  (void)x64_storage_runtime_try_enable_hyperv_native(
      handoff_boot_services_active(), kernel_allow_hybrid_storage_prepare(),
      klog_print_adapter);
  klog_print_adapter_flush();
  update_system_runtime_platform_status();
  klog(KLOG_INFO, "[boot] ExitBootServices concluido no kernel.");
}

int kernel_allow_hybrid_storage_prepare(void) {
  int allow = !handoff_boot_services_active();
  x64_storage_runtime_allow_hyperv_hybrid_prepare(allow);
  return allow;
}

void kernel_note_shell_session_ready(void) {
  if (x64_storage_runtime_hyperv_present()) {
    x64_storage_runtime_allow_hyperv_hybrid_prepare(0);
  }
}

void kernel_shell_after_native_runtime_ready(void) {
  struct x64_hyperv_runtime_coordinator_ops ops;
  kernel_hyperv_runtime_coordinator_ops_init(&ops);
  x64_hyperv_runtime_after_native_ready(&ops);
}

/* ── volume / shell runtime state builders ───────────────────────────── */

void kernel_volume_runtime_state_init(
    struct x64_kernel_volume_runtime_state *out) {
  if (!out) {
    return;
  }
  out->handoff = g_h;
  out->root_sb = &g_shell_root_sb;
  out->shell_persistent_storage = &g_shell_persistent_storage;
  out->active_volume_key = g_active_volume_key;
  out->active_volume_key_size = sizeof(g_active_volume_key);
  out->active_volume_key_ready = &g_active_volume_key_ready;
  out->handoff_volume_key = g_handoff_volume_key;
  out->handoff_volume_key_size = sizeof(g_handoff_volume_key);
  out->handoff_volume_key_ready = &g_handoff_volume_key_ready;
  out->data_io_probe = g_data_io_probe;
  out->data_io_probe_size = sizeof(g_data_io_probe);
  out->disk_salt = g_disk_salt;
  out->disk_salt_size = sizeof(g_disk_salt);
  out->kdf_iterations = g_kdf_iterations;
}

void kernel_volume_runtime_io_init(struct x64_kernel_volume_runtime_io *out) {
  if (!out) {
    return;
  }
  out->print = fbcon_print;
  out->print_hex = fbcon_print_hex;
  out->print_hex64 = fbcon_print_hex64;
  out->print_dec_u32 = fbcon_print_dec_u32;
  out->putc = fbcon_putc;
  out->readline = kernel_readline;
  out->print_active_efi_runtime_trace = print_active_efi_runtime_trace;
}

void kernel_shell_runtime_state_init(
    struct x64_kernel_shell_runtime_state *out) {
  if (!out) {
    return;
  }
  out->handoff = g_h;
  out->shell_ctx = &g_shell_ctx;
  out->session_ctx = &g_session_ctx;
  out->settings = &g_shell_settings;
  out->shell_initialized = &g_shell_initialized;
  out->shell_fs_ready = &g_shell_fs_ready;
  out->shell_persistent_storage = &g_shell_persistent_storage;
  out->shell_recovery_ram_fallback = &g_shell_recovery_ram_fallback;
  out->data_io_probe = g_data_io_probe;
  out->data_io_probe_size = sizeof(g_data_io_probe);
  out->allow_recovery_ram_fallback = 1u;
  out->desktop_autostart_pending = 0;
}

void kernel_shell_runtime_io_init(struct x64_kernel_shell_runtime_io *out) {
  if (!out) {
    return;
  }
  out->print = fbcon_print;
  out->print_hex = fbcon_print_hex;
  out->print_hex64 = fbcon_print_hex64;
  out->print_dec_u32 = fbcon_print_dec_u32;
  out->putc = fbcon_putc;
}

int load_handoff_volume_key(void) {
  struct x64_kernel_volume_runtime_state state;
  kernel_volume_runtime_state_init(&state);
  return x64_kernel_volume_runtime_load_handoff_key(&state);
}

static int fs_ensure_dir_recursive(const char *path) {
  return x64_kernel_volume_runtime_ensure_dir_recursive(path);
}

static int fs_write_text_file(const char *path, const char *text) {
  return x64_kernel_volume_runtime_write_text_file(path, text);
}

static int persist_active_volume_key_hash(void) {
  struct x64_kernel_volume_runtime_state state;
  kernel_volume_runtime_state_init(&state);
  return x64_kernel_volume_runtime_persist_active_key_hash(&state);
}

static int mount_encrypted_data_volume(struct block_device *data_dev) {
  struct x64_kernel_volume_runtime_state state;
  struct x64_kernel_volume_runtime_io io;
  kernel_volume_runtime_state_init(&state);
  kernel_volume_runtime_io_init(&io);
  return x64_kernel_volume_runtime_mount_encrypted_data_volume(&state, &io,
                                                               data_dev);
}

static int mount_root_CAPYFS(struct block_device *dev, const char *label) {
  struct x64_kernel_volume_runtime_state state;
  struct x64_kernel_volume_runtime_io io;
  kernel_volume_runtime_state_init(&state);
  kernel_volume_runtime_io_init(&io);
  return x64_kernel_volume_runtime_mount_root_capyfs(&state, &io, dev, label);
}

void kernel_shell_runtime_ops_init(struct x64_kernel_shell_runtime_ops *out) {
  if (!out) {
    return;
  }
  out->mount_root_capyfs = mount_root_CAPYFS;
  out->ensure_dir_recursive = fs_ensure_dir_recursive;
  out->write_text_file = fs_write_text_file;
  out->mount_encrypted_data_volume = mount_encrypted_data_volume;
  out->persist_active_volume_key_hash = persist_active_volume_key_hash;
  out->handoff_has_firmware_block_io = handoff_has_firmware_block_io;
  out->boot_services_active = handoff_boot_services_active;
  out->after_native_runtime_ready = kernel_shell_after_native_runtime_ready;
}

void kernel_hyperv_runtime_coordinator_ops_init(
    struct x64_hyperv_runtime_coordinator_ops *out) {
  if (!out) {
    return;
  }
  out->boot_services_active = handoff_boot_services_active;
  out->allow_hybrid_storage_prepare = kernel_allow_hybrid_storage_prepare;
  out->maybe_exit_boot_services_after_native_runtime =
      maybe_exit_boot_services_after_native_runtime;
  out->update_system_runtime_platform_status =
      update_system_runtime_platform_status;
  out->print_input_runtime_status = print_input_runtime_status;
  out->print_storage_runtime_status = print_storage_runtime_status;
  out->print = fbcon_print;
}

/* ── shell runtime setup ─────────────────────────────────────────────── */

int prepare_shell_runtime(void) {
  struct x64_kernel_shell_runtime_state state;
  struct x64_kernel_shell_runtime_io io;
  struct x64_kernel_shell_runtime_ops ops;
  int rc = 0;
  kernel_shell_runtime_state_init(&state);
  kernel_shell_runtime_io_init(&io);
  kernel_shell_runtime_ops_init(&ops);
  dbg_runtime_puts("[pr] begin\n");
  rc = x64_kernel_prepare_shell_runtime(&state, &io, &ops);
  dbg_runtime_puts(rc == 0 ? "[pr] ok\n" : "[pr] fail\n");
  return rc;
}

int init_shell_context(const struct user_record *user) {
  struct x64_kernel_shell_runtime_state state;
  int rc = 0;
  kernel_shell_runtime_state_init(&state);
  if (prepare_shell_runtime() != 0 || !user || !user->username[0]) {
    return -1;
  }
  rc = x64_kernel_begin_shell_session(&state, user);
  if (rc == 0) {
    kernel_note_shell_session_ready();
    (void)klog_persist_flush_default();
    (void)kernel_sync_root_volume();
    if (state.desktop_autostart_pending) {
      (void)klog_persist_flush_default();
      (void)kernel_sync_root_volume();
      (void)run_shell_alias("desktopstart");
    }
  }
  return rc;
}

/* ── login_runtime wrappers ──────────────────────────────────────────── */

void login_session_reset(struct session_context *ctx) {
  session_reset(ctx);
}

void login_session_set_active(struct session_context *ctx) {
  session_set_active(ctx);
}

void login_shell_context_init(struct shell_context *ctx,
                              struct session_context *session,
                              const struct system_settings *settings) {
  shell_context_init(ctx, session, settings);
}

int login_system_login(struct session_context *session,
                       const struct system_settings *settings) {
  return system_login(session, settings);
}

int login_maintenance_mode_active(void) {
  return x64_kernel_recovery_maintenance_active();
}

int login_consume_recovery_login_request(void) {
  int requested = g_recovery_login_requested;
  g_recovery_login_requested = 0;
  return requested;
}

void login_show_splash(const struct system_settings *settings) {
  if (!settings || !settings->splash_enabled) {
    return;
  }
  ui_boot_splash();
}

const struct user_record *
login_session_user(const struct session_context *ctx) {
  return session_user(ctx);
}

const char *login_session_cwd(const struct session_context *ctx) {
  return session_cwd(ctx);
}

int login_shell_context_should_logout(const struct shell_context *ctx) {
  return shell_context_should_logout(ctx);
}

int try_shell_command(char *line) {
  return x64_kernel_try_shell_command(&g_shell_ctx, g_shell_initialized, line);
}

int run_shell_alias(const char *alias_line) {
  return x64_kernel_run_shell_alias(&g_shell_ctx, g_shell_initialized,
                                    alias_line);
}

/* ── x64_kernel_manual_* (exported for recovery console) ─────────────── */

int x64_kernel_manual_prepare_hyperv_input(void) {
  struct x64_native_runtime_gate_status gate;

  if (handoff_boot_services_active()) {
    update_system_runtime_platform_status();
    return 0;
  }

  if (g_input_runtime.hyperv_deferred) {
    int rc = x64_input_force_enable_hyperv_native(
        &g_input_runtime, handoff_boot_services_active(), klog_print_adapter);
    klog_print_adapter_flush();
    update_system_runtime_platform_status();
    return rc;
  }

  x64_native_runtime_gate_eval(g_h, &g_input_runtime,
                               g_exit_boot_services_attempted,
                               g_exit_boot_services_done,
                               g_exit_boot_services_status, &gate);
  if (gate.gate != SYSTEM_EXIT_BOOT_SERVICES_GATE_WAIT_INPUT) {
    update_system_runtime_platform_status();
    return 0;
  }

  {
    int rc = x64_input_try_prepare_hyperv_runtime(&g_input_runtime,
                                                  handoff_boot_services_active(),
                                                  klog_print_adapter);
    klog_print_adapter_flush();
    update_system_runtime_platform_status();
    return rc;
  }
}

int x64_kernel_manual_prepare_hyperv_storage(void) {
  int rc = x64_storage_runtime_manual_hyperv_step(
      handoff_boot_services_active(), klog_print_adapter);
  klog_print_adapter_flush();
  update_system_runtime_platform_status();
  return rc;
}

int x64_kernel_manual_prepare_native_bridge(void) {
  int rc = x64_platform_tables_prepare_bridge();
  if (rc > 0) {
    klog(KLOG_INFO,
         "[runtime] Bridge nativo x64 armado: GDT/IDT/PIC do kernel ativos antes do EBS.");
  }
  update_system_runtime_platform_status();
  return rc;
}

int x64_kernel_manual_prepare_hyperv_synic(void) {
  int rc = 0;

  if (!handoff_boot_services_active()) {
    update_system_runtime_platform_status();
    return 0;
  }
  if (!x64_platform_tables_active() && !x64_platform_tables_bridge_active()) {
    update_system_runtime_platform_status();
    return 0;
  }
  if (!vmbus_runtime_hypercall_prepared()) {
    update_system_runtime_platform_status();
    return 0;
  }

  rc = vmbus_runtime_prepare_synic();
  if (rc == 0) {
    klog(KLOG_INFO,
         "[hyperv] SynIC preparado em passo manual/controlado; conexao do VMBus segue desativada.");
    update_system_runtime_platform_status();
    return 1;
  }

  update_system_runtime_platform_status();
  return -1;
}

int x64_kernel_manual_try_exit_boot_services(void) {
  int exit_done_before = g_exit_boot_services_done;
  EFI_STATUS_K exit_status_before = g_exit_boot_services_status;
  struct x64_hyperv_runtime_coordinator_ops ops;
  int rc = 0;
  kernel_hyperv_runtime_coordinator_ops_init(&ops);

  maybe_exit_boot_services_after_native_runtime();
  if (g_exit_boot_services_done != exit_done_before) {
    rc = x64_hyperv_runtime_poll_promotions(&g_input_runtime, &ops);
    update_system_runtime_platform_status();
    return rc < 0 ? -1 : 1;
  }
  if (g_exit_boot_services_status != exit_status_before &&
      g_exit_boot_services_status != EFI_SUCCESS_K) {
    update_system_runtime_platform_status();
    return -1;
  }
  update_system_runtime_platform_status();
  return 0;
}

int x64_kernel_manual_native_runtime_step(void) {
  struct x64_hyperv_runtime_coordinator_ops ops;
  struct x64_native_runtime_gate_status gate;
  int changed = 0;
  int failed = 0;
  int prepared_transition = 0;
  int exit_done_before = g_exit_boot_services_done;
  int rc = 0;
  kernel_hyperv_runtime_coordinator_ops_init(&ops);

  x64_native_runtime_gate_eval(g_h, &g_input_runtime,
                               g_exit_boot_services_attempted,
                               g_exit_boot_services_done,
                               g_exit_boot_services_status, &gate);
  if (gate.gate == SYSTEM_EXIT_BOOT_SERVICES_GATE_WAIT_INPUT &&
      !handoff_boot_services_active()) {
    rc = x64_kernel_manual_prepare_hyperv_input();
    if (rc > 0) {
      changed = 1;
      prepared_transition = 1;
    } else if (rc < 0) {
      failed = 1;
    }
  } else if (gate.gate == SYSTEM_EXIT_BOOT_SERVICES_GATE_WAIT_STORAGE_FIRMWARE) {
    rc = x64_kernel_manual_prepare_hyperv_storage();
    if (rc > 0) {
      changed = 1;
      prepared_transition = 1;
    } else if (rc < 0) {
      failed = 1;
    }
  }
  if (!prepared_transition) {
    rc = x64_kernel_manual_try_exit_boot_services();
    if (rc > 0) {
      changed = 1;
    } else if (rc < 0) {
      failed = 1;
    }
  }
  if (exit_done_before != g_exit_boot_services_done) {
    changed = 1;
  }
  rc = x64_hyperv_runtime_poll_promotions(&g_input_runtime, &ops);
  if (rc > 0) {
    changed = 1;
  } else if (rc < 0) {
    failed = 1;
  }
  update_system_runtime_platform_status();
  if (changed) {
    return 1;
  }
  return failed ? -1 : 0;
}
