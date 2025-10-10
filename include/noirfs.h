#ifndef NOIRFS_H
#define NOIRFS_H

#include <stddef.h>
#include <stdint.h>

#include "block.h"
#include "vfs.h"

#define NOIRFS_MAGIC 0x4E524653u /* 'NRFS' */
#define NOIRFS_VERSION 2
#define NOIRFS_BLOCK_SIZE 4096
#define NOIRFS_NAME_MAX 32

struct noir_super {
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

struct noir_inode_disk {
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

struct noir_dirent_disk {
    uint32_t ino;
    char name[NOIRFS_NAME_MAX];
} __attribute__((packed));

typedef void (*noirfs_progress_cb)(const char *stage, uint32_t percent);

int mount_noirfs(struct block_device *dev, struct super_block *sb);
int noirfs_format(struct block_device *dev,
                  uint32_t inode_count,
                  uint32_t block_count,
                  noirfs_progress_cb progress_cb);

#endif
