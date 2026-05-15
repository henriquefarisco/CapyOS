#ifndef KERNEL_LINUX_COMPAT_LINUX_STATFS_H
#define KERNEL_LINUX_COMPAT_LINUX_STATFS_H

/* Linux ABI filesystem-statistics syscalls.
 *
 *   int statfs (const char *path, struct statfs *buf);
 *   int fstatfs(int fd,            struct statfs *buf);
 *
 * Why this matters for the Firefox port:
 *   - Firefox's "free space check" before downloading or extracting
 *     attachments queries statvfs() (which musl maps onto statfs).
 *     -ENOSYS makes Firefox refuse to start downloads.
 *   - SQLite's `journal_mode = WAL` path checks statfs to detect
 *     space pressure before writing checkpoints; on -ENOSYS it
 *     falls back to "rollback" mode which is much slower.
 *   - GIO and gvfs probe statfs to label volumes.
 *
 * Marco M1 has only tmpfs (RAM-backed). We synthesise a 64-byte
 * Linux-shaped `struct statfs` reporting "small but non-zero"
 * counts so the size check succeeds:
 *   - f_type   = TMPFS_MAGIC (0x01021994)
 *   - f_bsize  = 4096
 *   - f_blocks = total system RAM / 4096 (provider-injected; the
 *                default uses a 64 MiB approximation)
 *   - f_bfree  = same as f_blocks (we don't track usage)
 *   - f_bavail = same
 *   - f_files  = 1024 (rough cap for Marco M1 tmpfs handle table)
 *   - f_ffree  = 1024
 *   - f_fsid   = {0, 0}
 *   - f_namelen = 255
 *   - f_frsize = 4096
 *   - f_flags  = 0
 *   - f_spare  = {0, 0, 0, 0}
 *
 * When a real backing store lands, the provider returns honest
 * counts. */

#include <stdint.h>
#include <stddef.h>

#define LINUX_STATFS_TMPFS_MAGIC     0x01021994u
#define LINUX_STATFS_DEFAULT_BSIZE   4096u
#define LINUX_STATFS_DEFAULT_NAMELEN 255u

/* Linux x86_64 `struct statfs` is 120 bytes:
 *   __kernel_long_t f_type, f_bsize;
 *   __kernel_fsblkcnt_t f_blocks, f_bfree, f_bavail;
 *   __kernel_fsfilcnt_t f_files, f_ffree;
 *   __kernel_fsid_t f_fsid;     // 8 bytes
 *   __kernel_long_t f_namelen, f_frsize, f_flags;
 *   __kernel_long_t f_spare[4];
 * x86_64 has __kernel_long_t = 8 bytes. Total = 11 * 8 + 8 + 8
 * = 104 + 8 + 8 = 120 bytes. */
struct linux_statfs {
    int64_t  f_type;
    int64_t  f_bsize;
    uint64_t f_blocks;
    uint64_t f_bfree;
    uint64_t f_bavail;
    uint64_t f_files;
    uint64_t f_ffree;
    uint32_t f_fsid[2];
    int64_t  f_namelen;
    int64_t  f_frsize;
    int64_t  f_flags;
    int64_t  f_spare[4];
};

struct linux_statfs_providers {
    /* Returns total filesystem block count (in f_bsize units).
     * NULL = caller falls back to a 64 MiB / 4 KiB = 16384
     * default. */
    uint64_t (*total_blocks)(void);
    /* Returns total inode count (Marco M1 default 1024). */
    uint64_t (*total_files)(void);
};

void linux_statfs_install_providers(const struct linux_statfs_providers *p);
void linux_statfs_reset_for_tests(void);

int64_t linux_statfs (const char *path, struct linux_statfs *buf);
int64_t linux_fstatfs(int fd,            struct linux_statfs *buf);

void linux_statfs_register_syscalls(void);

#endif /* KERNEL_LINUX_COMPAT_LINUX_STATFS_H */
