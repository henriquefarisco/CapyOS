#ifndef VFS_H
#define VFS_H

#include <stddef.h>
#include <stdint.h>

#include "fs/block.h"

#define VFS_NAME_MAX 32

#define VFS_MODE_FILE 0x1
#define VFS_MODE_DIR  0x2

#define VFS_PERM_READ  0x4
#define VFS_PERM_WRITE 0x2
#define VFS_PERM_EXEC  0x1

#define VFS_OPEN_READ   0x1
#define VFS_OPEN_WRITE  0x2

enum vfs_error {
    VFS_OK = 0,
    VFS_ERR_INVALID_ARGUMENT = 1,
    VFS_ERR_INVALID_PATH,
    VFS_ERR_NAME_TOO_LONG,
    VFS_ERR_NOT_FOUND,
    VFS_ERR_ALREADY_EXISTS,
    VFS_ERR_NOT_DIRECTORY,
    VFS_ERR_IS_DIRECTORY,
    VFS_ERR_PERMISSION_DENIED,
    VFS_ERR_DIR_NOT_EMPTY,
    VFS_ERR_UNSUPPORTED,
    VFS_ERR_NO_MEMORY,
    VFS_ERR_IO,
};

struct vfs_metadata {
    uint32_t uid;
    uint32_t gid;
    uint16_t perm;
};

struct vfs_stat {
    uint32_t ino;
    uint32_t size;
    uint32_t uid;
    uint32_t gid;
    uint16_t mode;
    uint16_t perm;
};

typedef int (*vfs_iter_cb)(const char *name, uint16_t mode, void *ctx);

struct inode;
struct dentry;
struct file;
struct super_block;

struct file_ops {
    int (*open)(struct inode *inode, struct file *file);
    int (*close)(struct file *file);
    int (*lookup)(struct inode *dir, const char *name, struct inode **out_inode);
    int (*create)(struct inode *dir, const char *name, uint16_t mode,
                  const struct vfs_metadata *meta, struct inode **out_inode);
    long (*read)(struct file *file, void *buffer, size_t size);
    long (*write)(struct file *file, const void *buffer, size_t size);
    int (*iterate)(struct inode *dir, vfs_iter_cb cb, void *ctx);
    int (*remove)(struct inode *dir, const char *name, int is_dir);
    int (*rename)(struct inode *src_dir, const char *src_name,
                  struct inode *dst_dir, const char *dst_name);
    int (*stat)(struct inode *inode, struct vfs_stat *out);
    int (*set_metadata)(struct inode *inode, const struct vfs_metadata *meta);
};

struct inode {
    struct super_block *sb;
    uint32_t ino;
    uint16_t mode;
    uint32_t size;
    uint32_t uid;
    uint32_t gid;
    uint16_t perm;
    const struct file_ops *ops;
    void *private_data;
};

struct dentry {
    char name[VFS_NAME_MAX];
    struct dentry *parent;
    struct dentry *first_child;
    struct dentry *next_sibling;
    struct inode *inode;
    uint32_t refcount;
};

struct file {
    struct dentry *dentry;
    uint32_t position;
    uint32_t flags;
};

struct super_block {
    struct block_device *bdev;
    struct dentry *root;
    void *fs_private;
};

int vfs_init(void);
int vfs_mount_root(struct super_block *sb);
struct super_block *vfs_root(void);

int vfs_lookup(const char *path, struct dentry **out);
int vfs_create(const char *path, uint16_t mode, const struct vfs_metadata *meta);
struct file *vfs_open(const char *path, uint32_t flags);
int vfs_close(struct file *file);
long vfs_read(struct file *file, void *buffer, size_t size);
long vfs_write(struct file *file, const void *buffer, size_t size);
int vfs_listdir(const char *path, vfs_iter_cb cb, void *ctx);
int vfs_unlink(const char *path);
int vfs_rmdir(const char *path);
int vfs_rename(const char *src_path, const char *dst_path);
int vfs_stat_path(const char *path, struct vfs_stat *out);
int vfs_set_metadata(const char *path, const struct vfs_metadata *meta);
int vfs_last_error(void);
const char *vfs_error_string(int error);

#endif
