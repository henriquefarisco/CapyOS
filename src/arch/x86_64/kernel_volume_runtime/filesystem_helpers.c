#include "internal/kernel_volume_runtime_internal.h"

int x64_kernel_volume_runtime_load_handoff_key(
    struct x64_kernel_volume_runtime_state *state) {
  uint8_t key_hash[SHA256_DIGEST_SIZE];
  if (!state || !state->handoff_volume_key || !state->handoff_volume_key_ready ||
      state->handoff_volume_key_size < 2) {
    return -1;
  }
  *state->handoff_volume_key_ready = 0;
  state->handoff_volume_key[0] = '\0';
  if (!state->handoff || state->handoff->version < 3) return -1;
  if ((state->handoff->boot_cfg_flags & BOOT_CONFIG_FLAG_HAS_VOLUME_KEY) == 0) return -1;
  if (normalize_volume_key_input(state->handoff->boot_volume_key,
                                 state->handoff_volume_key,
                                 state->handoff_volume_key_size) != 0) {
    return -1;
  }
  if (compute_volume_key_hash(state->handoff_volume_key, key_hash) == 0) {
    dbg_puts("[kvr] handoff key hash=");
    dbg_hex32(dbg_be32_local(key_hash));
    dbg_putc('\n');
    secure_memzero(key_hash, sizeof(key_hash));
  }
  *state->handoff_volume_key_ready = 1;
  return 0;
}

int x64_kernel_volume_runtime_ensure_dir_recursive(const char *path) {
  char build[128];
  size_t build_len = 1;
  const char *p = NULL;
  if (!path || path[0] != '/') return -1;
  build[0] = '/';
  build[1] = '\0';
  p = path;
  while (*p == '/') ++p;
  while (*p) {
    const char *start = p;
    size_t len = 0;
    while (start[len] && start[len] != '/') ++len;
    if (len > 0) {
      if (build_len > 1) {
        if (build_len + 1 >= sizeof(build)) return -1;
        build[build_len++] = '/';
      }
      if (build_len + len >= sizeof(build)) return -1;
      for (size_t i = 0; i < len; ++i) build[build_len++] = start[i];
      build[build_len] = '\0';
      struct dentry *d = NULL;
      if (vfs_lookup(build, &d) != 0) {
        if (vfs_create(build, VFS_MODE_DIR, NULL) != 0) {
          d = NULL;
          if (vfs_lookup(build, &d) != 0) return -1;
        }
      }
      if (d && d->refcount) d->refcount--;
    }
    p += len;
    while (*p == '/') ++p;
  }
  return 0;
}

int x64_kernel_volume_runtime_write_text_file(const char *path, const char *text) {
  struct dentry *d = NULL;
  if (!path || !text) return -1;
  (void)vfs_unlink(path);
  if (vfs_create(path, VFS_MODE_FILE, NULL) != 0) {
    if (vfs_lookup(path, &d) != 0) return -1;
    if (d && d->refcount) d->refcount--;
  }
  struct file *f = vfs_open(path, VFS_OPEN_WRITE);
  if (!f) return -1;
  size_t len = local_strlen(text);
  long written = vfs_write(f, text, len);
  vfs_close(f);
  return (written == (long)len) ? 0 : -1;
}

int x64_kernel_volume_runtime_append_text_file(const char *path, const char *text) {
  struct dentry *d = NULL;
  struct file *f = NULL;
  size_t len = 0;
  long written = 0;
  if (!path || !text) return -1;
  if (vfs_lookup(path, &d) != 0) {
    if (vfs_create(path, VFS_MODE_FILE, NULL) != 0) return -1;
  } else if (d && d->refcount) {
    d->refcount--;
  }
  f = vfs_open(path, VFS_OPEN_WRITE);
  if (!f) return -1;
  if (f->dentry && f->dentry->inode) f->position = f->dentry->inode->size;
  len = local_strlen(text);
  written = vfs_write(f, text, len);
  vfs_close(f);
  return (written == (long)len) ? 0 : -1;
}
