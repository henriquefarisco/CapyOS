/* kernel_io_helpers.c — Filesystem helpers, handoff queries, recovery reports.
 *
 * Owns: shell/session/settings/volume-key globals, local_copy/length,
 * buffer_append helpers, handoff_* query functions, print_*_status wrappers,
 * kernel_ensure_directory_recursive, kernel_write/append_text_file,
 * kernel_persist_recovery_report/artifacts, kernel_sync_root_volume.
 *
 * Split from kernel_main.c to keep each TU ≤ 500 lines.
 */
#pragma GCC optimize("O0")
#include <stddef.h>
#include <stdint.h>

#include "arch/x86_64/kernel_main_internal.h"
#include "arch/x86_64/kernel_platform_runtime.h"
#include "arch/x86_64/kernel_volume_runtime.h"
#include "arch/x86_64/storage_runtime.h"
#include "arch/x86_64/input_runtime.h"
#include "boot/handoff.h"
#include "core/system_init.h"
#include "drivers/timer/pit.h"
#include "fs/block.h"
#include "fs/buffer.h"
#include "fs/capyfs.h"
#include "fs/vfs.h"
#include "net/network_bootstrap.h"
#include "net/stack.h"
#include "services/service_manager.h"
#include "services/service_boot_policy.h"
#include "services/update_agent.h"
#include "util/kstring.h"
#include "auth/session.h"
#include "shell/core.h"

/* ── globals (owned here, extern'd via internal header) ──────────────── */

struct shell_context g_shell_ctx;
struct session_context g_session_ctx;
struct super_block g_shell_root_sb;
struct system_settings g_shell_settings;
struct system_service_boot_policy_decision g_boot_policy_decision;
int g_shell_initialized = 0;
int g_shell_fs_ready = 0;
int g_shell_persistent_storage = 0;
int g_shell_recovery_ram_fallback = 0;
int g_runtime_maintenance_mode = 0;
int g_recovery_login_requested = 0;
char g_active_volume_key[X64_KERNEL_VOLUME_KEY_MAX];
int g_active_volume_key_ready = 0;
char g_handoff_volume_key[X64_KERNEL_VOLUME_KEY_MAX];
int g_handoff_volume_key_ready = 0;
uint8_t g_data_io_probe[CAPYFS_BLOCK_SIZE] __attribute__((aligned(64)));
const uint8_t g_disk_salt[16] = {0x4e, 0x6f, 0x69, 0x72, 0x4f, 0x53,
                                 0x2d, 0x46, 0x53, 0x2d, 0x53, 0x61,
                                 0x6c, 0x74, 0x21, 0x00};
const uint32_t g_kdf_iterations = 16000;

/* ── tiny string helpers ─────────────────────────────────────────────── */

int streq(const char *a, const char *b) {
  return kstreq(a, b);
}

void local_copy(char *dst, size_t dst_size, const char *src) {
  kstrcpy(dst, dst_size, src);
}

size_t local_length(const char *text) {
  return kstrlen(text);
}

void buffer_append_text(char *dst, size_t dst_size, const char *src) {
  kbuf_append(dst, dst_size, src);
}

void buffer_append_u32(char *dst, size_t dst_size, uint32_t val) {
  kbuf_append_u32(dst, dst_size, val);
}

void buffer_append_yes_no(char *dst, size_t dst_size, int flag) {
  kbuf_append_yesno(dst, dst_size, flag);
}

/* ── handoff query functions ─────────────────────────────────────────── */

int handoff_keyboard_layout(char *out, size_t out_size) {
  if (!out || out_size < 2 || !g_h || g_h->version < 3 ||
      !g_h->boot_keyboard_layout[0]) {
    return -1;
  }
  local_copy(out, out_size, g_h->boot_keyboard_layout);
  return 0;
}

int handoff_language(char *out, size_t out_size) {
  if (!out || out_size < 2 || !g_h || g_h->version < 7 ||
      !g_h->boot_language[0]) {
    return -1;
  }
  local_copy(out, out_size, g_h->boot_language);
  return 0;
}

int handoff_hostname(char *out, size_t out_size) {
  if (!out || out_size < 2 || !g_h || g_h->version < 8 ||
      !g_h->boot_hostname[0]) {
    return -1;
  }
  local_copy(out, out_size, g_h->boot_hostname);
  return 0;
}

int handoff_theme(char *out, size_t out_size) {
  if (!out || out_size < 2 || !g_h || g_h->version < 8 ||
      !g_h->boot_theme[0]) {
    return -1;
  }
  local_copy(out, out_size, g_h->boot_theme);
  return 0;
}

int handoff_admin_username(char *out, size_t out_size) {
  if (!out || out_size < 2 || !g_h || g_h->version < 8 ||
      !g_h->boot_admin_username[0]) {
    return -1;
  }
  local_copy(out, out_size, g_h->boot_admin_username);
  return 0;
}

int handoff_admin_password(char *out, size_t out_size) {
  if (!out || out_size < 2 || !g_h || g_h->version < 8 ||
      !g_h->boot_admin_password[0]) {
    return -1;
  }
  local_copy(out, out_size, g_h->boot_admin_password);
  return 0;
}

int handoff_splash_enabled(void) {
  if (!g_h || g_h->version < 8) {
    return -1;
  }
  return g_h->boot_splash_enabled ? 1 : 0;
}

int handoff_boot_services_active(void) {
  return x64_kernel_handoff_boot_services_active(g_h);
}

int handoff_has_firmware_input(void) {
  return x64_kernel_handoff_has_firmware_input(g_h);
}

int handoff_has_firmware_block_io(void) {
  return x64_kernel_handoff_has_firmware_block_io(g_h);
}

int handoff_has_exit_boot_services_contract(void) {
  return x64_kernel_handoff_has_exit_boot_services_contract(g_h);
}

/* ── print_*_status wrappers ─────────────────────────────────────────── */

void print_input_runtime_status(void) {
  struct x64_platform_diag_io io;
  kernel_platform_diag_io_init(&io);
  x64_kernel_print_input_runtime_status(&g_input_runtime, &io);
}

void print_storage_runtime_status(void) {
  struct x64_platform_diag_io io;
  kernel_platform_diag_io_init(&io);
  x64_kernel_print_storage_runtime_status(g_h, &io);
}

void update_system_runtime_platform_status(void) {
  x64_kernel_update_system_runtime_platform_status(
      g_h, &g_input_runtime, g_exit_boot_services_attempted,
      g_exit_boot_services_done, g_exit_boot_services_status);
}

void kernel_maybe_refresh_network_runtime(void) {
  if (!g_network_runtime_refresh_enabled) {
    return;
  }
  (void)net_stack_refresh_runtime();
}

void kernel_update_logger_service_status(int rc) {
  if (rc == 0) {
    (void)service_manager_set_state(
        SYSTEM_SERVICE_LOGGER, SYSTEM_SERVICE_STATE_READY, 0,
        "persistent klog active in /var/log/capyos_klog.txt");
  } else {
    (void)service_manager_set_state(
        SYSTEM_SERVICE_LOGGER, SYSTEM_SERVICE_STATE_DEGRADED, rc,
        "persistent klog unavailable; ring buffer only");
  }
}

/* ── filesystem helpers ──────────────────────────────────────────────── */

int kernel_ensure_directory_recursive(const char *path) {
  char build[128];
  struct vfs_metadata meta = {0, 0, 0755};
  struct vfs_stat st;
  size_t build_len = 0;
  const char *cursor = path;

  if (!path || path[0] != '/') {
    return -1;
  }

  build[build_len++] = '/';
  build[build_len] = '\0';
  while (*cursor == '/') {
    ++cursor;
  }
  while (*cursor) {
    const char *start = cursor;
    size_t segment_len = 0;
    while (cursor[segment_len] && cursor[segment_len] != '/') {
      ++segment_len;
    }
    if (segment_len > 0) {
      if (build_len > 1 && build[build_len - 1] != '/') {
        if (build_len + 1 >= sizeof(build)) {
          return -1;
        }
        build[build_len++] = '/';
      }
      if (build_len + segment_len >= sizeof(build)) {
        return -1;
      }
      for (size_t i = 0; i < segment_len; ++i) {
        build[build_len++] = start[i];
      }
      build[build_len] = '\0';
      if (vfs_stat_path(build, &st) != 0) {
        if (vfs_create(build, VFS_MODE_DIR, &meta) != 0) {
          return -1;
        }
      } else if ((st.mode & VFS_MODE_DIR) == 0) {
        return -1;
      }
    }
    cursor += segment_len;
    while (*cursor == '/') {
      ++cursor;
    }
  }
  return 0;
}

int kernel_sync_root_volume(void) {
  struct super_block *root = vfs_root();
  if (!root || !root->bdev) {
    return -1;
  }
  return buffer_cache_sync(root->bdev);
}

int kernel_write_text_file(const char *path, const char *text) {
  char parent[128];
  struct vfs_metadata meta = {0, 0, 0644};
  struct vfs_stat st;
  struct file *file = NULL;
  size_t path_len = 0;
  size_t split = 0;
  size_t len = 0;
  long written = 0;

  if (!path || path[0] != '/' || !text) {
    return -1;
  }

  path_len = local_length(path);
  if (path_len >= sizeof(parent)) {
    return -1;
  }
  local_copy(parent, sizeof(parent), path);
  while (split < path_len && parent[split]) {
    ++split;
  }
  while (split > 1 && parent[split - 1] != '/') {
    --split;
  }
  if (split == 0) {
    return -1;
  }
  if (split == 1) {
    parent[1] = '\0';
  } else {
    parent[split - 1] = '\0';
  }
  if (kernel_ensure_directory_recursive(parent) != 0) {
    return -1;
  }

  if (vfs_stat_path(path, &st) == 0) {
    if ((st.mode & VFS_MODE_DIR) != 0) {
      return -1;
    }
    if (vfs_unlink(path) != 0) {
      return -1;
    }
  }
  if (vfs_create(path, VFS_MODE_FILE, &meta) != 0) {
    return -1;
  }
  file = vfs_open(path, VFS_OPEN_WRITE);
  if (!file) {
    return -1;
  }
  len = local_length(text);
  written = vfs_write(file, text, len);
  vfs_close(file);
  if (written != (long)len) {
    return -1;
  }
  return kernel_sync_root_volume();
}

int kernel_append_text_file(const char *path, const char *text) {
  char parent[128];
  struct vfs_metadata meta = {0, 0, 0644};
  struct vfs_stat st;
  struct file *file = NULL;
  size_t path_len = 0;
  size_t split = 0;
  size_t len = 0;
  long written = 0;

  if (!path || path[0] != '/' || !text) {
    return -1;
  }

  path_len = local_length(path);
  if (path_len >= sizeof(parent)) {
    return -1;
  }
  local_copy(parent, sizeof(parent), path);
  while (split < path_len && parent[split]) {
    ++split;
  }
  while (split > 1 && parent[split - 1] != '/') {
    --split;
  }
  if (split == 0) {
    return -1;
  }
  if (split == 1) {
    parent[1] = '\0';
  } else {
    parent[split - 1] = '\0';
  }
  if (kernel_ensure_directory_recursive(parent) != 0) {
    return -1;
  }

  if (vfs_stat_path(path, &st) != 0) {
    if (vfs_create(path, VFS_MODE_FILE, &meta) != 0) {
      return -1;
    }
  } else if ((st.mode & VFS_MODE_DIR) != 0) {
    return -1;
  }

  file = vfs_open(path, VFS_OPEN_WRITE);
  if (!file) {
    return -1;
  }
  if (file->dentry && file->dentry->inode) {
    file->position = file->dentry->inode->size;
  }
  len = local_length(text);
  written = vfs_write(file, text, len);
  vfs_close(file);
  if (written != (long)len) {
    return -1;
  }
  return kernel_sync_root_volume();
}

/* ── service target helpers ──────────────────────────────────────────── */

const char *kernel_target_name(uint32_t target_id) {
  return service_manager_target_label(target_id);
}

uint32_t kernel_active_service_target(void) {
  struct system_service_target_status active_target;
  if (service_manager_target_current(&active_target) == 0) {
    return active_target.id;
  }
  return g_boot_policy_decision.final_target;
}

int kernel_capyfs_check_current(struct capyfs_check_report *out) {
  struct super_block *root = vfs_root();
  if (!root || !root->bdev || !out) {
    return -1;
  }
  return capyfs_check(root->bdev, out);
}

/* ── recovery report ─────────────────────────────────────────────────── */

int kernel_persist_recovery_report(void) {
  char report[1024];
  struct capyfs_check_report fs_report;
  struct net_stack_status net_status;
  struct system_update_status update_status;
  int fs_ok = 0;
  int net_ok = 0;

  if (!g_shell_fs_ready || !g_shell_persistent_storage) {
    return -1;
  }

  report[0] = '\0';
  fs_ok = kernel_capyfs_check_current(&fs_report) == 0;
  net_ok = net_stack_status(&net_status) == 0;

  buffer_append_text(report, sizeof(report), "CAPYOS recovery report\n");
  buffer_append_text(report, sizeof(report), "degraded=");
  buffer_append_yes_no(report, sizeof(report), g_boot_policy_decision.degraded);
  buffer_append_text(report, sizeof(report), "\nmaintenance_session=");
  buffer_append_yes_no(report, sizeof(report), g_runtime_maintenance_mode);
  buffer_append_text(report, sizeof(report), "\nforced_maintenance=");
  buffer_append_yes_no(report, sizeof(report),
                       g_boot_policy_decision.forced_maintenance);
  buffer_append_text(report, sizeof(report), "\nforced_core=");
  buffer_append_yes_no(report, sizeof(report), g_boot_policy_decision.forced_core);
  buffer_append_text(report, sizeof(report), "\nreason=");
  buffer_append_text(report, sizeof(report),
                     service_boot_policy_reason_label(g_boot_policy_decision.reason));
  buffer_append_text(report, sizeof(report), "\nsummary=");
  buffer_append_text(report, sizeof(report),
                     service_boot_policy_reason_summary(g_boot_policy_decision.reason));
  buffer_append_text(report, sizeof(report), "\nbootstrap_target=");
  buffer_append_text(report, sizeof(report),
                     kernel_target_name(g_boot_policy_decision.bootstrap_target));
  buffer_append_text(report, sizeof(report), "\nrequested_target=");
  buffer_append_text(report, sizeof(report),
                     kernel_target_name(g_boot_policy_decision.requested_target));
  buffer_append_text(report, sizeof(report), "\nboot_target=");
  buffer_append_text(report, sizeof(report),
                     kernel_target_name(g_boot_policy_decision.final_target));
  buffer_append_text(report, sizeof(report), "\nactive_target=");
  buffer_append_text(report, sizeof(report),
                     kernel_target_name(kernel_active_service_target()));
  buffer_append_text(report, sizeof(report), "\nsaved_target=");
  buffer_append_text(report, sizeof(report),
                     g_shell_settings.service_target[0]
                         ? g_shell_settings.service_target
                         : "network");
  buffer_append_text(report, sizeof(report), "\nshell_fs_ready=");
  buffer_append_yes_no(report, sizeof(report), g_shell_fs_ready);
  buffer_append_text(report, sizeof(report), "\npersistent_storage=");
  buffer_append_yes_no(report, sizeof(report), g_shell_persistent_storage);
  buffer_append_text(report, sizeof(report), "\nrecovery_ram_fallback=");
  buffer_append_yes_no(report, sizeof(report), g_shell_recovery_ram_fallback);
  buffer_append_text(report, sizeof(report), "\nvalidated_storage=");
  buffer_append_yes_no(report, sizeof(report), x64_storage_runtime_has_device());
  buffer_append_text(report, sizeof(report), "\ncapyfs=");
  buffer_append_text(report, sizeof(report),
                     fs_ok ? capyfs_check_result_label(fs_report.result)
                           : "unavailable");
  if (fs_ok) {
    buffer_append_text(report, sizeof(report), "\ncapyfs_root_entries=");
    buffer_append_u32(report, sizeof(report), fs_report.root_entries);
    buffer_append_text(report, sizeof(report), "\ncapyfs_reserved_blocks=");
    buffer_append_u32(report, sizeof(report), fs_report.reserved_blocks_expected);
  }
  buffer_append_text(report, sizeof(report), "\nnetwork_status=");
  buffer_append_text(report, sizeof(report), net_ok ? "available" : "unavailable");
  if (net_ok) {
    buffer_append_text(report, sizeof(report), "\nnetwork_runtime_supported=");
    buffer_append_yes_no(report, sizeof(report), net_status.runtime_supported);
    buffer_append_text(report, sizeof(report), "\nnetwork_ready=");
    buffer_append_yes_no(report, sizeof(report), net_status.ready);
  }
  update_agent_status_get(&update_status);
  buffer_append_text(report, sizeof(report), "\nupdate_catalog=");
  buffer_append_yes_no(report, sizeof(report), update_status.catalog_present);
  buffer_append_text(report, sizeof(report), "\nupdate_channel=");
  buffer_append_text(report, sizeof(report),
                     update_status.channel[0] ? update_status.channel : "stable");
  buffer_append_text(report, sizeof(report), "\nupdate_branch=");
  buffer_append_text(report, sizeof(report),
                     update_status.branch[0] ? update_status.branch : "main");
  buffer_append_text(report, sizeof(report), "\nupdate_available=");
  buffer_append_yes_no(report, sizeof(report), update_status.update_available);
  buffer_append_text(report, sizeof(report), "\nupdate_stage_ready=");
  buffer_append_yes_no(report, sizeof(report), update_status.stage_ready);
  buffer_append_text(report, sizeof(report), "\nupdate_pending_activation=");
  buffer_append_yes_no(report, sizeof(report), update_status.pending_activation);
  buffer_append_text(report, sizeof(report), "\nupdate_available_version=");
  buffer_append_text(report, sizeof(report),
                     update_status.available_version[0]
                         ? update_status.available_version
                         : "-");
  buffer_append_text(report, sizeof(report), "\nupdate_staged_version=");
  buffer_append_text(report, sizeof(report),
                     update_status.staged_version[0]
                         ? update_status.staged_version
                         : "-");
  buffer_append_text(report, sizeof(report), "\nupdate_summary=");
  buffer_append_text(report, sizeof(report),
                     update_status.summary[0] ? update_status.summary : "-");
  buffer_append_text(report, sizeof(report), "\n");

  return kernel_write_text_file("/var/log/recovery-boot.txt", report);
}

int kernel_append_recovery_history_event(const char *event_name) {
  char line[768];
  struct capyfs_check_report fs_report;
  struct net_stack_status net_status;
  struct system_update_status update_status;
  int fs_ok = 0;
  int net_ok = 0;

  if (!g_shell_fs_ready || !g_shell_persistent_storage) {
    return -1;
  }

  line[0] = '\0';
  fs_ok = kernel_capyfs_check_current(&fs_report) == 0;
  net_ok = net_stack_status(&net_status) == 0;

  buffer_append_text(line, sizeof(line), "ticks=");
  buffer_append_u32(line, sizeof(line), pit_ticks());
  buffer_append_text(line, sizeof(line), " event=");
  buffer_append_text(line, sizeof(line),
                     (event_name && event_name[0]) ? event_name : "unknown");
  buffer_append_text(line, sizeof(line), " degraded=");
  buffer_append_yes_no(line, sizeof(line), g_boot_policy_decision.degraded);
  buffer_append_text(line, sizeof(line), " maintenance=");
  buffer_append_yes_no(line, sizeof(line), g_runtime_maintenance_mode);
  buffer_append_text(line, sizeof(line), " reason=");
  buffer_append_text(line, sizeof(line),
                     service_boot_policy_reason_label(g_boot_policy_decision.reason));
  buffer_append_text(line, sizeof(line), " saved=");
  buffer_append_text(line, sizeof(line),
                     g_shell_settings.service_target[0]
                         ? g_shell_settings.service_target
                         : "network");
  buffer_append_text(line, sizeof(line), " boot=");
  buffer_append_text(line, sizeof(line),
                     kernel_target_name(g_boot_policy_decision.final_target));
  buffer_append_text(line, sizeof(line), " active=");
  buffer_append_text(line, sizeof(line),
                     kernel_target_name(kernel_active_service_target()));
  buffer_append_text(line, sizeof(line), " storage=");
  buffer_append_yes_no(line, sizeof(line), x64_storage_runtime_has_device());
  buffer_append_text(line, sizeof(line), " ram_fallback=");
  buffer_append_yes_no(line, sizeof(line), g_shell_recovery_ram_fallback);
  buffer_append_text(line, sizeof(line), " capyfs=");
  buffer_append_text(line, sizeof(line),
                     fs_ok ? capyfs_check_result_label(fs_report.result)
                           : "unavailable");
  buffer_append_text(line, sizeof(line), " network=");
  if (!net_ok) {
    buffer_append_text(line, sizeof(line), "unavailable");
  } else if (!net_status.runtime_supported) {
    buffer_append_text(line, sizeof(line), "unsupported");
  } else if (!net_status.ready) {
    buffer_append_text(line, sizeof(line), "degraded");
  } else {
    buffer_append_text(line, sizeof(line), "ready");
  }
  update_agent_status_get(&update_status);
  buffer_append_text(line, sizeof(line), " update=");
  if (update_status.pending_activation) {
    buffer_append_text(line, sizeof(line), "pending");
  } else if (update_status.stage_ready) {
    buffer_append_text(line, sizeof(line), "staged");
  } else if (update_status.update_available) {
    buffer_append_text(line, sizeof(line), "available");
  } else if (update_status.catalog_present) {
    buffer_append_text(line, sizeof(line), "catalog");
  } else {
    buffer_append_text(line, sizeof(line), "none");
  }
  buffer_append_text(line, sizeof(line), " channel=");
  buffer_append_text(line, sizeof(line),
                     update_status.channel[0] ? update_status.channel : "stable");
  buffer_append_text(line, sizeof(line), " branch=");
  buffer_append_text(line, sizeof(line),
                     update_status.branch[0] ? update_status.branch : "main");
  buffer_append_text(line, sizeof(line), " update_rc=");
  if (update_status.last_result < 0) {
    buffer_append_text(line, sizeof(line), "-");
    buffer_append_u32(line, sizeof(line), (uint32_t)(-update_status.last_result));
  } else {
    buffer_append_u32(line, sizeof(line), (uint32_t)update_status.last_result);
  }
  buffer_append_text(line, sizeof(line), "\n");

  return kernel_append_text_file("/var/log/recovery-history.log", line);
}

void kernel_persist_recovery_artifacts(const char *event_name) {
  (void)kernel_persist_recovery_report();
  (void)kernel_append_recovery_history_event(event_name);
}
