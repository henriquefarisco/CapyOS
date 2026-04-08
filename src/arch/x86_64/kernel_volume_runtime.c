#include "arch/x86_64/kernel_volume_runtime.h"

#include <stddef.h>
#include <stdint.h>

#include "arch/x86_64/storage_runtime.h"
#include "boot/boot_config.h"
#include "drivers/storage/efi_block.h"
#include "fs/buffer.h"
#include "fs/capyfs.h"
#include "memory/kmem.h"
#include "security/crypt.h"

#define X64_VOLUME_KEY_HASH_HEX_LEN 64
#define X64_VOLUME_KEY_HASH_PATH "/system/volume.key.sha256"

static size_t local_strlen(const char *s) {
  size_t len = 0;
  if (!s) {
    return 0;
  }
  while (s[len]) {
    ++len;
  }
  return len;
}

static void local_copy(char *dst, size_t dst_size, const char *src) {
  if (!dst || dst_size == 0) {
    return;
  }
  size_t i = 0;
  if (src) {
    while (src[i] && i + 1 < dst_size) {
      dst[i] = src[i];
      ++i;
    }
  }
  dst[i] = '\0';
}

static int streq(const char *a, const char *b) {
  size_t i = 0;
  if (!a || !b) {
    return 0;
  }
  while (a[i] && b[i]) {
    if (a[i] != b[i]) {
      return 0;
    }
    ++i;
  }
  return a[i] == '\0' && b[i] == '\0';
}

static void secure_memzero(void *ptr, size_t len) {
  volatile uint8_t *p = (volatile uint8_t *)ptr;
  while (len--) {
    *p++ = 0;
  }
}

static void io_print(const struct x64_kernel_volume_runtime_io *io,
                     const char *message) {
  if (io && io->print && message) {
    io->print(message);
  }
}

static void io_print_hex(const struct x64_kernel_volume_runtime_io *io,
                         uint64_t value) {
  if (io && io->print_hex) {
    io->print_hex(value);
  }
}

static void io_print_hex64(const struct x64_kernel_volume_runtime_io *io,
                           uint64_t value) {
  if (io && io->print_hex64) {
    io->print_hex64(value);
  }
}

static void io_print_dec_u32(const struct x64_kernel_volume_runtime_io *io,
                             uint32_t value) {
  if (io && io->print_dec_u32) {
    io->print_dec_u32(value);
  }
}

static void io_putc(const struct x64_kernel_volume_runtime_io *io, char ch) {
  if (io && io->putc) {
    io->putc(ch);
  }
}

static size_t io_readline(const struct x64_kernel_volume_runtime_io *io,
                          char *buf, size_t maxlen, int mask) {
  if (!io || !io->readline) {
    return 0;
  }
  return io->readline(buf, maxlen, mask);
}

static void io_print_active_efi_runtime_trace(
    const struct x64_kernel_volume_runtime_io *io) {
  if (io && io->print_active_efi_runtime_trace) {
    io->print_active_efi_runtime_trace();
  }
}

static int ascii_is_alnum(char c) {
  if (c >= '0' && c <= '9') {
    return 1;
  }
  if (c >= 'a' && c <= 'z') {
    return 1;
  }
  if (c >= 'A' && c <= 'Z') {
    return 1;
  }
  return 0;
}

static char ascii_upper(char c) {
  if (c >= 'a' && c <= 'z') {
    return (char)(c - 'a' + 'A');
  }
  return c;
}

static int normalize_volume_key_input(const char *input, char *out,
                                      size_t out_size) {
  if (!input || !out || out_size < 2) {
    return -1;
  }

  size_t n = 0;
  for (size_t i = 0; input[i]; ++i) {
    char c = input[i];
    if (c == '-' || c == ' ' || c == '\t' || c == '\r' || c == '\n') {
      continue;
    }
    if (!ascii_is_alnum(c) || n + 1 >= out_size) {
      return -1;
    }
    out[n++] = ascii_upper(c);
  }

  if (n < 4) {
    return -1;
  }
  out[n] = '\0';
  return 0;
}

static void format_volume_key_groups(const char *normalized, char *out,
                                     size_t out_size) {
  size_t si = 0;
  size_t di = 0;

  if (!normalized || !out || out_size == 0) {
    return;
  }

  while (normalized[si] && di + 1 < out_size) {
    if (si > 0 && (si % 4) == 0) {
      if (di + 1 >= out_size) {
        break;
      }
      out[di++] = '-';
    }
    out[di++] = normalized[si++];
  }
  out[di] = '\0';
}

static void bytes_to_hex_local(const uint8_t *src, size_t len, char *dst,
                               size_t dst_size) {
  static const char hex[] = "0123456789abcdef";
  size_t di = 0;

  if (!src || !dst || dst_size == 0) {
    return;
  }

  for (size_t i = 0; i < len && (di + 2) < dst_size; ++i) {
    dst[di++] = hex[(src[i] >> 4) & 0x0F];
    dst[di++] = hex[src[i] & 0x0F];
  }
  dst[di] = '\0';
}

static int hex_value_local(char c) {
  if (c >= '0' && c <= '9') {
    return c - '0';
  }
  if (c >= 'a' && c <= 'f') {
    return 10 + (c - 'a');
  }
  if (c >= 'A' && c <= 'F') {
    return 10 + (c - 'A');
  }
  return -1;
}

static int hex_to_bytes_local(const char *hex, size_t hex_len, uint8_t *dst,
                              size_t dst_len) {
  if (!hex || !dst || hex_len != dst_len * 2) {
    return -1;
  }
  for (size_t i = 0; i < dst_len; ++i) {
    int hi = hex_value_local(hex[i * 2]);
    int lo = hex_value_local(hex[i * 2 + 1]);
    if (hi < 0 || lo < 0) {
      return -1;
    }
    dst[i] = (uint8_t)((hi << 4) | lo);
  }
  return 0;
}

static int fs_read_text_file(const char *path, char *out, size_t out_size) {
  if (!path || !out || out_size < 2) {
    return -1;
  }
  struct file *f = vfs_open(path, VFS_OPEN_READ);
  if (!f) {
    return -1;
  }
  long read = vfs_read(f, out, out_size - 1);
  vfs_close(f);
  if (read < 0) {
    return -1;
  }
  out[(size_t)read] = '\0';
  return 0;
}

static int compute_volume_key_hash(const char *normalized_key,
                                   uint8_t out_hash[SHA256_DIGEST_SIZE]) {
  struct sha256_ctx ctx;

  if (!normalized_key || !out_hash) {
    return -1;
  }

  sha256_init(&ctx);
  sha256_update(&ctx, (const uint8_t *)normalized_key,
                local_strlen(normalized_key));
  sha256_final(&ctx, out_hash);
  return 0;
}

static int save_volume_key_hash(const char *normalized_key) {
  uint8_t digest[SHA256_DIGEST_SIZE];
  char hex[X64_VOLUME_KEY_HASH_HEX_LEN + 2];

  if (compute_volume_key_hash(normalized_key, digest) != 0) {
    return -1;
  }

  bytes_to_hex_local(digest, sizeof(digest), hex, sizeof(hex));
  hex[X64_VOLUME_KEY_HASH_HEX_LEN] = '\n';
  hex[X64_VOLUME_KEY_HASH_HEX_LEN + 1] = '\0';
  secure_memzero(digest, sizeof(digest));
  return x64_kernel_volume_runtime_write_text_file(X64_VOLUME_KEY_HASH_PATH,
                                                   hex);
}

static int load_volume_key_hash(uint8_t out_hash[SHA256_DIGEST_SIZE]) {
  char hex[X64_VOLUME_KEY_HASH_HEX_LEN + 8];
  size_t len = 0;

  if (!out_hash) {
    return -1;
  }
  if (fs_read_text_file(X64_VOLUME_KEY_HASH_PATH, hex, sizeof(hex)) != 0) {
    return -1;
  }
  while (hex[len] && hex[len] != '\n' && hex[len] != '\r') {
    ++len;
  }
  if (len != X64_VOLUME_KEY_HASH_HEX_LEN) {
    return -1;
  }
  return hex_to_bytes_local(hex, len, out_hash, SHA256_DIGEST_SIZE);
}

static int append_key_candidate(const char **list, size_t *count, size_t cap,
                                const char *candidate) {
  if (!list || !count || !candidate || !candidate[0] || *count >= cap) {
    return -1;
  }
  for (size_t i = 0; i < *count; ++i) {
    if (streq(list[i], candidate)) {
      return 0;
    }
  }
  list[(*count)++] = candidate;
  return 0;
}

static struct block_device *open_crypt_volume_with_password(
    const struct x64_kernel_volume_runtime_state *state,
    struct block_device *data_dev, const char *password) {
  uint8_t key1[CRYPT_KEY_SIZE];
  uint8_t key2[CRYPT_KEY_SIZE];
  struct block_device *crypt_dev = NULL;

  if (!state || !data_dev || !password || !password[0] || !state->disk_salt ||
      state->disk_salt_size == 0) {
    return NULL;
  }

  secure_memzero(key1, sizeof(key1));
  secure_memzero(key2, sizeof(key2));
  crypt_derive_xts_keys(password, state->disk_salt, state->disk_salt_size,
                        state->kdf_iterations, key1, key2);
  crypt_dev = crypt_init(data_dev, key1, key2);
  secure_memzero(key1, sizeof(key1));
  secure_memzero(key2, sizeof(key2));
  if (!crypt_dev || crypt_dev == data_dev) {
    return NULL;
  }
  return crypt_dev;
}

static int probe_block_zeroed(struct block_device *dev, uint32_t lba,
                              uint8_t *buf) {
  if (!dev || !buf || lba >= dev->block_count) {
    return 1;
  }
  if (block_device_read(dev, lba, buf) != 0) {
    return 0;
  }
  for (uint32_t i = 0; i < dev->block_size; ++i) {
    if (buf[i] != 0) {
      return 0;
    }
  }
  return 1;
}

static int device_is_blank(const struct x64_kernel_volume_runtime_state *state,
                           struct block_device *dev) {
  uint8_t *buf = NULL;
  uint32_t samples[9];
  size_t n = 0;

  if (!state || !dev || dev->block_count == 0 || dev->block_size == 0 ||
      !state->data_io_probe || state->data_io_probe_size < dev->block_size) {
    return 1;
  }

  buf = (uint8_t *)kalloc(dev->block_size);
  if (!buf) {
    return 0;
  }

  samples[n++] = 0;
  if (dev->block_count > 1) {
    samples[n++] = 1;
  }
  if (dev->block_count > 2) {
    samples[n++] = 2;
  }
  if (dev->block_count > 128) {
    samples[n++] = 127;
    samples[n++] = 128;
    samples[n++] = 129;
  }
  if (dev->block_count > 256) {
    samples[n++] = 256;
  }
  if (dev->block_count > 4) {
    samples[n++] = dev->block_count / 2;
  }
  if (dev->block_count > 0) {
    samples[n++] = dev->block_count - 1;
  }

  for (size_t i = 0; i < n; ++i) {
    if (!probe_block_zeroed(dev, samples[i], buf)) {
      kfree(buf);
      return 0;
    }
  }

  kfree(buf);
  return 1;
}

static int mount_root_capyfs(struct x64_kernel_volume_runtime_state *state,
                             const struct x64_kernel_volume_runtime_io *io,
                             struct block_device *dev, const char *label) {
  int mount_rc = 0;
  int root_rc = -1;

  if (!state || !state->root_sb) {
    return -1;
  }

  mount_rc = mount_capyfs(dev, state->root_sb);
  root_rc = (mount_rc == 0) ? vfs_mount_root(state->root_sb) : -1;
  if (mount_rc != 0 || root_rc != 0) {
    io_print(io, "[fs] ERRO: falha ao montar CAPYFS em ");
    io_print(io, label ? label : "dispositivo");
    io_print(io, ". mount=");
    io_print_hex(io, (uint64_t)(uint32_t)mount_rc);
    io_print(io, " root=");
    io_print_hex(io, (uint64_t)(uint32_t)root_rc);
    io_putc(io, '\n');
    return -1;
  }
  return 0;
}

int x64_kernel_volume_runtime_mount_root_capyfs(
    struct x64_kernel_volume_runtime_state *state,
    const struct x64_kernel_volume_runtime_io *io, struct block_device *dev,
    const char *label) {
  return mount_root_capyfs(state, io, dev, label);
}

static int initialize_encrypted_data_volume(
    struct x64_kernel_volume_runtime_state *state,
    const struct x64_kernel_volume_runtime_io *io,
    struct block_device *data_dev, const char *normalized_key) {
  struct block_device *crypt_dev = NULL;

  if (!state || !data_dev || !normalized_key || !normalized_key[0] ||
      !state->active_volume_key || !state->active_volume_key_ready) {
    return -1;
  }

  local_copy(state->active_volume_key, state->active_volume_key_size,
             normalized_key);
  *state->active_volume_key_ready = 1;

  crypt_dev =
      open_crypt_volume_with_password(state, data_dev, state->active_volume_key);
  if (!crypt_dev) {
    io_print(io, "[fs] ERRO: falha ao iniciar camada criptografica.\n");
    return -1;
  }

  int fmt_rc = capyfs_format(crypt_dev, 128, crypt_dev->block_count, NULL);
  if (fmt_rc != 0) {
    io_print(io, "[fs] ERRO: falha ao formatar volume cifrado. rc=");
    io_print_hex(io, (uint64_t)(uint32_t)fmt_rc);
    uint32_t sync_block = 0;
    if (buffer_cache_last_error_block(&sync_block) == 0) {
      io_print(io, " sync_block=");
      io_print_dec_u32(io, sync_block);
      io_print(io, " sync_code=");
      io_print_dec_u32(io,
                       (uint32_t)(buffer_cache_last_error_code() &
                                  0xFFFFFFFF));
    }
    io_print_active_efi_runtime_trace(io);
    io_putc(io, '\n');
    crypt_free(crypt_dev);
    return -1;
  }

  if (state->data_io_probe && state->data_io_probe_size >= data_dev->block_size &&
      block_device_read(data_dev, 0, state->data_io_probe) == 0) {
    uint32_t nonzero = 0;
    for (uint32_t i = 0; i < data_dev->block_size; ++i) {
      if (state->data_io_probe[i] != 0) {
        ++nonzero;
      }
    }
    io_print(io, "[fs] Probe RAW apos formatacao (LBA0) bytes!=0: ");
    io_print_dec_u32(io, nonzero);
    io_putc(io, '\n');
  } else {
    io_print(io, "[fs] Aviso: probe RAW apos formatacao falhou.\n");
  }

  if (mount_root_capyfs(state, io, crypt_dev, "DATA cifrada") != 0) {
    crypt_free(crypt_dev);
    return -1;
  }
  io_print(io, "[fs] Volume cifrado inicializado e montado.\n");
  return 0;
}

int x64_kernel_volume_runtime_load_handoff_key(
    struct x64_kernel_volume_runtime_state *state) {
  if (!state || !state->handoff_volume_key || !state->handoff_volume_key_ready ||
      state->handoff_volume_key_size < 2) {
    return -1;
  }

  *state->handoff_volume_key_ready = 0;
  state->handoff_volume_key[0] = '\0';
  if (!state->handoff || state->handoff->version < 3) {
    return -1;
  }
  if ((state->handoff->boot_cfg_flags & BOOT_CONFIG_FLAG_HAS_VOLUME_KEY) == 0) {
    return -1;
  }
  if (normalize_volume_key_input(state->handoff->boot_volume_key,
                                 state->handoff_volume_key,
                                 state->handoff_volume_key_size) != 0) {
    return -1;
  }
  *state->handoff_volume_key_ready = 1;
  return 0;
}

int x64_kernel_volume_runtime_ensure_dir_recursive(const char *path) {
  char build[128];
  size_t build_len = 1;
  const char *p = NULL;

  if (!path || path[0] != '/') {
    return -1;
  }

  build[0] = '/';
  build[1] = '\0';
  p = path;
  while (*p == '/') {
    ++p;
  }

  while (*p) {
    const char *start = p;
    size_t len = 0;
    while (start[len] && start[len] != '/') {
      ++len;
    }
    if (len > 0) {
      if (build_len > 1) {
        if (build_len + 1 >= sizeof(build)) {
          return -1;
        }
        build[build_len++] = '/';
      }
      if (build_len + len >= sizeof(build)) {
        return -1;
      }
      for (size_t i = 0; i < len; ++i) {
        build[build_len++] = start[i];
      }
      build[build_len] = '\0';
      struct dentry *d = NULL;
      if (vfs_lookup(build, &d) != 0) {
        if (vfs_create(build, VFS_MODE_DIR, NULL) != 0) {
          return -1;
        }
      } else if (d && d->refcount) {
        d->refcount--;
      }
    }
    p += len;
    while (*p == '/') {
      ++p;
    }
  }
  return 0;
}

int x64_kernel_volume_runtime_write_text_file(const char *path,
                                              const char *text) {
  if (!path || !text) {
    return -1;
  }
  (void)vfs_unlink(path);
  if (vfs_create(path, VFS_MODE_FILE, NULL) != 0) {
    return -1;
  }
  struct file *f = vfs_open(path, VFS_OPEN_WRITE);
  if (!f) {
    return -1;
  }
  size_t len = local_strlen(text);
  long written = vfs_write(f, text, len);
  vfs_close(f);
  return (written == (long)len) ? 0 : -1;
}

int x64_kernel_volume_runtime_append_text_file(const char *path,
                                               const char *text) {
  struct dentry *d = NULL;
  struct file *f = NULL;
  size_t len = 0;
  long written = 0;

  if (!path || !text) {
    return -1;
  }

  if (vfs_lookup(path, &d) != 0) {
    if (vfs_create(path, VFS_MODE_FILE, NULL) != 0) {
      return -1;
    }
  } else if (d && d->refcount) {
    d->refcount--;
  }

  f = vfs_open(path, VFS_OPEN_WRITE);
  if (!f) {
    return -1;
  }
  if (f->dentry && f->dentry->inode) {
    f->position = f->dentry->inode->size;
  }
  len = local_strlen(text);
  written = vfs_write(f, text, len);
  vfs_close(f);
  return (written == (long)len) ? 0 : -1;
}

int x64_kernel_volume_runtime_persist_active_key_hash(
    const struct x64_kernel_volume_runtime_state *state) {
  uint8_t existing[SHA256_DIGEST_SIZE];

  if (!state || !state->shell_persistent_storage ||
      !state->active_volume_key_ready || !state->active_volume_key ||
      !*state->shell_persistent_storage || !*state->active_volume_key_ready) {
    return 0;
  }

  if (x64_kernel_volume_runtime_ensure_dir_recursive("/system") != 0) {
    return -1;
  }
  if (load_volume_key_hash(existing) == 0) {
    secure_memzero(existing, sizeof(existing));
    return 0;
  }
  return save_volume_key_hash(state->active_volume_key);
}

int x64_kernel_volume_runtime_mount_encrypted_data_volume(
    struct x64_kernel_volume_runtime_state *state,
    const struct x64_kernel_volume_runtime_io *io,
    struct block_device *data_dev) {
  char raw_key[128];
  char normalized_key[X64_KERNEL_VOLUME_KEY_MAX];
  char grouped_key[X64_KERNEL_VOLUME_KEY_MAX + 16];

  if (!state || !data_dev || !state->active_volume_key ||
      !state->active_volume_key_ready || !state->handoff_volume_key ||
      !state->handoff_volume_key_ready || !state->data_io_probe ||
      state->data_io_probe_size < data_dev->block_size) {
    return -1;
  }

  io_print(io, "[fs] Probing leitura inicial da particao DATA...\n");
  if (block_device_read(data_dev, 0, state->data_io_probe) != 0) {
    io_print(io, "[fs] ERRO: falha de I/O ao ler DATA (backend ");
    io_print(io, x64_storage_runtime_backend_name());
    io_print(io, "/");
    io_print(io, x64_storage_runtime_data_path());
    io_print(io, ").\n");
    if (x64_storage_runtime_uses_firmware()) {
      io_print(io, "[fs] EFI ReadBlocks status=");
      {
        const struct efi_block_device *efi_disk =
            x64_storage_runtime_active_efi();
        io_print_hex64(io, efi_disk ? efi_disk->ctx.last_status : 0);
        io_print(io, " code=");
        io_print_dec_u32(
            io, efi_disk ? (uint32_t)(efi_disk->ctx.last_status & 0xFFFFFFFFULL)
                         : 0);
        io_print(io, " lba=");
        io_print_dec_u32(io, efi_disk ? efi_disk->ctx.last_block_no : 0);
        io_print(io, " media=");
        io_print_dec_u32(io, efi_disk ? efi_disk->ctx.last_media_id : 0);
      }
      io_putc(io, '\n');
    }
    io_print(
        io,
        "[fs] A chave pode estar correta; acesso ao disco falhou antes da criptografia.\n");
    return -1;
  }
  io_print(io, "[fs] Probe de leitura DATA concluido.\n");
  (void)x64_kernel_volume_runtime_load_handoff_key(state);

  if (device_is_blank(state, data_dev)) {
    secure_memzero((uint8_t *)normalized_key, sizeof(normalized_key));
    io_print(
        io,
        "[fs] Particao DATA vazia detectada. Inicializando volume cifrado.\n");
    if (*state->handoff_volume_key_ready) {
      local_copy(normalized_key, sizeof(normalized_key),
                 state->handoff_volume_key);
      io_print(io, "[fs] Chave de volume provisionada pela instalacao ISO.\n");
    } else {
      io_print(
          io,
          "[fs] ERRO: nenhuma chave provisionada encontrada no boot.\n");
      io_print(
          io,
          "[fs] Boot normal nao deve gerar/trocar chave de volume.\n");
      io_print(
          io,
          "[fs] Reinicie pela ISO para provisionar a chave e recriar o volume com seguranca.\n");
      return -1;
    }
    int init_rc =
        initialize_encrypted_data_volume(state, io, data_dev, normalized_key);
    secure_memzero((uint8_t *)normalized_key, sizeof(normalized_key));
    return init_rc;
  }

  secure_memzero(raw_key, sizeof(raw_key));
  secure_memzero((uint8_t *)normalized_key, sizeof(normalized_key));
  secure_memzero((uint8_t *)grouped_key, sizeof(grouped_key));

  if (*state->handoff_volume_key_ready) {
    struct block_device *crypt_dev = open_crypt_volume_with_password(
        state, data_dev, state->handoff_volume_key);
    if (crypt_dev && mount_root_capyfs(state, io, crypt_dev, "DATA cifrada") ==
                         0) {
      local_copy(state->active_volume_key, state->active_volume_key_size,
                 state->handoff_volume_key);
      *state->active_volume_key_ready = 1;
      io_print(io, "[fs] Volume cifrado montado automaticamente.\n");
      return 0;
    }
    if (crypt_dev) {
      crypt_free(crypt_dev);
    }
    io_print(
        io,
        "[fs] Aviso: chave provisionada falhou para desbloquear o volume existente.\n");
    io_print(
        io,
        "[security] Validacao estrita por hash da chave foi desativada nesta fase.\n");
    io_print(io,
             "[fs] Seguindo para modo manual de chave (somente desbloqueio).\n");
  }

  io_print(io, "[fs] Volume cifrado detectado. Informe a senha para montar.\n");
  for (int attempt = 0; attempt < 3; ++attempt) {
    io_print(io, "Chave do volume: ");
    size_t len = io_readline(io, raw_key, sizeof(raw_key), 0);
    if (len == 0) {
      io_print(io, "Chave vazia. Tente novamente.\n");
      continue;
    }

    int normalized_ok =
        (normalize_volume_key_input(raw_key, normalized_key,
                                    sizeof(normalized_key)) == 0);
    if (normalized_ok) {
      format_volume_key_groups(normalized_key, grouped_key, sizeof(grouped_key));
    }

    const char *candidates[4];
    size_t candidate_count = 0;
    (void)append_key_candidate(candidates, &candidate_count,
                               sizeof(candidates) / sizeof(candidates[0]),
                               raw_key);
    if (normalized_ok) {
      (void)append_key_candidate(candidates, &candidate_count,
                                 sizeof(candidates) / sizeof(candidates[0]),
                                 normalized_key);
      (void)append_key_candidate(candidates, &candidate_count,
                                 sizeof(candidates) / sizeof(candidates[0]),
                                 grouped_key);
    }

    int mounted = 0;
    for (size_t i = 0; i < candidate_count && !mounted; ++i) {
      struct block_device *crypt_dev =
          open_crypt_volume_with_password(state, data_dev, candidates[i]);
      if (!crypt_dev) {
        continue;
      }
      if (mount_root_capyfs(state, io, crypt_dev, "DATA cifrada") == 0) {
        mounted = 1;
        break;
      }
      crypt_free(crypt_dev);
    }

    if (mounted) {
      if (normalized_ok) {
        local_copy(state->active_volume_key, state->active_volume_key_size,
                   normalized_key);
      } else {
        local_copy(state->active_volume_key, state->active_volume_key_size,
                   raw_key);
      }
      *state->active_volume_key_ready = 1;
      secure_memzero(raw_key, sizeof(raw_key));
      secure_memzero((uint8_t *)normalized_key, sizeof(normalized_key));
      secure_memzero((uint8_t *)grouped_key, sizeof(grouped_key));
      io_print(io, "[fs] Volume cifrado montado com sucesso.\n");
      return 0;
    }

    secure_memzero(raw_key, sizeof(raw_key));
    secure_memzero((uint8_t *)normalized_key, sizeof(normalized_key));
    secure_memzero((uint8_t *)grouped_key, sizeof(grouped_key));
    io_print(io, "Chave incorreta ou volume invalido. ");
    io_print(io, "Formato aceito: letras/numeros; hifens opcionais.\n");
    if (attempt < 2) {
      io_print(io, "[fs] Tentativas restantes para desbloqueio: ");
      io_print_dec_u32(io, (uint32_t)(2 - attempt));
      io_putc(io, '\n');
    }
  }

  io_print(io, "[fs] Falha ao desbloquear o volume DATA apos 3 tentativas.\n");
  return -1;
}
