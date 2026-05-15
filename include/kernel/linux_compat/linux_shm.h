#ifndef KERNEL_LINUX_COMPAT_LINUX_SHM_H
#define KERNEL_LINUX_COMPAT_LINUX_SHM_H

#include <stdint.h>
#include <stddef.h>

/* Linux-ABI POSIX shared memory shim (S2.10).
 *
 * NOT a syscall on Linux: glibc/musl `shm_open(name, oflag, mode)`
 * is implemented as `open("/dev/shm/" + name, oflag, mode)` and
 * `shm_unlink(name)` is `unlink("/dev/shm/" + name)`. The kernel
 * side is just a tmpfs mounted at /dev/shm.
 *
 * Marco M1 strategy: expose an in-kernel storage table for named
 * shm objects so that when the VFS lands and `/dev/shm/...` paths
 * route here, mmap/ftruncate/read/write/close all work.
 *
 * Used by:
 *   - Chromium IPC SharedMemoryPlatform_posix.cpp: creates a shm
 *     object, ftruncates to size, mmaps both ends.
 *   - Mozilla XPCOM IPC sandbox.
 *   - musl pthread mutex if process-shared (rare today).
 *
 * No syscall registration -- this is a kernel-internal API. The
 * eventual VFS routing layer will call into us. The API is
 * exercised here via host tests so the contract is locked before
 * the wiring lands.
 */

#define LINUX_SHM_MAX_OBJECTS    16
#define LINUX_SHM_MAX_NAME       63
#define LINUX_SHM_MAX_SIZE       (64ull * 1024 * 1024)  /* 64 MiB cap */

/* Linux POSIX file open flags subset for shm_open. */
#define LINUX_O_RDONLY  0x0u
#define LINUX_O_WRONLY  0x1u
#define LINUX_O_RDWR    0x2u
#define LINUX_O_CREAT   0x40u
#define LINUX_O_EXCL    0x80u
#define LINUX_O_TRUNC   0x200u

#define LINUX_SHM_OPEN_KNOWN_FLAGS \
    (LINUX_O_RDONLY | LINUX_O_WRONLY | LINUX_O_RDWR | \
     LINUX_O_CREAT | LINUX_O_EXCL | LINUX_O_TRUNC)

/* fd encoding: 0x9000 + slot. */
#define LINUX_SHM_FD_BASE 0x9000

void linux_shm_reset_for_tests(void);

/* Create or open a named shm object. Returns shm-fd >= 0, or
 * -LINUX_E*. Linux semantics:
 *   O_CREAT alone        : create if absent, open if present
 *   O_CREAT | O_EXCL     : create, fail with -EEXIST if present
 *   O_TRUNC              : truncate to 0 on open
 *   without O_CREAT      : -ENOENT if not present
 *   name == NULL or ""   : -EINVAL
 *   name length > MAX    : -ENAMETOOLONG
 *   table full           : -EMFILE
 */
int64_t linux_shm_open(const char *name, uint32_t oflag, uint32_t mode);

/* Remove the name from the table. Existing fds remain valid until
 * close (POSIX semantics). Returns 0 / -ENOENT. */
int64_t linux_shm_unlink(const char *name);

/* Resize the shm object backing the fd. Linux requires this
 * before mmap because anonymous /dev/shm files start at size 0.
 *
 *   size > LINUX_SHM_MAX_SIZE -> -LINUX_EFBIG
 *   shrinking truncates content
 *   growing zero-fills      */
int64_t linux_shm_truncate(int fd, uint64_t size);

/* Read current size of an shm object. */
int64_t linux_shm_size(int fd);

/* Close an shm fd. If the name was unlinked AND no other fd
 * holds it, the backing is freed. */
int64_t linux_shm_close(int fd);

/* Test-only observation: how many objects are in the name table. */
size_t linux_shm_test_named_count(void);

/* No syscall registration: shm_open is libc-only. */

#endif /* KERNEL_LINUX_COMPAT_LINUX_SHM_H */
