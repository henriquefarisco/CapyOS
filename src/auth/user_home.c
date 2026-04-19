#include "auth/user_home.h"

#include "auth/session.h"
#include "fs/vfs.h"

static int ensure_directory_with_metadata(const char *path,
                                          const struct vfs_metadata *meta) {
  struct dentry *d = NULL;
  int lookup_rc = 0;

  if (!path || !meta) {
    return -1;
  }

  lookup_rc = vfs_lookup(path, &d);
  if (lookup_rc == 0 && d) {
    int is_dir = d->inode && (d->inode->mode & VFS_MODE_DIR);
    if (d->refcount) {
      d->refcount--;
    }
    if (!is_dir) {
      return -1;
    }
    return vfs_set_metadata(path, meta);
  }

  if (vfs_create(path, VFS_MODE_DIR, meta) != 0) {
    d = NULL;
    if (vfs_lookup(path, &d) != 0 || !d) {
      return -1;
    }
  }
  if (!d) {
    if (vfs_lookup(path, &d) != 0 || !d) {
      return -1;
    }
  }
  {
    int is_dir = d->inode && (d->inode->mode & VFS_MODE_DIR);
    if (d->refcount) {
      d->refcount--;
    }
    if (!is_dir) {
      return -1;
    }
  }
  return vfs_set_metadata(path, meta);
}

static int path_is_home_child(const char *path) {
  return path && path[0] == '/' && path[1] == 'h' && path[2] == 'o' &&
         path[3] == 'm' && path[4] == 'e' &&
         (path[5] == '/' || path[5] == '\0');
}

int user_home_prepare(const char *path, uint32_t uid, uint32_t gid) {
  struct session_context *previous_session = NULL;
  struct vfs_metadata root_meta = {0, 0, 0755};
  struct vfs_metadata home_meta = {uid, gid, 0700};
  int rc = 0;

  if (!path || path[0] != '/') {
    return -1;
  }

  previous_session = session_active();
  session_set_active(NULL);

  if (path_is_home_child(path) &&
      ensure_directory_with_metadata("/home", &root_meta) != 0) {
    rc = -1;
  } else if (ensure_directory_with_metadata(path, &home_meta) != 0) {
    rc = -1;
  }

  session_set_active(previous_session);
  return rc;
}
