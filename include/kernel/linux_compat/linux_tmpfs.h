#ifndef KERNEL_LINUX_COMPAT_LINUX_TMPFS_H
#define KERNEL_LINUX_COMPAT_LINUX_TMPFS_H

#include <stdint.h>
#include <stddef.h>

/* In-memory filesystem for `/tmp/` paths (S2.9).
 *
 * Linux Firefox/musl/SpiderMonkey use `/tmp/` for scratch
 * files (mktemp-style temporaries, GIO crash dumps, profiling
 * traces, font caches). In a regular Linux box this is mounted
 * as `tmpfs` -- an in-memory filesystem managed by the kernel.
 *
 * For Marco M1 we provide a flat-namespace tmpfs:
 *   - Paths look like `/tmp/<name>` where `<name>` does not
 *     contain '/' (no subdirectories).
 *   - Fixed pool of 16 files; each file holds up to 4 KiB.
 *   - File content is allocated inline in the slot table
 *     (no separate page allocator) for simplicity.
 *
 * Scaling up later (more files, larger files, subdirectories)
 * happens when we land a real page allocator and a directory
 * tree. For now this is enough to back tempfile() and basic
 * userland that scribbles into `/tmp/`.
 *
 * POSIX semantics provided:
 *
 *   open(O_CREAT)        Allocate a slot; populate name; size 0.
 *   open(O_CREAT|O_EXCL) Fail with EEXIST if name already exists.
 *   open(O_TRUNC)        On existing file, reset size to 0.
 *   open(O_APPEND)       Future writes seek to end before write.
 *   open(no O_CREAT)     Fail with ENOENT if name not found.
 *   read                 Bytes from cursor; EOF at size.
 *   write                Bytes to cursor; auto-grows to a max.
 *                        Writes past LINUX_TMPFS_MAX_FILE_SIZE
 *                        return -ENOSPC (Linux behaviour for a
 *                        full filesystem).
 *   lseek SET/CUR/END    Standard semantics, may go past EOF.
 *   unlink               Remove name from table; existing fds
 *                        keep working until close (orphan).
 *   close                Decrement refcount. If unlinked AND
 *                        refcount==0, slot is freed.
 *
 * Module exposes the fd-based API used by the linux_vfs router
 * plus a free-form `linux_tmpfs_unlink(path)` that the future
 * `unlink(2)` syscall handler will call.
 */

/* fd encoding: 0xA000 (next free range after shm at 0x9000). */
#define LINUX_TMPFS_FD_BASE         0xA000

#define LINUX_TMPFS_MAX_FILES       16
#define LINUX_TMPFS_MAX_HANDLES     32
#define LINUX_TMPFS_MAX_NAME        128u
#define LINUX_TMPFS_MAX_FILE_SIZE   4096u

/* Public surface. */
int64_t linux_tmpfs_open (const char *path, uint32_t flags, uint32_t mode);
int64_t linux_tmpfs_close(int fd);
int64_t linux_tmpfs_read_fd (int fd, void *buf, size_t len);
int64_t linux_tmpfs_write_fd(int fd, const void *buf, size_t len);
int64_t linux_tmpfs_lseek_fd(int fd, int64_t offset, int whence);

/* Free-form unlink: removes the name from the table; existing
 * handles keep working (orphan) until close. Returns 0 or -errno. */
int64_t linux_tmpfs_unlink(const char *path);

void linux_tmpfs_reset_for_tests(void);

#endif /* KERNEL_LINUX_COMPAT_LINUX_TMPFS_H */
