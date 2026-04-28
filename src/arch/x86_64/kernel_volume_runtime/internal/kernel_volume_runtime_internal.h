#ifndef KERNEL_VOLUME_RUNTIME_INTERNAL_H
#define KERNEL_VOLUME_RUNTIME_INTERNAL_H

#include "arch/x86_64/kernel_volume_runtime.h"
#include "arch/x86_64/storage_runtime.h"
#include "boot/boot_config.h"
#include "drivers/storage/efi_block.h"
#include "fs/buffer.h"
#include "fs/capyfs.h"
#include "fs/vfs.h"
#include "memory/kmem.h"
#include "security/crypt.h"
#include <stddef.h>
#include <stdint.h>

#define X64_VOLUME_KEY_HASH_HEX_LEN 64
#define X64_VOLUME_KEY_HASH_PATH "/system/volume.key.sha256"

static inline size_t local_strlen(const char *s) {
  size_t len = 0;
  if (!s) return 0;
  while (s[len]) ++len;
  return len;
}
static inline void local_copy(char *dst, size_t dst_size, const char *src) {
  if (!dst || dst_size == 0) return;
  size_t i = 0;
  if (src) {
    while (src[i] && i + 1 < dst_size) { dst[i] = src[i]; ++i; }
  }
  dst[i] = '\0';
}
static inline int streq(const char *a, const char *b) {
  size_t i = 0;
  if (!a || !b) return 0;
  while (a[i] && b[i]) {
    if (a[i] != b[i]) return 0;
    ++i;
  }
  return a[i] == '\0' && b[i] == '\0';
}
static inline void secure_memzero(void *ptr, size_t len) {
  volatile uint8_t *p = (volatile uint8_t *)ptr;
  while (len--) *p++ = 0;
}
static inline void io_print(const struct x64_kernel_volume_runtime_io *io,
                             const char *message) {
  if (io && io->print && message) io->print(message);
}
static inline void io_print_hex(const struct x64_kernel_volume_runtime_io *io,
                                uint64_t value) {
  if (io && io->print_hex) io->print_hex(value);
}
static inline void io_print_hex64(const struct x64_kernel_volume_runtime_io *io,
                                   uint64_t value) {
  if (io && io->print_hex64) io->print_hex64(value);
}
static inline void io_print_dec_u32(const struct x64_kernel_volume_runtime_io *io,
                                     uint32_t value) {
  if (io && io->print_dec_u32) io->print_dec_u32(value);
}
static inline void io_putc(const struct x64_kernel_volume_runtime_io *io, char ch) {
  if (io && io->putc) io->putc(ch);
}
static inline void dbg_putc(char ch) {
  __asm__ volatile("outb %0, %1" : : "a"((uint8_t)ch), "Nd"((uint16_t)0xE9));
}
static inline void dbg_puts(const char *s) {
  while (s && *s) dbg_putc(*s++);
}
static inline void dbg_hex32(uint32_t value) {
  static const char hex[] = "0123456789ABCDEF";
  for (int shift = 28; shift >= 0; shift -= 4) dbg_putc(hex[(value >> shift) & 0xFu]);
}
static inline uint32_t dbg_be32_local(const uint8_t *buf) {
  if (!buf) return 0;
  return ((uint32_t)buf[0] << 24) | ((uint32_t)buf[1] << 16) |
         ((uint32_t)buf[2] << 8) | (uint32_t)buf[3];
}
static inline size_t io_readline(const struct x64_kernel_volume_runtime_io *io,
                                  char *buf, size_t maxlen, int mask) {
  if (!io || !io->readline) return 0;
  return io->readline(buf, maxlen, mask);
}
static inline void io_print_active_efi_runtime_trace(
    const struct x64_kernel_volume_runtime_io *io) {
  if (io && io->print_active_efi_runtime_trace) io->print_active_efi_runtime_trace();
}

int normalize_volume_key_input(const char *input, char *out, size_t out_size);
void format_volume_key_groups(const char *normalized, char *out, size_t out_size);
void bytes_to_hex_local(const uint8_t *src, size_t len, char *dst, size_t dst_size);
int hex_to_bytes_local(const char *hex, size_t hex_len, uint8_t *dst, size_t dst_len);

int compute_volume_key_hash(const char *normalized_key,
                            uint8_t out_hash[SHA256_DIGEST_SIZE]);
int save_volume_key_hash(const char *normalized_key);
int load_volume_key_hash(uint8_t out_hash[SHA256_DIGEST_SIZE]);
int append_key_candidate(const char **list, size_t *count, size_t cap,
                         const char *candidate);
struct block_device *open_crypt_volume_with_password(
    const struct x64_kernel_volume_runtime_state *state,
    struct block_device *data_dev, const char *password);
void reset_blank_probe_regions(const struct x64_kernel_volume_runtime_state *state,
                               struct block_device *dev);
int device_is_blank(const struct x64_kernel_volume_runtime_state *state,
                    struct block_device *dev);

int mount_root_capyfs(struct x64_kernel_volume_runtime_state *state,
                      const struct x64_kernel_volume_runtime_io *io,
                      struct block_device *dev, const char *label);
int initialize_encrypted_data_volume(
    struct x64_kernel_volume_runtime_state *state,
    const struct x64_kernel_volume_runtime_io *io,
    struct block_device *data_dev, const char *normalized_key);

#endif
