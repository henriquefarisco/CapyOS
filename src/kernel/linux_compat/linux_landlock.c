#include "kernel/linux_compat/linux_landlock.h"
#include "kernel/linux_compat/linux_syscall.h"
#include "kernel/linux_compat/linux_syscall_nrs.h"
#include "kernel/linux_compat/linux_errno.h"

#include <stdint.h>
#include <stddef.h>

#define LANDLOCK_ABI_VERSION  4  /* Linux 6.10 ABI level */

static uint8_t g_ruleset_in_use[LINUX_LANDLOCK_RULESET_FD_MAX];
static int     g_initialised;

static void ensure_init(void) {
    if (g_initialised) return;
    for (int i = 0; i < LINUX_LANDLOCK_RULESET_FD_MAX; i++) {
        g_ruleset_in_use[i] = 0;
    }
    g_initialised = 1;
}

void linux_landlock_reset_for_tests(void) {
    g_initialised = 0;
    ensure_init();
}

static int valid_ruleset_fd(int fd) {
    if (fd < LINUX_LANDLOCK_FD_BASE) return 0;
    int idx = fd - LINUX_LANDLOCK_FD_BASE;
    if (idx >= LINUX_LANDLOCK_RULESET_FD_MAX) return 0;
    return g_ruleset_in_use[idx];
}

static int ruleset_slot(int fd) {
    ensure_init();
    if (fd < LINUX_LANDLOCK_FD_BASE) return -1;
    int idx = fd - LINUX_LANDLOCK_FD_BASE;
    if (idx < 0 || idx >= LINUX_LANDLOCK_RULESET_FD_MAX) return -1;
    if (!g_ruleset_in_use[idx]) return -1;
    return idx;
}

int64_t linux_landlock_create_ruleset(
    const struct linux_landlock_ruleset_attr *attr,
    size_t size, uint32_t flags) {
    ensure_init();
    /* Linux: when LANDLOCK_CREATE_RULESET_VERSION is in flags,
     * `attr` and `size` must be NULL/0; the syscall returns
     * the ABI version. */
    if (flags & LINUX_LANDLOCK_CREATE_RULESET_VERSION) {
        if (attr || size != 0) return -LINUX_EINVAL;
        if (flags & ~LINUX_LANDLOCK_CREATE_RULESET_VERSION) {
            return -LINUX_EINVAL;
        }
        return LANDLOCK_ABI_VERSION;
    }
    if (flags != 0) return -LINUX_EINVAL;
    if (!attr) return -LINUX_EFAULT;
    if (size < LINUX_LANDLOCK_RULESET_ATTR_MIN_SIZE) {
        return -LINUX_EINVAL;
    }
    if (attr->handled_access_fs & ~LINUX_LANDLOCK_ACCESS_FS_KNOWN) {
        return -LINUX_EINVAL;
    }
    if (attr->handled_access_net & ~LINUX_LANDLOCK_ACCESS_NET_KNOWN) {
        return -LINUX_EINVAL;
    }
    /* Linux: at least one access right must be requested. */
    if (attr->handled_access_fs == 0 &&
        attr->handled_access_net == 0) {
        return -LINUX_ENOMSG;
    }
    for (int i = 0; i < LINUX_LANDLOCK_RULESET_FD_MAX; i++) {
        if (!g_ruleset_in_use[i]) {
            g_ruleset_in_use[i] = 1;
            return LINUX_LANDLOCK_FD_BASE + i;
        }
    }
    return -LINUX_ENFILE;
}

int64_t linux_landlock_add_rule(int ruleset_fd, int rule_type,
                                const void *rule_attr, uint32_t flags) {
    ensure_init();
    if (flags != 0) return -LINUX_EINVAL;
    if (!valid_ruleset_fd(ruleset_fd)) return -LINUX_EBADF;
    if (rule_type != LINUX_LANDLOCK_RULE_PATH_BENEATH &&
        rule_type != LINUX_LANDLOCK_RULE_NET_PORT) {
        return -LINUX_EINVAL;
    }
    if (!rule_attr) return -LINUX_EFAULT;
    /* Marco M1: no Landlock LSM, so accept the rule
     * structurally. Real enforcement lands when we have a path
     * resolver to evaluate against. */
    return 0;
}

int64_t linux_landlock_restrict_self(int ruleset_fd, uint32_t flags) {
    ensure_init();
    if (flags != 0) return -LINUX_EINVAL;
    if (!valid_ruleset_fd(ruleset_fd)) return -LINUX_EBADF;
    /* Marco M1: no per-task LSM hook; accept the request. The fd
     * remains alive for sandboxed userland probes. */
    return 0;
}

int64_t linux_landlock_close(int fd) {
    int slot = ruleset_slot(fd);
    if (slot < 0) return -LINUX_EBADF;
    g_ruleset_in_use[slot] = 0;
    return 0;
}

int64_t linux_landlock_read(int fd, void *buf, size_t len) {
    if (ruleset_slot(fd) < 0) return -LINUX_EBADF;
    if (len == 0) return 0;
    if (!buf) return -LINUX_EFAULT;
    return -LINUX_EINVAL;
}

int64_t linux_landlock_write(int fd, const void *buf, size_t len) {
    if (ruleset_slot(fd) < 0) return -LINUX_EBADF;
    if (len == 0) return 0;
    if (!buf) return -LINUX_EFAULT;
    return -LINUX_EINVAL;
}

int64_t linux_landlock_lseek(int fd, int64_t offset, int whence) {
    (void)offset; (void)whence;
    if (ruleset_slot(fd) < 0) return -LINUX_EBADF;
    return -LINUX_ESPIPE;
}

static int64_t sys_create(const struct linux_syscall_args *a) {
    return linux_landlock_create_ruleset(
        (const struct linux_landlock_ruleset_attr *)(uintptr_t)a->a0,
        (size_t)a->a1, (uint32_t)a->a2);
}
static int64_t sys_add(const struct linux_syscall_args *a) {
    return linux_landlock_add_rule((int)a->a0, (int)a->a1,
                                   (const void *)(uintptr_t)a->a2,
                                   (uint32_t)a->a3);
}
static int64_t sys_restrict(const struct linux_syscall_args *a) {
    return linux_landlock_restrict_self((int)a->a0, (uint32_t)a->a1);
}

void linux_landlock_register_syscalls(void) {
    ensure_init();
    (void)linux_syscall_register(LINUX_NR_landlock_create_ruleset,
                                 sys_create);
    (void)linux_syscall_register(LINUX_NR_landlock_add_rule, sys_add);
    (void)linux_syscall_register(LINUX_NR_landlock_restrict_self,
                                 sys_restrict);
}
