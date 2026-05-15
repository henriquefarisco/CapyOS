#ifndef KERNEL_LINUX_COMPAT_LINUX_STAT_H
#define KERNEL_LINUX_COMPAT_LINUX_STAT_H

/* Linux ABI `stat`/`fstat`/`lstat` -- file metadata.
 *
 * `struct stat` on x86_64 Linux is a 144-byte structure (with
 * paddings) holding st_dev, st_ino, st_mode, st_size, mtime,
 * etc. Userland code that calls `fstat(fd, &sb)` typically only
 * reads two fields:
 *
 *   sb.st_mode  (to test S_ISREG / S_ISCHR / S_ISDIR)
 *   sb.st_size  (to mmap or pre-allocate a buffer)
 *
 * Marco M1 strategy: provide a minimal struct that satisfies
 * the most common consumers (musl FILE* setup, mmap-based
 * loaders) while the rich metadata layer (real inode numbers,
 * timestamps, link counts) waits for capyfs to mature.
 *
 * Defaults we report:
 *
 *   - fd 0/1/2 (stdin/out/err): S_IFCHR (character device).
 *     This makes musl's stdio init pick line-buffered for stderr
 *     and the right flags for stdin/stdout when redirected to
 *     pipes. (Combined with ioctl ENOTTY they pick block-buffered
 *     for non-tty redirection, line-buffered for tty.)
 *   - other fd >= 3: S_IFREG (regular file) with size = 0.
 *     Most musl callers tolerate size=0 as "unknown, fall back
 *     to read until EOF".
 *
 * Path-based variants (stat/lstat) recognise a conservative set of
 * Linux-compat pseudo paths (`/`, `/dev`, `/proc`, `/tmp`, `/dev/shm`
 * and the fixed files backed by devfs/procfs). Unknown paths still
 * return -ENOSYS so userland code can fall back to open()+fstat()
 * until a real namei walker lands. */

#include <stdint.h>
#include <stddef.h>

/* Linux x86_64 `struct stat` layout (from include/uapi/asm/stat.h
 * on x86_64). Sizes match: 144 bytes total. */
struct linux_stat {
    uint64_t st_dev;
    uint64_t st_ino;
    uint64_t st_nlink;
    uint32_t st_mode;
    uint32_t st_uid;
    uint32_t st_gid;
    uint32_t __pad0;
    uint64_t st_rdev;
    int64_t  st_size;
    int64_t  st_blksize;
    int64_t  st_blocks;
    int64_t  st_atime_sec;
    uint64_t st_atime_nsec;
    int64_t  st_mtime_sec;
    uint64_t st_mtime_nsec;
    int64_t  st_ctime_sec;
    uint64_t st_ctime_nsec;
    int64_t  _capyos_pad[3];
};

/* Linux file-type bits from <sys/stat.h>. We re-declare here
 * to keep this header freestanding-friendly. */
#define LINUX_S_IFMT   0170000u
#define LINUX_S_IFREG  0100000u  /* regular file */
#define LINUX_S_IFCHR  0020000u  /* character device */
#define LINUX_S_IFDIR  0040000u  /* directory */
#define LINUX_S_IFIFO  0010000u  /* fifo / pipe */
#define LINUX_S_IFLNK  0120000u  /* symbolic link */
#define LINUX_S_IRUSR  0000400u  /* user read */
#define LINUX_S_IWUSR  0000200u  /* user write */
#define LINUX_S_IXUSR  0000100u  /* user exec */

/* Default permission masks for the synthetic stats we report. */
#define LINUX_STAT_DEFAULT_PERMS \
    (LINUX_S_IRUSR | LINUX_S_IWUSR)
#define LINUX_STAT_DIR_PERMS \
    (LINUX_STAT_DEFAULT_PERMS | LINUX_S_IXUSR)

/* Returns 0 on success; -EBADF for fd < 0; -EFAULT for NULL out;
 * never fails otherwise (we synthesise plausible defaults). */
int64_t linux_fstat(int fd, struct linux_stat *out);

/* stat/lstat: path-based for known pseudo paths, -ENOSYS otherwise. */
int64_t linux_stat(const char *path, struct linux_stat *out);
int64_t linux_lstat(const char *path, struct linux_stat *out);
int linux_stat_path_is_known(const char *path);

void linux_stat_register_syscalls(void);

#endif /* KERNEL_LINUX_COMPAT_LINUX_STAT_H */
