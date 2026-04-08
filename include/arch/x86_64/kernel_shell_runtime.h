#ifndef ARCH_X86_64_KERNEL_SHELL_RUNTIME_H
#define ARCH_X86_64_KERNEL_SHELL_RUNTIME_H

#include <stddef.h>
#include <stdint.h>

#include "boot/handoff.h"
#include "core/session.h"
#include "core/system_init.h"
#include "core/user.h"
#include "fs/block.h"
#include "shell/core.h"

struct x64_kernel_shell_runtime_state {
  const struct boot_handoff *handoff;
  struct shell_context *shell_ctx;
  struct session_context *session_ctx;
  struct system_settings *settings;
  int *shell_initialized;
  int *shell_fs_ready;
  int *shell_persistent_storage;
  uint8_t *data_io_probe;
  size_t data_io_probe_size;
};

struct x64_kernel_shell_runtime_io {
  void (*print)(const char *message);
  void (*print_hex)(uint64_t value);
  void (*print_hex64)(uint64_t value);
  void (*print_dec_u32)(uint32_t value);
  void (*putc)(char ch);
};

struct x64_kernel_shell_runtime_ops {
  int (*mount_root_capyfs)(struct block_device *dev, const char *label);
  int (*ensure_dir_recursive)(const char *path);
  int (*write_text_file)(const char *path, const char *text);
  int (*mount_encrypted_data_volume)(struct block_device *data_dev);
  int (*persist_active_volume_key_hash)(void);
  int (*handoff_has_firmware_block_io)(void);
  int (*boot_services_active)(void);
  void (*after_native_runtime_ready)(void);
};

int x64_kernel_prepare_shell_runtime(
    struct x64_kernel_shell_runtime_state *state,
    const struct x64_kernel_shell_runtime_io *io,
    const struct x64_kernel_shell_runtime_ops *ops);
int x64_kernel_begin_shell_session(
    struct x64_kernel_shell_runtime_state *state,
    const struct user_record *user);

#endif /* ARCH_X86_64_KERNEL_SHELL_RUNTIME_H */
