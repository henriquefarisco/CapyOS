#include "auth/user_prefs.h"

#include "lang/localization.h"
#include "fs/buffer.h"
#include "fs/vfs.h"
#include "memory/kmem.h"

#define USER_PREFS_DIR_NAME ".config"
#define USER_PREFS_APP_DIR_NAME "capyos"
#define USER_PREFS_FILE_NAME "user.ini"
#define USER_PREFS_PATH_MAX 160

static size_t cstring_length(const char *s) {
  size_t len = 0;
  if (!s) {
    return 0;
  }
  while (s[len]) {
    ++len;
  }
  return len;
}

static void cstring_copy(char *dst, size_t dst_size, const char *src) {
  size_t i = 0;
  if (!dst || dst_size == 0) {
    return;
  }
  if (src) {
    while (src[i] && i < dst_size - 1) {
      dst[i] = src[i];
      ++i;
    }
  }
  dst[i] = '\0';
}

static int strings_equal(const char *a, const char *b) {
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
  return a[i] == b[i];
}

static void buffer_append(char *dst, size_t dst_size, const char *src) {
  size_t pos = cstring_length(dst);
  size_t idx = 0;
  if (!dst || dst_size == 0 || !src) {
    return;
  }
  while (src[idx] && pos + 1 < dst_size) {
    dst[pos++] = src[idx++];
  }
  dst[pos] = '\0';
}

void user_preferences_set_defaults(struct user_preferences *prefs) {
  if (!prefs) {
    return;
  }
  cstring_copy(prefs->language, sizeof(prefs->language), "pt-BR");
}

const char *user_preferences_language(const struct user_preferences *prefs) {
  const char *normalized =
      localization_normalize_language(prefs ? prefs->language : NULL);
  return normalized ? normalized : "pt-BR";
}

static int build_user_config_dir(const struct user_record *user, char *out,
                                 size_t out_len) {
  if (!user || !user->home[0] || !out || out_len == 0) {
    return -1;
  }
  out[0] = '\0';
  buffer_append(out, out_len, user->home);
  buffer_append(out, out_len, "/");
  buffer_append(out, out_len, USER_PREFS_DIR_NAME);
  return 0;
}

static int build_user_app_config_dir(const struct user_record *user, char *out,
                                     size_t out_len) {
  if (build_user_config_dir(user, out, out_len) != 0) {
    return -1;
  }
  buffer_append(out, out_len, "/");
  buffer_append(out, out_len, USER_PREFS_APP_DIR_NAME);
  return 0;
}

static int build_user_prefs_path(const struct user_record *user, char *out,
                                 size_t out_len) {
  if (build_user_app_config_dir(user, out, out_len) != 0) {
    return -1;
  }
  buffer_append(out, out_len, "/");
  buffer_append(out, out_len, USER_PREFS_FILE_NAME);
  return 0;
}

static int ensure_directory(const char *path, const struct vfs_metadata *meta) {
  struct dentry *d = NULL;
  if (vfs_lookup(path, &d) == 0 && d) {
    int ok = d->inode && (d->inode->mode & VFS_MODE_DIR);
    if (d->refcount) {
      d->refcount--;
    }
    if (ok && meta) {
      (void)vfs_set_metadata(path, meta);
    }
    return ok ? 0 : -1;
  }
  if (vfs_create(path, VFS_MODE_DIR, meta) != 0) {
    return -1;
  }
  if (meta) {
    (void)vfs_set_metadata(path, meta);
  }
  return 0;
}

static int write_text_file(const char *path, const char *text,
                           const struct vfs_metadata *meta) {
  struct dentry *d = NULL;
  if (!path || !text) {
    return -1;
  }
  if (vfs_lookup(path, &d) != 0) {
    if (vfs_create(path, VFS_MODE_FILE, meta) != 0) {
      return -1;
    }
  } else if (d && d->refcount) {
    d->refcount--;
  }
  if (meta) {
    (void)vfs_set_metadata(path, meta);
  }
  {
    struct file *f = vfs_open(path, VFS_OPEN_WRITE);
    size_t len = cstring_length(text);
    long written = 0;
    if (!f) {
      return -1;
    }
    if (f->dentry && f->dentry->inode) {
      f->position = 0;
    }
    written = vfs_write(f, text, len);
    vfs_close(f);
    return (written == (long)len) ? 0 : -1;
  }
}

static void sync_root_device(void) {
  struct super_block *root = vfs_root();
  if (root && root->bdev) {
    (void)buffer_cache_sync(root->bdev);
  }
}

static int apply_pref_line(struct user_preferences *prefs, const char *line,
                           size_t len) {
  size_t eq = 0;
  char key[24];
  char value[32];
  if (!prefs || !line) {
    return 0;
  }
  while (eq < len && line[eq] != '=') {
    ++eq;
  }
  if (eq == 0 || eq >= len) {
    return 0;
  }
  if (eq >= sizeof(key)) {
    eq = sizeof(key) - 1;
  }
  for (size_t i = 0; i < eq; ++i) {
    key[i] = line[i];
  }
  key[eq] = '\0';
  len -= (eq + 1);
  if (len >= sizeof(value)) {
    len = sizeof(value) - 1;
  }
  for (size_t i = 0; i < len; ++i) {
    value[i] = line[eq + 1 + i];
  }
  value[len] = '\0';

  if (strings_equal(key, "language")) {
    const char *normalized = localization_normalize_language(value);
    if (normalized) {
      cstring_copy(prefs->language, sizeof(prefs->language), normalized);
      return 1;
    }
  }
  return 0;
}

int user_prefs_load(const struct user_record *user, struct user_preferences *out) {
  char path[USER_PREFS_PATH_MAX];
  struct file *f = NULL;
  size_t size = 0;
  char *buffer = NULL;
  long read = 0;
  int applied_any = 0;

  if (!user || !out) {
    return -1;
  }

  user_preferences_set_defaults(out);
  if (build_user_prefs_path(user, path, sizeof(path)) != 0) {
    return -1;
  }

  f = vfs_open(path, VFS_OPEN_READ);
  if (!f) {
    return 0;
  }
  if (f->dentry && f->dentry->inode) {
    size = f->dentry->inode->size;
  }
  buffer = (char *)kalloc(size + 1);
  if (!buffer) {
    vfs_close(f);
    return -1;
  }
  read = vfs_read(f, buffer, size);
  vfs_close(f);
  if (read < 0) {
    kfree(buffer);
    return -1;
  }
  buffer[read] = '\0';

  {
    size_t start = 0;
    for (size_t i = 0; i <= (size_t)read; ++i) {
      if (i == (size_t)read || buffer[i] == '\n') {
        size_t segment_len = i - start;
        if (segment_len > 0) {
          if (apply_pref_line(out, &buffer[start], segment_len) != 0) {
            applied_any = 1;
          }
        }
        start = i + 1;
      }
    }
  }

  kfree(buffer);
  return applied_any ? 1 : 0;
}

int user_prefs_save(const struct user_record *user,
                    const struct user_preferences *prefs) {
  char config_dir[USER_PREFS_PATH_MAX];
  char app_dir[USER_PREFS_PATH_MAX];
  char path[USER_PREFS_PATH_MAX];
  char buffer[64];
  struct vfs_metadata dir_meta;
  struct vfs_metadata file_meta;
  const char *language = NULL;

  if (!user || !prefs) {
    return -1;
  }

  language = user_preferences_language(prefs);
  if (build_user_config_dir(user, config_dir, sizeof(config_dir)) != 0 ||
      build_user_app_config_dir(user, app_dir, sizeof(app_dir)) != 0 ||
      build_user_prefs_path(user, path, sizeof(path)) != 0) {
    return -1;
  }

  dir_meta.uid = user->uid;
  dir_meta.gid = user->gid;
  dir_meta.perm = 0700;
  file_meta.uid = user->uid;
  file_meta.gid = user->gid;
  file_meta.perm = 0600;

  if (ensure_directory(config_dir, &dir_meta) != 0 ||
      ensure_directory(app_dir, &dir_meta) != 0) {
    return -1;
  }

  buffer[0] = '\0';
  buffer_append(buffer, sizeof(buffer), "language=");
  buffer_append(buffer, sizeof(buffer), language);
  buffer_append(buffer, sizeof(buffer), "\n");

  if (write_text_file(path, buffer, &file_meta) != 0) {
    return -1;
  }
  sync_root_device();
  return 0;
}

int user_prefs_save_language(const struct user_record *user,
                             const char *language) {
  struct user_preferences prefs;
  const char *normalized = localization_normalize_language(language);
  if (!user || !normalized) {
    return -1;
  }
  user_preferences_set_defaults(&prefs);
  (void)user_prefs_load(user, &prefs);
  cstring_copy(prefs.language, sizeof(prefs.language), normalized);
  return user_prefs_save(user, &prefs);
}
