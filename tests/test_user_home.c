#include <stdio.h>
#include <string.h>

#include "auth/session.h"
#include "auth/user_home.h"
#include "fs/vfs.h"

struct fake_entry {
  const char *path;
  int exists;
  struct inode inode;
  struct dentry dentry;
};

static struct fake_entry g_entries[] = {
    {.path = "/home"},
    {.path = "/home/admin"},
};

static struct session_context g_session_a;
static struct session_context g_session_b;
static struct session_context *g_active_session = &g_session_a;
static struct session_context *g_last_set_session = NULL;
static int g_create_calls = 0;
static int g_set_metadata_calls = 0;

static int expect_true(int cond, const char *msg) {
  if (!cond) {
    fprintf(stderr, "[user_home] %s\n", msg);
    return 1;
  }
  return 0;
}

static struct fake_entry *find_entry(const char *path) {
  size_t i = 0;
  if (!path) {
    return NULL;
  }
  for (i = 0; i < sizeof(g_entries) / sizeof(g_entries[0]); ++i) {
    if (strcmp(g_entries[i].path, path) == 0) {
      return &g_entries[i];
    }
  }
  return NULL;
}

static void reset_state(void) {
  size_t i = 0;
  g_active_session = &g_session_a;
  g_last_set_session = NULL;
  g_create_calls = 0;
  g_set_metadata_calls = 0;
  memset(&g_session_a, 0, sizeof(g_session_a));
  memset(&g_session_b, 0, sizeof(g_session_b));
  for (i = 0; i < sizeof(g_entries) / sizeof(g_entries[0]); ++i) {
    g_entries[i].exists = 0;
    memset(&g_entries[i].inode, 0, sizeof(g_entries[i].inode));
    memset(&g_entries[i].dentry, 0, sizeof(g_entries[i].dentry));
    g_entries[i].dentry.inode = &g_entries[i].inode;
  }
}

struct session_context *session_active(void) {
  return g_active_session;
}

void session_set_active(struct session_context *ctx) {
  g_active_session = ctx;
  g_last_set_session = ctx;
}

int vfs_lookup(const char *path, struct dentry **out) {
  struct fake_entry *entry = find_entry(path);
  if (!entry || !entry->exists || !out) {
    return -VFS_ERR_NOT_FOUND;
  }
  entry->dentry.refcount++;
  *out = &entry->dentry;
  return 0;
}

int vfs_create(const char *path, uint16_t mode, const struct vfs_metadata *meta) {
  struct fake_entry *entry = find_entry(path);
  if (!entry || !meta || (mode & VFS_MODE_DIR) == 0) {
    return -VFS_ERR_INVALID_ARGUMENT;
  }
  g_create_calls++;
  entry->exists = 1;
  entry->inode.mode = VFS_MODE_DIR;
  entry->inode.uid = meta->uid;
  entry->inode.gid = meta->gid;
  entry->inode.perm = meta->perm;
  entry->dentry.refcount = 1;
  return 0;
}

int vfs_set_metadata(const char *path, const struct vfs_metadata *meta) {
  struct fake_entry *entry = find_entry(path);
  if (!entry || !entry->exists || !meta) {
    return -VFS_ERR_NOT_FOUND;
  }
  g_set_metadata_calls++;
  entry->inode.uid = meta->uid;
  entry->inode.gid = meta->gid;
  entry->inode.perm = meta->perm;
  return 0;
}

int run_user_home_tests(void) {
  int fails = 0;
  struct fake_entry *home = NULL;
  struct fake_entry *admin = NULL;

  reset_state();
  fails += expect_true(user_home_prepare("/home/admin", 1000, 1000) == 0,
                       "should prepare a missing admin home");
  home = find_entry("/home");
  admin = find_entry("/home/admin");
  fails += expect_true(home && home->exists, "/home should be created");
  fails += expect_true(admin && admin->exists, "/home/admin should be created");
  fails += expect_true(home->inode.uid == 0 && home->inode.gid == 0,
                       "/home should stay owned by root");
  fails += expect_true(home->inode.perm == 0755,
                       "/home should keep directory permissions");
  fails += expect_true(admin->inode.uid == 1000 && admin->inode.gid == 1000,
                       "admin home should be owned by the provisioned user");
  fails += expect_true(admin->inode.perm == 0700,
                       "admin home should receive restrictive permissions");
  fails += expect_true(g_create_calls == 2,
                       "missing /home and /home/admin should both be created");
  fails += expect_true(g_set_metadata_calls == 2,
                       "created directories should be normalized with metadata");
  fails += expect_true(g_active_session == &g_session_a,
                       "active session should be restored after provisioning");

  reset_state();
  home = find_entry("/home");
  admin = find_entry("/home/admin");
  home->exists = 1;
  home->inode.mode = VFS_MODE_DIR;
  home->inode.uid = 0;
  home->inode.gid = 0;
  home->inode.perm = 0755;
  admin->exists = 1;
  admin->inode.mode = VFS_MODE_DIR;
  admin->inode.uid = 0;
  admin->inode.gid = 0;
  admin->inode.perm = 0755;
  g_active_session = &g_session_b;

  fails += expect_true(user_home_prepare("/home/admin", 1000, 1000) == 0,
                       "should repair ownership on an existing admin home");
  fails += expect_true(g_create_calls == 0,
                       "existing directories should not be recreated");
  fails += expect_true(g_set_metadata_calls == 2,
                       "existing directories should be normalized with metadata");
  fails += expect_true(admin->inode.uid == 1000 && admin->inode.gid == 1000,
                       "existing admin home should be re-owned");
  fails += expect_true(admin->inode.perm == 0700,
                       "existing admin home should be tightened to 0700");
  fails += expect_true(g_active_session == &g_session_b,
                       "session restore should preserve the previous active session");

  return fails;
}
