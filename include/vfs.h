#ifndef VFS_H
#define VFS_H

#include <stddef.h>
#include <stdint.h>

#include "block.h"

#define VFS_NAME_MAX 32

#define VFS_MODE_FILE 0x1
#define VFS_MODE_DIR  0x2

struct inode;
struct dentry;
struct file;
struct super_block;

struct file_ops {
    int (*open)(struct inode *inode, struct file *file);
    int (*close)(struct file *file);
    int (*lookup)(struct inode *dir, const char *name, struct inode **out_inode);
    int (*create)(struct inode *dir, const char *name, uint16_t mode, struct inode **out_inode);
    long (*read)(struct file *file, void *buffer, size_t size);
    long (*write)(struct file *file, const void *buffer, size_t size);
};

struct inode {
    struct super_block *sb;
    uint32_t ino;
    uint16_t mode;
    uint32_t size;
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
int vfs_create(const char *path, uint16_t mode);
struct file *vfs_open(const char *path, uint32_t flags);
int vfs_close(struct file *file);
long vfs_read(struct file *file, void *buffer, size_t size);
long vfs_write(struct file *file, const void *buffer, size_t size);

#endif
