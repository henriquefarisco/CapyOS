#include "../internal/first_boot_internal.h"

#if defined(__x86_64__)
#include "arch/x86_64/kernel_volume_runtime.h"
#endif

/* Reports whether `src` holds at least one non-zero byte without revealing
 * any byte value. Constant time over `len`. Useful for boot diagnostics that
 * must confirm a credential field is populated without leaking material. */
static int bytes_have_nonzero(const uint8_t *src, size_t len) {
  uint8_t acc = 0;
  if (!src) {
    return 0;
  }
  for (size_t i = 0; i < len; ++i) {
    acc |= src[i];
  }
  return acc != 0 ? 1 : 0;
}

int config_ensure_directory(const char *path) {
  char build[128];
  size_t build_len = 0;
  const char *p = NULL;
  struct dentry *chk = NULL;

  if (!path || path[0] != '/') {
    return -1;
  }
#if defined(__x86_64__)
  if (x64_kernel_volume_runtime_ensure_dir_recursive(path) == 0) {
    return 0;
  }
#endif

  build[0] = '/';
  build[1] = '\0';
  build_len = 1;
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

      chk = NULL;
      if (vfs_lookup(build, &chk) != 0) {
        if (vfs_create(build, VFS_MODE_DIR, NULL) != 0) {
          chk = NULL;
          if (vfs_lookup(build, &chk) != 0) {
            return -1;
          }
        }
      }
      if (chk && chk->refcount) {
        chk->refcount--;
      }
    }
    p += len;
    while (*p == '/') {
      ++p;
    }
  }
  return 0;
}

int config_write_text_file(const char *path, const char *text) {
  if (!path || !text) {
    return -1;
  }
  struct dentry *d = NULL;
  if (vfs_lookup(path, &d) != 0) {
    if (vfs_create(path, VFS_MODE_FILE, NULL) != 0) {
      if (vfs_lookup(path, &d) != 0) {
        return -1;
      }
    }
  }
  if (d && d->refcount) {
    d->refcount--;
  }
  struct file *f = vfs_open(path, VFS_OPEN_WRITE);
  if (!f) {
    return -1;
  }
  if (f->dentry && f->dentry->inode) {
    f->position = 0;
  }
  size_t len = cstring_length(text);
  long written = vfs_write(f, text, len);
  vfs_close(f);
  return (written == (long)len) ? 0 : -1;
}

void config_build_home_path(const char *username, char *out, size_t out_len) {
  cstring_copy(out, out_len, "/home/");
  size_t base_len = cstring_length(out);
  size_t uname_len = cstring_length(username);
  if (base_len + uname_len >= out_len) {
    return;
  }
  for (size_t i = 0; i < uname_len; ++i) {
    out[base_len + i] = username[i];
  }
  out[base_len + uname_len] = '\0';
}

static int valid_username_char(char c) {
  if (c >= 'a' && c <= 'z') return 1;
  if (c >= 'A' && c <= 'Z') return 1;
  if (c >= '0' && c <= '9') return 1;
  return c == '-' || c == '_';
}

int config_validate_admin_username(const char *username) {
  if (!username || !username[0]) return 0;
  size_t len = cstring_length(username);
  if (len == 0 || len >= USER_NAME_MAX) return 0;
  for (size_t i = 0; i < len; ++i) {
    if (!valid_username_char(username[i])) return 0;
  }
  return 1;
}

const char *config_validate_theme(const char *input) {
  if (!input || cstring_length(input) == 0) return "capyos";
  if (strings_equal(input, "capyos") || strings_equal(input, "CAPYOS") ||
      strings_equal(input, "ocean") || strings_equal(input, "forest")) {
    return strings_equal(input, "CAPYOS") ? "capyos" : input;
  }
  return "capyos";
}

int config_verify_directory_exists(const char *path) {
  if (!path) return -1;
  struct vfs_stat st;
  if (vfs_stat_path(path, &st) != 0) {
    vga_write("Falha ao verificar diretorio: ");
    vga_write(path);
    vga_newline();
    return -1;
  }
  if ((st.mode & VFS_MODE_DIR) == 0) {
    vga_write("Item nao eh diretorio: ");
    vga_write(path);
    vga_newline();
    return -1;
  }
  return 0;
}

void config_log_user_record_state(const struct user_record *rec) {
  if (!rec) return;
  /* Boot diagnostics must confirm the administrator record is populated
   * without exposing salt or PBKDF2 hash material. Logging the raw bytes
   * (even in hex) on the boot log would let anyone with read access to the
   * log mount an offline brute-force attack against the credential, so we
   * only report presence and the expected sizes here. The constants are
   * compile-time public ABI from `auth/user.h`. */
  char uid_buf[12], gid_buf[12];
  char salt_size_buf[12], hash_size_buf[12];
  int salt_present = bytes_have_nonzero(rec->salt, USER_SALT_SIZE);
  int hash_present = bytes_have_nonzero(rec->hash, USER_HASH_SIZE);
  config_u32_to_string(rec->uid, uid_buf, sizeof(uid_buf));
  config_u32_to_string(rec->gid, gid_buf, sizeof(gid_buf));
  config_u32_to_string((uint32_t)USER_SALT_SIZE, salt_size_buf,
                       sizeof(salt_size_buf));
  config_u32_to_string((uint32_t)USER_HASH_SIZE, hash_size_buf,
                       sizeof(hash_size_buf));
  config_log_buffer_append("   userdb: usuario=");
  config_log_buffer_append(rec->username);
  config_log_buffer_append(" uid=");
  config_log_buffer_append(uid_buf);
  config_log_buffer_append(" gid=");
  config_log_buffer_append(gid_buf);
  config_log_buffer_append(" home=");
  config_log_buffer_append(rec->home);
  config_log_buffer_append(" salt=[redacted size=");
  config_log_buffer_append(salt_size_buf);
  config_log_buffer_append(salt_present ? " present=1]" : " present=0]");
  config_log_buffer_append(" hash=[redacted size=");
  config_log_buffer_append(hash_size_buf);
  config_log_buffer_append(hash_present ? " present=1]" : " present=0]");
  config_log_buffer_append("\n");
  config_log_flush_pending();
}

