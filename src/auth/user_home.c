#include "auth/user_home.h"

#include <stddef.h>

#include "auth/session.h"
#include "auth/user.h"
#include "fs/vfs.h"

/* Standard folder structure provisioned inside every user's home, mirroring
 * the familiar desktop layout (Windows/Ubuntu-like). "Desktop" is the folder
 * the graphical desktop session renders; the others organize user content. */
static const char *const k_home_subdirs[] = {
    "Desktop", "Documents", "Personal", "Professional"};

/* Join `<base>/<name>` into `out` (bounded). Returns 0 on success, -1 if the
 * result would not fit. `base` is an absolute home path without trailing
 * slash (e.g. "/home/ana"); `name` is a single path component. */
static int home_join_child(char *out, size_t out_size, const char *base,
                           const char *name) {
  size_t j = 0;
  size_t i = 0;

  if (!out || out_size == 0 || !base || !name) {
    return -1;
  }
  for (i = 0; base[i] != '\0'; ++i) {
    if (j + 1 >= out_size) {
      return -1;
    }
    out[j++] = base[i];
  }
  if (j == 0 || out[j - 1] != '/') {
    if (j + 1 >= out_size) {
      return -1;
    }
    out[j++] = '/';
  }
  for (i = 0; name[i] != '\0'; ++i) {
    if (j + 1 >= out_size) {
      return -1;
    }
    out[j++] = name[i];
  }
  out[j] = '\0';
  return 0;
}

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
  } else {
    /* Provision the standard user folder structure inside the home. Each
     * folder inherits the home's owner and stays private (0700). Best
     * effort: a single failing folder marks rc but the rest are still
     * attempted so the structure is as complete as possible. */
    struct vfs_metadata sub_meta = {uid, gid, 0700};
    size_t k = 0;
    for (k = 0; k < sizeof(k_home_subdirs) / sizeof(k_home_subdirs[0]); ++k) {
      char child[USER_HOME_MAX + 16];
      if (home_join_child(child, sizeof(child), path, k_home_subdirs[k]) != 0 ||
          ensure_directory_with_metadata(child, &sub_meta) != 0) {
        rc = -1;
      }
    }
  }

  session_set_active(previous_session);
  return rc;
}
