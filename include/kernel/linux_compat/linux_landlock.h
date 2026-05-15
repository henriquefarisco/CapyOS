#ifndef KERNEL_LINUX_COMPAT_LINUX_LANDLOCK_H
#define KERNEL_LINUX_COMPAT_LINUX_LANDLOCK_H

/* Linux ABI Landlock (hardened userland sandbox) syscalls.
 *
 *   int landlock_create_ruleset(const struct landlock_ruleset_attr *attr,
 *                                size_t size, uint32_t flags);
 *   int landlock_add_rule       (int ruleset_fd, int rule_type,
 *                                const void *rule_attr, uint32_t flags);
 *   int landlock_restrict_self  (int ruleset_fd, uint32_t flags);
 *
 * Why this matters for the Firefox port:
 *   - Modern Firefox content sandbox (Linux 5.13+) calls
 *     landlock_create_ruleset to install a per-task allow-list
 *     of filesystem paths and ports, then landlock_restrict_self
 *     to lock the renderer down. -ENOSYS makes Firefox fall
 *     back to the seccomp-only sandbox (functional but weaker).
 *   - bubblewrap (used by flatpak Firefox) probes for landlock
 *     support during init; -ENOSYS is gracefully handled but
 *     emits a warning.
 *
 * Linux semantics:
 *   - landlock_create_ruleset: returns a fd >= 0 representing
 *     the ruleset; size and flags must be valid.
 *   - landlock_add_rule: rule_type whitelist (PATH_BENEATH,
 *     NET_PORT); rule_attr layout depends on type.
 *   - landlock_restrict_self: locks the calling task into the
 *     ruleset; can only relax restrictions (subset of current).
 *
 * Marco M1 has no Landlock LSM but we accept structurally so
 * Firefox's "is Landlock available?" probe sees the right
 * ABI. We allocate ruleset fds from a small table; any later
 * add_rule / restrict_self call validates against a known
 * ruleset and accepts. */

#include <stdint.h>
#include <stddef.h>

#define LINUX_LANDLOCK_RULESET_FD_MAX  16
#define LINUX_LANDLOCK_FD_BASE         0xB000

/* Landlock access rights (uapi/linux/landlock.h). */
#define LINUX_LANDLOCK_ACCESS_FS_EXECUTE     (1ULL << 0)
#define LINUX_LANDLOCK_ACCESS_FS_WRITE_FILE  (1ULL << 1)
#define LINUX_LANDLOCK_ACCESS_FS_READ_FILE   (1ULL << 2)
#define LINUX_LANDLOCK_ACCESS_FS_READ_DIR    (1ULL << 3)
#define LINUX_LANDLOCK_ACCESS_FS_REMOVE_DIR  (1ULL << 4)
#define LINUX_LANDLOCK_ACCESS_FS_REMOVE_FILE (1ULL << 5)
#define LINUX_LANDLOCK_ACCESS_FS_MAKE_CHAR   (1ULL << 6)
#define LINUX_LANDLOCK_ACCESS_FS_MAKE_DIR    (1ULL << 7)
#define LINUX_LANDLOCK_ACCESS_FS_MAKE_REG    (1ULL << 8)
#define LINUX_LANDLOCK_ACCESS_FS_MAKE_SOCK   (1ULL << 9)
#define LINUX_LANDLOCK_ACCESS_FS_MAKE_FIFO   (1ULL << 10)
#define LINUX_LANDLOCK_ACCESS_FS_MAKE_BLOCK  (1ULL << 11)
#define LINUX_LANDLOCK_ACCESS_FS_MAKE_SYM    (1ULL << 12)
#define LINUX_LANDLOCK_ACCESS_FS_REFER       (1ULL << 13)
#define LINUX_LANDLOCK_ACCESS_FS_TRUNCATE    (1ULL << 14)
#define LINUX_LANDLOCK_ACCESS_FS_KNOWN \
    ((1ULL << 15) - 1)  /* bits 0..14 */

#define LINUX_LANDLOCK_ACCESS_NET_BIND_TCP    (1ULL << 0)
#define LINUX_LANDLOCK_ACCESS_NET_CONNECT_TCP (1ULL << 1)
#define LINUX_LANDLOCK_ACCESS_NET_KNOWN       0x3ULL

/* Rule types. */
#define LINUX_LANDLOCK_RULE_PATH_BENEATH  1
#define LINUX_LANDLOCK_RULE_NET_PORT      2

struct linux_landlock_ruleset_attr {
    uint64_t handled_access_fs;
    uint64_t handled_access_net;
    uint64_t scoped;     /* Linux 6.10+; we accept it */
};

#define LINUX_LANDLOCK_RULESET_ATTR_MIN_SIZE  16  /* fs + net */

#define LINUX_LANDLOCK_CREATE_RULESET_VERSION  (1u << 0)

int64_t linux_landlock_create_ruleset(
    const struct linux_landlock_ruleset_attr *attr,
    size_t size, uint32_t flags);
int64_t linux_landlock_add_rule(int ruleset_fd, int rule_type,
                                const void *rule_attr, uint32_t flags);
int64_t linux_landlock_restrict_self(int ruleset_fd, uint32_t flags);
int64_t linux_landlock_close(int fd);
int64_t linux_landlock_read(int fd, void *buf, size_t len);
int64_t linux_landlock_write(int fd, const void *buf, size_t len);
int64_t linux_landlock_lseek(int fd, int64_t offset, int whence);

void linux_landlock_register_syscalls(void);
void linux_landlock_reset_for_tests(void);

#endif /* KERNEL_LINUX_COMPAT_LINUX_LANDLOCK_H */
