#include "internal/kernel_volume_runtime_internal.h"

#include "security/volume_provider.h"

static int fs_read_text_file(const char *path, char *out, size_t out_size) {
  if (!path || !out || out_size < 2) return -1;
  struct file *f = vfs_open(path, VFS_OPEN_READ);
  if (!f) return -1;
  long read = vfs_read(f, out, out_size - 1);
  vfs_close(f);
  if (read < 0) return -1;
  out[(size_t)read] = '\0';
  return 0;
}

int compute_volume_key_hash(const char *normalized_key,
                            uint8_t out_hash[SHA256_DIGEST_SIZE]) {
  struct sha256_ctx ctx;
  if (!normalized_key || !out_hash) return -1;
  sha256_init(&ctx);
  sha256_update(&ctx, (const uint8_t *)normalized_key, local_strlen(normalized_key));
  sha256_final(&ctx, out_hash);
  /* The volume password digest is the actual secret used to gate
   * decryption of the encrypted root volume. After `sha256_final`,
   * `ctx.state[]` mirrors `out_hash` and `ctx.data[]` carries the
   * padded last block derived from the user's normalized password.
   * Wiping the context before return prevents the freed stack frame
   * from retaining either the hash or material derived from the
   * password itself across to other kernel paths. */
  sha256_clear(&ctx);
  return 0;
}

int save_volume_key_hash(const char *normalized_key) {
  uint8_t digest[SHA256_DIGEST_SIZE];
  char hex[X64_VOLUME_KEY_HASH_HEX_LEN + 2];
  if (compute_volume_key_hash(normalized_key, digest) != 0) return -1;
  bytes_to_hex_local(digest, sizeof(digest), hex, sizeof(hex));
  hex[X64_VOLUME_KEY_HASH_HEX_LEN] = '\n';
  hex[X64_VOLUME_KEY_HASH_HEX_LEN + 1] = '\0';
  secure_memzero(digest, sizeof(digest));
  return x64_kernel_volume_runtime_write_text_file(X64_VOLUME_KEY_HASH_PATH, hex);
}

int load_volume_key_hash(uint8_t out_hash[SHA256_DIGEST_SIZE]) {
  char hex[X64_VOLUME_KEY_HASH_HEX_LEN + 8];
  size_t len = 0;
  if (!out_hash) return -1;
  if (fs_read_text_file(X64_VOLUME_KEY_HASH_PATH, hex, sizeof(hex)) != 0) return -1;
  while (hex[len] && hex[len] != '\n' && hex[len] != '\r') ++len;
  if (len != X64_VOLUME_KEY_HASH_HEX_LEN) return -1;
  return hex_to_bytes_local(hex, len, out_hash, SHA256_DIGEST_SIZE);
}

int append_key_candidate(const char **list, size_t *count, size_t cap,
                         const char *candidate) {
  if (!list || !count || !candidate || !candidate[0] || *count >= cap) return -1;
  for (size_t i = 0; i < *count; ++i) {
    if (streq(list[i], candidate)) return 0;
  }
  list[(*count)++] = candidate;
  return 0;
}

struct block_device *open_crypt_volume_with_password(
    const struct x64_kernel_volume_runtime_state *state,
    struct block_device *data_dev, const char *password) {
  struct block_device *crypt_dev = NULL;
  if (!state || !data_dev || !password || !password[0] || !state->disk_salt ||
      state->disk_salt_size == 0) {
    return NULL;
  }
  /*
   * alpha.222: try the on-disk volume header first; fall back to the
   * legacy PBKDF2 + g_disk_salt path when no header is present. The
   * provider wipes its scratch key material on both paths; we do not
   * see the derived keys here. The two-tier flow makes legacy volumes
   * (installed pre-alpha.222) keep mounting after the kernel upgrade
   * while every fresh install lands on the modern, per-volume-salt
   * Argon2id path. See `include/security/volume_provider.h` for the
   * detailed threat model.
   */
  if (volume_provider_open(data_dev, password, state->disk_salt,
                           state->disk_salt_size, state->kdf_iterations,
                           &crypt_dev) != 0) {
    return NULL;
  }
  if (!crypt_dev || crypt_dev == data_dev) {
    return NULL;
  }
  return crypt_dev;
}

static int probe_block_zeroed(struct block_device *dev, uint32_t lba, uint8_t *buf) {
  if (!dev || !buf || lba >= dev->block_count) return 1;
  if (block_device_read(dev, lba, buf) != 0) return 0;
  for (uint32_t i = 0; i < dev->block_size; ++i) {
    if (buf[i] != 0) return 0;
  }
  return 1;
}

static size_t build_blank_probe_samples(struct block_device *dev,
                                        uint32_t *samples, size_t cap) {
  size_t n = 0;
  if (!dev || !samples || cap == 0 || dev->block_count == 0) return 0;
  samples[n++] = 0;
  if (n < cap && dev->block_count > 1) samples[n++] = 1;
  if (n < cap && dev->block_count > 2) samples[n++] = 2;
  if (n < cap && dev->block_count > 128) samples[n++] = 127;
  if (n < cap && dev->block_count > 128) samples[n++] = 128;
  if (n < cap && dev->block_count > 129) samples[n++] = 129;
  if (n < cap && dev->block_count > 256) samples[n++] = 256;
  if (n < cap && dev->block_count > 4) samples[n++] = dev->block_count / 2;
  if (n < cap && dev->block_count > 0) samples[n++] = dev->block_count - 1;
  return n;
}

void reset_blank_probe_regions(const struct x64_kernel_volume_runtime_state *state,
                               struct block_device *dev) {
  uint32_t samples[9];
  size_t sample_count = 0;
  if (!state || !dev || !state->data_io_probe ||
      state->data_io_probe_size < dev->block_size) {
    return;
  }
  for (uint32_t i = 0; i < dev->block_size; ++i) state->data_io_probe[i] = 0;
  sample_count = build_blank_probe_samples(dev, samples,
                                           sizeof(samples) / sizeof(samples[0]));
  for (size_t i = 0; i < sample_count; ++i) {
    (void)block_device_write(dev, samples[i], state->data_io_probe);
  }
}

int device_is_blank(const struct x64_kernel_volume_runtime_state *state,
                    struct block_device *dev) {
  uint8_t *buf = NULL;
  uint32_t samples[9];
  size_t n = 0;
  if (!state || !dev || dev->block_count == 0 || dev->block_size == 0 ||
      !state->data_io_probe || state->data_io_probe_size < dev->block_size) {
    return 1;
  }
  buf = (uint8_t *)kalloc(dev->block_size);
  if (!buf) return 0;
  n = build_blank_probe_samples(dev, samples, sizeof(samples) / sizeof(samples[0]));
  for (size_t i = 0; i < n; ++i) {
    if (!probe_block_zeroed(dev, samples[i], buf)) {
      kfree(buf);
      return 0;
    }
  }
  kfree(buf);
  return 1;
}
