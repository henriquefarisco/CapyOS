#ifndef KERNEL_LINUX_COMPAT_LINUX_STATX_H
#define KERNEL_LINUX_COMPAT_LINUX_STATX_H

/* Linux ABI `statx(2)` -- modern stat with per-field selection.
 *
 *   int statx(int dirfd, const char *pathname, int flags,
 *             unsigned int mask, struct statx *buf);
 *
 * Modern musl (>= 1.2) and glibc both call statx in some paths,
 * falling back to fstatat/fstat when the kernel returns -ENOSYS.
 * Implementing statx directly avoids the fallback path and keeps
 * userland on the fast track.
 *
 * The struct is 256 bytes -- much larger than struct stat -- with
 * extra timestamp resolution, btime, attribute masks, etc. We
 * synthesise the same minimal subset we report from `linux_fstat`:
 *
 *   stx_mode      -- S_IFCHR for stdio, S_IFREG otherwise
 *   stx_size      -- 0 (unknown)
 *   stx_blksize   -- 4096
 *   stx_nlink     -- 1
 *   stx_attributes_mask -- 0 (we don't expose any extra bits)
 *
 * `mask` (the bitmap of fields userland wants) is honoured: we
 * always return the synthesised values for whatever the user
 * asked for. `stx_mask` (the bitmap of fields we ACTUALLY filled)
 * is set to the intersection of `mask` and our supported fields.
 *
 * Modes of operation:
 *
 *   - dirfd >= 0 with empty path AND flags & AT_EMPTY_PATH
 *     -> equivalent to fstat(dirfd) -- this is how musl issues
 *        fstat in some paths.
 *   - dirfd == AT_FDCWD with non-empty path -> path-based stat
 *     for the same known pseudo paths supported by linux_stat();
 *     AT_SYMLINK_NOFOLLOW switches projection to linux_lstat();
 *     unknown paths return -ENOSYS until a real namei walker lands.
 *   - other dirfd values -> -ENOTDIR. */

#include <stdint.h>
#include <stddef.h>

#define LINUX_STATX_AT_FDCWD       (-100)
#define LINUX_STATX_AT_SYMLINK_NOFOLLOW 0x100
#define LINUX_STATX_AT_EMPTY_PATH  0x1000

/* Subset of statx mask bits userland passes to express "I want
 * these fields". Linux defines many more (BTIME, MNT_ID, etc.)
 * but we ignore unknown bits in the mask. */
#define LINUX_STATX_TYPE      0x00000001u
#define LINUX_STATX_MODE      0x00000002u
#define LINUX_STATX_NLINK     0x00000004u
#define LINUX_STATX_UID       0x00000008u
#define LINUX_STATX_GID       0x00000010u
#define LINUX_STATX_ATIME     0x00000020u
#define LINUX_STATX_MTIME     0x00000040u
#define LINUX_STATX_CTIME     0x00000080u
#define LINUX_STATX_INO       0x00000100u
#define LINUX_STATX_SIZE      0x00000200u
#define LINUX_STATX_BLOCKS    0x00000400u
#define LINUX_STATX_BASIC_STATS \
    (LINUX_STATX_TYPE | LINUX_STATX_MODE | LINUX_STATX_NLINK | \
     LINUX_STATX_UID  | LINUX_STATX_GID  | LINUX_STATX_ATIME | \
     LINUX_STATX_MTIME | LINUX_STATX_CTIME | LINUX_STATX_INO | \
     LINUX_STATX_SIZE | LINUX_STATX_BLOCKS)

/* Subset of fields we actually fill. Mode + size + nlink + blksize
 * is what userland reads in 99% of cases. */
#define LINUX_STATX_SUPPORTED \
    (LINUX_STATX_TYPE | LINUX_STATX_MODE | LINUX_STATX_NLINK | \
     LINUX_STATX_SIZE)

/* `struct statx_timestamp` from Linux. */
struct linux_statx_timestamp {
    int64_t  tv_sec;
    uint32_t tv_nsec;
    int32_t  __reserved;
};

/* `struct statx` -- 256 bytes on Linux x86_64. */
struct linux_statx {
    uint32_t stx_mask;
    uint32_t stx_blksize;
    uint64_t stx_attributes;
    uint32_t stx_nlink;
    uint32_t stx_uid;
    uint32_t stx_gid;
    uint16_t stx_mode;
    uint16_t __pad1;
    uint64_t stx_ino;
    uint64_t stx_size;
    uint64_t stx_blocks;
    uint64_t stx_attributes_mask;

    struct linux_statx_timestamp stx_atime;
    struct linux_statx_timestamp stx_btime;
    struct linux_statx_timestamp stx_ctime;
    struct linux_statx_timestamp stx_mtime;

    uint32_t stx_rdev_major;
    uint32_t stx_rdev_minor;
    uint32_t stx_dev_major;
    uint32_t stx_dev_minor;

    uint64_t stx_mnt_id;
    uint32_t stx_dio_mem_align;
    uint32_t stx_dio_offset_align;

    uint64_t __spare3[12];
};

int64_t linux_statx(int dirfd, const char *pathname, int flags,
                    uint32_t mask, struct linux_statx *buf);

void linux_statx_register_syscalls(void);

#endif /* KERNEL_LINUX_COMPAT_LINUX_STATX_H */
