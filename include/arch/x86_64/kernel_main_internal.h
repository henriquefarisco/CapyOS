/* kernel_main_internal.h — Shared state and helpers for kernel_main split.
 *
 * This header is INTERNAL to the kernel_main translation-unit group.
 * Files that include this:
 *   - kernel_main.c          (entry point + glue)
 *   - framebuffer_console.c  (fbcon_* rendering)
 *   - boot_splash.c          (splash screen / ASCII banner)
 *   - kernel_services.c      (service poll / start / stop handlers)
 *   - kernel_runtime_ops.c   (login wrappers, volume/shell runtime builders)
 *   - kernel_io_helpers.c    (filesystem helpers, recovery reports)
 *
 * Do NOT include this from files outside src/arch/x86_64/.
 */

#ifndef ARCH_X86_64_KERNEL_MAIN_INTERNAL_H
#define ARCH_X86_64_KERNEL_MAIN_INTERNAL_H

#include <stddef.h>
#include <stdint.h>

#include "arch/x86_64/kernel_volume_runtime.h" /* X64_KERNEL_VOLUME_KEY_MAX */
#include "fs/capyfs.h"                         /* CAPYFS_BLOCK_SIZE        */

#include "boot/handoff.h"
#include "drivers/efi/efi_console.h"
#include "kernel/log/klog.h"

/* Forward-declare aggregate types used in function signatures.  The
 * individual .c files that need the full definitions pull in the
 * real headers (shell/core.h, auth/session.h, etc.). */
struct shell_context;
struct session_context;
struct super_block;
struct system_settings;
struct system_service_boot_policy_decision;
struct user_record;
struct x64_input_runtime;
struct x64_platform_diag_io;
struct x64_kernel_volume_runtime_state;
struct x64_kernel_volume_runtime_io;
struct x64_kernel_shell_runtime_state;
struct x64_kernel_shell_runtime_io;
struct x64_kernel_shell_runtime_ops;
struct x64_hyperv_runtime_coordinator_ops;
struct capyfs_check_report;

/* ── inline asm helpers (shared across TUs) ──────────────────────────── */

static inline void dbgcon_putc(uint8_t c) {
  __asm__ volatile("outb %0, %1" : : "a"(c), "Nd"((uint16_t)0xE9));
}

static inline void outb_inline(uint16_t port, uint8_t val) {
  __asm__ volatile("outb %0, %1" : : "a"(val), "Nd"(port));
}

static inline uint8_t inb_inline(uint16_t port) {
  uint8_t ret;
  __asm__ volatile("inb %1, %0" : "=a"(ret) : "Nd"(port));
  return ret;
}

static inline void cpu_relax(void) { __asm__ volatile("pause"); }

/* ── fbcon_t type (shared) ───────────────────────────────────────────── */

typedef struct {
  uint32_t *fb;
  uint32_t width;
  uint32_t height;
  uint32_t stride;
  uint32_t origin_y;
  uint32_t cols;
  uint32_t rows;
  uint32_t col;
  uint32_t row;
  uint32_t fg;
  uint32_t bg;
} fbcon_t;

#define FONT_W    8u
#define FONT_H    8u
#define FONT_SCALE 2u
#define CELL_W    (FONT_W * FONT_SCALE)
#define CELL_H    (FONT_H * FONT_SCALE)

/* ── shared globals (defined in framebuffer_console.c) ───────────────── */

extern fbcon_t g_con;
extern int g_serial_mirror;
extern int g_com1_ready;

/* ── shared globals (defined in boot_splash.c) ───────────────────────── */

extern uint32_t g_theme_splash_bg;
extern uint32_t g_theme_splash_icon;
extern uint32_t g_theme_splash_bar_border;
extern uint32_t g_theme_splash_bar_bg;
extern uint32_t g_theme_splash_bar_fill;

/* ── shared globals (defined in kernel_main.c) ───────────────────────── */

extern const struct boot_handoff *g_h;
extern struct x64_input_runtime g_input_runtime;
extern int g_exit_boot_services_attempted;
extern int g_exit_boot_services_done;
extern EFI_STATUS_K g_exit_boot_services_status;
extern int g_network_runtime_refresh_enabled;

/* Shell / volume key state (defined in kernel_io_helpers.c) */
extern struct shell_context g_shell_ctx;
extern struct session_context g_session_ctx;
extern struct super_block g_shell_root_sb;
extern struct system_settings g_shell_settings;
extern struct system_service_boot_policy_decision g_boot_policy_decision;
extern int g_shell_initialized;
extern int g_shell_fs_ready;
extern int g_shell_persistent_storage;
extern int g_shell_recovery_ram_fallback;
extern int g_runtime_maintenance_mode;
extern int g_recovery_login_requested;

/* Volume key buffers (defined in kernel_io_helpers.c) */
extern char g_active_volume_key[X64_KERNEL_VOLUME_KEY_MAX];
extern int g_active_volume_key_ready;
extern char g_handoff_volume_key[X64_KERNEL_VOLUME_KEY_MAX];
extern int g_handoff_volume_key_ready;
extern uint8_t g_data_io_probe[CAPYFS_BLOCK_SIZE];
extern const uint8_t g_disk_salt[16];
extern const uint32_t g_kdf_iterations;

/* ── fbcon API (framebuffer_console.c) ───────────────────────────────── */

void fbcon_init(const struct boot_handoff *h);
void fbcon_fill_rect_px(uint32_t x0, uint32_t y0, uint32_t w, uint32_t h,
                        uint32_t color);
void fbcon_scroll(void);
void fbcon_putch_px(uint32_t x, uint32_t y, char c);
void fbcon_putc(char c);
void fbcon_print(const char *s);
void fbcon_putch_at(uint32_t x, uint32_t y, char c, uint32_t fg, uint32_t bg);
void fbcon_clear_view(void);
void fbcon_print_hex64(uint64_t v);
void fbcon_print_hex(uint64_t v);
void fbcon_print_dec_u32(uint32_t v);
void fbcon_print_hex8(uint8_t v);
void fbcon_print_hex16(uint16_t v);
void fbcon_print_ipv4(uint32_t ip);
void fbcon_print_mac(const uint8_t mac[6]);
void fbcon_set_visual_muted(int muted);
void fbcon_muted_flush_line(void);
void kernel_platform_diag_io_init(struct x64_platform_diag_io *out);

/* Theme apply/sync (framebuffer_console.c — called from kernel_main.c) */
void system_platform_apply_theme(const char *theme);
void system_platform_sync_theme(const struct system_settings *settings);

/* ── boot_splash.c ───────────────────────────────────────────────────── */

void ui_boot_splash(void);
void ui_draw_capyos_icon(uint32_t x0, uint32_t y0, uint32_t scale,
                         uint32_t color);
void ui_draw_bars(void);
void ui_banner(void);
void cmd_info(void);
int rsdp_is_valid(uint64_t rsdp_addr);

/* ── kernel_io_helpers.c ─────────────────────────────────────────────── */

int streq(const char *a, const char *b);
void local_copy(char *dst, size_t dst_size, const char *src);
size_t local_length(const char *text);
void buffer_append_text(char *dst, size_t dst_size, const char *src);
void buffer_append_u32(char *dst, size_t dst_size, uint32_t val);
void buffer_append_yes_no(char *dst, size_t dst_size, int flag);
int kernel_ensure_directory_recursive(const char *path);
int kernel_sync_root_volume(void);
int kernel_write_text_file(const char *path, const char *text);
int kernel_append_text_file(const char *path, const char *text);
const char *kernel_target_name(uint32_t target);
uint32_t kernel_active_service_target(void);
int kernel_capyfs_check_current(struct capyfs_check_report *out);
int kernel_persist_recovery_report(void);
int kernel_append_recovery_history_event(const char *event_name);
void kernel_persist_recovery_artifacts(const char *event_name);
int handoff_keyboard_layout(char *out, size_t out_size);
int handoff_language(char *out, size_t out_size);
int handoff_hostname(char *out, size_t out_size);
int handoff_theme(char *out, size_t out_size);
int handoff_admin_username(char *out, size_t out_size);
int handoff_admin_password(char *out, size_t out_size);
int handoff_splash_enabled(void);
int handoff_boot_services_active(void);
int handoff_has_firmware_input(void);
int handoff_has_firmware_block_io(void);
int handoff_has_exit_boot_services_contract(void);
void print_input_runtime_status(void);
void print_storage_runtime_status(void);
void update_system_runtime_platform_status(void);
void kernel_maybe_refresh_network_runtime(void);
void kernel_update_logger_service_status(int rc);

/* ── kernel_services.c ───────────────────────────────────────────────── */

void kernel_update_network_service_status(void);
int kernel_service_poll_networkd(void *ctx);
int kernel_service_start_networkd(void *ctx);
int kernel_service_stop_networkd(void *ctx);
int kernel_service_poll_logger(void *ctx);
int kernel_service_start_logger(void *ctx);
int kernel_service_stop_logger(void *ctx);
int kernel_service_poll_update_agent(void *ctx);
int kernel_service_start_update_agent(void *ctx);
int kernel_service_stop_update_agent(void *ctx);
void kernel_update_update_agent_service_status(int rc);
int kernel_work_recovery_snapshot(void *ctx);
int kernel_work_gpu_discovery(void *ctx);
int kernel_work_usb_bringup(void *ctx);
int kernel_work_update_agent_warmup(void *ctx);
void kernel_update_recovery_snapshot_work(int schedule_now);
void kernel_schedule_background_boot_work(int shell_runtime_ready);
void kernel_service_poll(void);
void x64_kernel_runtime_poll_background(void);
uint32_t kernel_service_target_from_settings(
    const struct system_settings *settings);
void kernel_log_boot_policy_decision(
    const struct system_service_boot_policy_decision *decision);
int kernel_boots_in_maintenance_mode(void);
const char *kernel_boot_maintenance_reason(void);
int kernel_start_maintenance_session(struct session_context *session,
                                     const struct system_settings *settings);

/* ── kernel_runtime_ops.c ────────────────────────────────────────────── */

/* login_runtime wrapper table builders */
void login_session_reset(struct session_context *ctx);
void login_session_set_active(struct session_context *ctx);
void login_shell_context_init(struct shell_context *ctx,
                              struct session_context *session,
                              const struct system_settings *settings);
int login_system_login(struct session_context *session,
                       const struct system_settings *settings);
int login_maintenance_mode_active(void);
int login_consume_recovery_login_request(void);
void login_show_splash(const struct system_settings *settings);
const struct user_record *login_session_user(const struct session_context *ctx);
const char *login_session_cwd(const struct session_context *ctx);
int login_shell_context_should_logout(const struct shell_context *ctx);
int try_shell_command(char *line);
int run_shell_alias(const char *alias_line);
int init_shell_context(const struct user_record *user);
int prepare_shell_runtime(void);
int kernel_start_maintenance_session(struct session_context *session,
                                     const struct system_settings *settings);

/* volume / shell runtime state builders */
void kernel_volume_runtime_state_init(
    struct x64_kernel_volume_runtime_state *out);
void kernel_volume_runtime_io_init(struct x64_kernel_volume_runtime_io *out);
void kernel_shell_runtime_state_init(struct x64_kernel_shell_runtime_state *out);
void kernel_shell_runtime_io_init(struct x64_kernel_shell_runtime_io *out);
void kernel_shell_runtime_ops_init(struct x64_kernel_shell_runtime_ops *out);
void kernel_hyperv_runtime_coordinator_ops_init(
    struct x64_hyperv_runtime_coordinator_ops *out);

/* EFI ExitBootServices */
void maybe_exit_boot_services_after_native_runtime(void);
int kernel_allow_hybrid_storage_prepare(void);
void kernel_shell_after_native_runtime_ready(void);
void kernel_note_shell_session_ready(void);

/* klog adapter */
void klog_print_adapter(const char *s);
void klog_print_adapter_flush(void);
size_t kernel_readline(char *buf, size_t maxlen, int mask);

/* input */
int kernel_input_getc(char *out_char);
int kernel_input_trygetc(char *out_char);
size_t kernel_input_readline(char *buf, size_t maxlen, int mask);

/* volume key loading */
int load_handoff_volume_key(void);

#endif /* ARCH_X86_64_KERNEL_MAIN_INTERNAL_H */
