#include "kernel/log/klog_persist.h"

#include "arch/x86_64/kernel_volume_runtime.h"
#include "kernel/log/klog.h"
#include "fs/buffer.h"
#include "fs/vfs.h"

static int append_klog_text(const char *path, const char *text) {
  return x64_kernel_volume_runtime_append_text_file(path, text);
}

int klog_persist_flush_default(void) {
  struct super_block *root = NULL;
  int rc = 0;

  if (x64_kernel_volume_runtime_ensure_dir_recursive("/var/log") != 0) {
    return -1;
  }
  rc = klog_flush(append_klog_text);
  if (rc != 0) {
    return rc;
  }
  root = vfs_root();
  if (root && root->bdev) {
    if (buffer_cache_sync(root->bdev) != 0) {
      return -1;
    }
  }
  return 0;
}
