#include "internal/capyfs_runtime_internal.h"

static struct file_ops capyfs_ops;
static int capyfs_ops_initialized = 0;

static void capyfs_init_ops(void) {
  if (capyfs_ops_initialized) return;
  capyfs_ops.open = capyfs_open;
  capyfs_ops.close = capyfs_close;
  capyfs_ops.lookup = capyfs_lookup;
  capyfs_ops.create = capyfs_create;
  capyfs_ops.read = capyfs_read;
  capyfs_ops.write = capyfs_write;
  capyfs_ops.iterate = capyfs_iterate;
  capyfs_ops.remove = capyfs_remove;
  capyfs_ops.rename = capyfs_rename_inode;
  capyfs_ops.stat = capyfs_stat_inode;
  capyfs_ops.set_metadata = capyfs_set_metadata;
  capyfs_ops_initialized = 1;
}

const struct file_ops *capyfs_file_ops(void) {
  capyfs_init_ops();
  return &capyfs_ops;
}
