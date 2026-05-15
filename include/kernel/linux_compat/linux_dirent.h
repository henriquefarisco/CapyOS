#ifndef KERNEL_LINUX_COMPAT_LINUX_DIRENT_H
#define KERNEL_LINUX_COMPAT_LINUX_DIRENT_H

/* Linux ABI `getdents64(2)` -- read directory entries.
 *
 *   ssize_t getdents64(int fd, void *dirp, size_t count);
 *
 * Userland that calls `opendir` + `readdir` ultimately issues
 * getdents64 on the directory fd. CapyOS doesn't have a directory
 * fd table yet (only file fds via devfs/procfs/tmpfs/shm), so we
 * have nothing concrete to enumerate.
 *
 * Marco M1 strategy: return 0 (EOF) for any positive fd. This
 * makes musl's readdir() return NULL on the first call, which
 * userland correctly interprets as "empty directory". Code that
 * actually needs entries (eg ls) gets an empty listing rather
 * than a hard ENOSYS, which is the kindest degradation.
 *
 * When real directory fds land, this stub gets replaced with a
 * dispatch keyed on fd encoding range (devfs/procfs/tmpfs each
 * supply their own getdents iterator).
 *
 * Failure modes:
 *   - fd < 0 -> -EBADF
 *   - count == 0 -> 0 (Linux: zero buffer is valid, just no
 *     entries returned)
 *   - dirp == NULL with count > 0 -> -EFAULT */

#include <stdint.h>
#include <stddef.h>

/* `struct linux_dirent64` -- variable-length record. The Linux
 * layout has a fixed 19-byte header followed by a NUL-terminated
 * filename, padded so each record's d_reclen is a multiple of
 * 8 bytes. Userland walks the buffer using d_reclen to advance.
 *
 * We define the type for completeness, but the Marco M1 stub
 * never emits any records. */
struct linux_dirent64 {
    uint64_t d_ino;
    int64_t  d_off;
    uint16_t d_reclen;
    uint8_t  d_type;
    char     d_name[];
};

#define LINUX_DT_UNKNOWN  0
#define LINUX_DT_FIFO     1
#define LINUX_DT_CHR      2
#define LINUX_DT_DIR      4
#define LINUX_DT_BLK      6
#define LINUX_DT_REG      8
#define LINUX_DT_LNK      10
#define LINUX_DT_SOCK     12

int64_t linux_getdents64(int fd, void *dirp, size_t count);

void linux_dirent_register_syscalls(void);

#endif /* KERNEL_LINUX_COMPAT_LINUX_DIRENT_H */
