#ifndef ARCH_X86_64_KERNEL_VOLUME_RUNTIME_H
#define ARCH_X86_64_KERNEL_VOLUME_RUNTIME_H

#include <stddef.h>
#include <stdint.h>

#include "boot/handoff.h"
#include "fs/block.h"
#include "fs/vfs.h"

#define X64_KERNEL_VOLUME_KEY_MAX 64

struct x64_kernel_volume_runtime_state {
  const struct boot_handoff *handoff;
  struct super_block *root_sb;
  int *shell_persistent_storage;
  char *active_volume_key;
  size_t active_volume_key_size;
  int *active_volume_key_ready;
  char *handoff_volume_key;
  size_t handoff_volume_key_size;
  int *handoff_volume_key_ready;
  uint8_t *data_io_probe;
  size_t data_io_probe_size;
  const uint8_t *disk_salt;
  size_t disk_salt_size;
  uint32_t kdf_iterations;
};

struct x64_kernel_volume_runtime_io {
  void (*print)(const char *message);
  void (*print_hex)(uint64_t value);
  void (*print_hex64)(uint64_t value);
  void (*print_dec_u32)(uint32_t value);
  void (*putc)(char ch);
  size_t (*readline)(char *buf, size_t maxlen, int mask);
  void (*print_active_efi_runtime_trace)(void);
};

int x64_kernel_volume_runtime_load_handoff_key(
    struct x64_kernel_volume_runtime_state *state);
int x64_kernel_volume_runtime_ensure_dir_recursive(const char *path);
int x64_kernel_volume_runtime_write_text_file(const char *path,
                                              const char *text);
int x64_kernel_volume_runtime_append_text_file(const char *path,
                                               const char *text);
int x64_kernel_volume_runtime_mount_root_capyfs(
    struct x64_kernel_volume_runtime_state *state,
    const struct x64_kernel_volume_runtime_io *io, struct block_device *dev,
    const char *label);
int x64_kernel_volume_runtime_persist_active_key_hash(
    const struct x64_kernel_volume_runtime_state *state);
int x64_kernel_volume_runtime_mount_encrypted_data_volume(
    struct x64_kernel_volume_runtime_state *state,
    const struct x64_kernel_volume_runtime_io *io,
    struct block_device *data_dev);

#endif /* ARCH_X86_64_KERNEL_VOLUME_RUNTIME_H */
