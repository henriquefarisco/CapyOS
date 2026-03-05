#ifndef CAPYFS_H
#define CAPYFS_H

#include <stddef.h>
#include <stdint.h>

#include "fs/block.h"
#include "fs/vfs.h"

#define CAPYFS_MAGIC 0x4E524653u /* legacy on-disk 'NRFS' magic */
#define CAPYFS_VERSION 2
#define CAPYFS_BLOCK_SIZE 4096
#define CAPYFS_NAME_MAX 32

struct capy_super {
    uint32_t magic;
    uint32_t version;
    uint32_t block_size;
    uint32_t block_count;
    uint32_t inode_count;
    uint32_t bmap_start;
    uint32_t imap_start;
    uint32_t inode_start;
    uint32_t data_start;
} __attribute__((packed));

struct capy_inode_disk {
    uint16_t mode;
    uint16_t links;
    uint32_t size;
    uint32_t uid;
    uint32_t gid;
    uint16_t perm;
    uint16_t reserved;
    uint32_t direct[12];
    uint32_t indirect;
} __attribute__((packed));

struct capy_dirent_disk {
    uint32_t ino;
    char name[CAPYFS_NAME_MAX];
} __attribute__((packed));

typedef void (*capyfs_progress_cb)(const char *stage, uint32_t percent);

int mount_capyfs(struct block_device *dev, struct super_block *sb);
int capyfs_format(struct block_device *dev,
                  uint32_t inode_count,
                  uint32_t block_count,
                  capyfs_progress_cb progress_cb);

// Returns the CAPYFS file operations table.
const struct file_ops *capyfs_file_ops(void);

// Public VFS entry point for direct creation on the native filesystem.
int capyfs_create_pub(struct inode *dir,
                      const char *name,
                      uint16_t mode,
                      const struct vfs_metadata *meta,
                      struct inode **out_inode);

#endif
