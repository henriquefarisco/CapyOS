/* config_internal.h: shared helpers for the config/ module. */
#ifndef CONFIG_INTERNAL_H
#define CONFIG_INTERNAL_H

#include "core/system_init.h"
#include "util/kstring.h"
#include "lang/localization.h"
#include "services/service_manager.h"
#include "auth/session.h"
#include "auth/user.h"
#include "drivers/console/tty.h"
#include "drivers/input/keyboard.h"
#include "drivers/video/vga.h"
#include "fs/buffer.h"
#include "fs/vfs.h"
#include "memory/kmem.h"

#include <stdint.h>

#define SYS_PATH_MAX 128
#define SETUP_LOG_CAPACITY 8192

/* ---- string/memory wrappers (inline) ---- */

static inline size_t cstring_length(const char *s) { return kstrlen(s); }
static inline void memory_zero(void *dst, size_t len) { kmemzero(dst, len); }
static inline void cstring_copy(char *dst, size_t dst_size, const char *src) {
  kstrcpy(dst, dst_size, src);
}
static inline int strings_equal(const char *a, const char *b) {
  return kstreq(a, b);
}

/* ---- shared buffer helper ---- */
void config_buffer_append(char *dst, size_t dst_size, const char *src);

/* ---- normalizers (defined in system_setup.c) ---- */
const char *normalize_keyboard_layout_name(const char *input);
const char *system_language_or_default(const char *language);
const char *system_network_mode_or_default(const char *mode);
const char *system_update_channel_or_default(const char *channel);
const char *system_update_branch_for_channel(const char *channel);
const char *system_update_manifest_for_channel(const char *channel);
const char *system_service_target_or_default(const char *target);
void system_ipv4_to_string(uint32_t ip, char out[16]);
int system_parse_ipv4(const char *text, uint32_t *out);

/* ---- boot defaults (defined in system_setup.c) ---- */
extern char g_boot_default_keyboard_layout[16];
extern char g_boot_default_language[16];
extern struct system_runtime_platform g_runtime_platform;

/* ---- setup logging (defined in src/config/first_boot modules) ---- */
void config_log_event(const char *msg);
void config_print_line(const char *msg);
void config_log_buffer_append(const char *msg);
void config_log_flush_pending(void);
void config_log_emit_segments(const char *a, const char *b, const char *c,
                              const char *d, const char *e);
void config_log_process_begin(const char *name);
void config_log_process_begin_success(const char *name);
void config_log_process_progress(const char *name);
void config_log_process_conclude(const char *name);
void config_log_process_finalize(const char *name);
void config_log_process_finalize_success(const char *name);
void config_log_dependency_wait(const char *dependency, const char *target);

/* ---- filesystem helpers (defined in src/config/first_boot modules) ---- */
int config_ensure_directory(const char *path);
int config_write_text_file(const char *path, const char *text);

/* ---- setup wizard UI (defined in system_setup_wizard.c) ---- */
enum system_ui_text_id {
  SYS_UI_PASSWORD_CONFIRM_PROMPT = 0,
  SYS_UI_PASSWORD_EMPTY,
  SYS_UI_PASSWORD_MISMATCH,
  SYS_UI_LAYOUTS_AVAILABLE,
  SYS_UI_LAYOUT_PROMPT,
  SYS_UI_LAYOUT_APPLIED_PREFIX,
  SYS_UI_LAYOUT_UNKNOWN,
  SYS_UI_HOSTNAME_PROMPT,
  SYS_UI_HOSTNAME_DEFINED_PREFIX,
  SYS_UI_THEMES_AVAILABLE,
  SYS_UI_THEME_PROMPT,
  SYS_UI_THEME_SELECTED_PREFIX,
  SYS_UI_SPLASH_PROMPT,
  SYS_UI_SPLASH_ENABLED,
  SYS_UI_SPLASH_DISABLED,
  SYS_UI_ADMIN_USER_PROMPT,
  SYS_UI_ADMIN_USER_INVALID,
  SYS_UI_ADMIN_HOME_CREATE_FAIL,
  SYS_UI_ADMIN_HOME_UNAVAILABLE,
  SYS_UI_ADMIN_HOME_PERM_WARNING,
  SYS_UI_ADMIN_EXISTS,
  SYS_UI_ADMIN_HOME_REBUILD_PREFIX,
  SYS_UI_ADMIN_HOME_REBUILD_FAIL,
  SYS_UI_ADMIN_PASSWORD_PROMPT_PREFIX,
  SYS_UI_ADMIN_REGISTER_FAIL,
  SYS_UI_ADMIN_RECORD_BUILD_FAIL,
  SYS_UI_ADMIN_SAVE_FAIL,
  SYS_UI_ADMIN_AUTH_REBUILD_FAIL,
  SYS_UI_ADMIN_USERDB_REBUILD_FAIL,
  SYS_UI_ADMIN_VALIDATED,
  SYS_UI_CONFIG_WRITE_FAIL,
  SYS_UI_CONFIG_VALIDATED,
  SYS_UI_FIRST_BOOT_COMPLETE_FAIL,
  SYS_UI_LOGIN_TITLE,
  SYS_UI_LOGIN_HOST_PREFIX,
  SYS_UI_LOGIN_USERNAME_PROMPT,
  SYS_UI_LOGIN_PASSWORD_PROMPT,
  SYS_UI_LOGIN_CREDENTIALS_REQUIRED,
  SYS_UI_LOGIN_INVALID,
};

const char *system_ui_text(const char *language, enum system_ui_text_id id);

size_t wizard_prompt(const char *prompt, char *buffer, size_t buffer_len,
                     int secret);
size_t wizard_prompt_setup(uint32_t progress, const char *title,
                           const char *prompt, char *buffer,
                           size_t buffer_len, int secret);
int wizard_menu_select_setup(uint32_t progress, const char *title,
                             const char *language,
                             const char *const *items, size_t count,
                             size_t default_index);
int prompt_password_pair(const char *label, char *password,
                         size_t password_len, const char *language);
void wizard_draw_header(uint32_t progress, const char *title);
const char *system_ui_menu_enabled(const char *language);
const char *system_ui_menu_disabled(const char *language);
const char *system_ui_splash_menu_title(const char *language);

/* ---- settings file helpers (defined in system_settings.c) ---- */
int config_write_settings_file(const struct system_settings *settings);
int config_write_update_repository_file(const struct system_settings *settings);
int config_verify_config_file(const char *hostname, const char *theme,
                              const char *keyboard, const char *language,
                              const char *update_channel,
                              const char *network_mode,
                              const char *service_target, int splash_enabled,
                              uint32_t ipv4_addr, uint32_t ipv4_mask,
                              uint32_t ipv4_gateway, uint32_t ipv4_dns);

/* ---- misc helpers (defined in src/config/first_boot modules) ---- */
void config_u32_to_string(uint32_t value, char *buf, size_t buf_len);
void config_sync_root_device(void);

#endif
