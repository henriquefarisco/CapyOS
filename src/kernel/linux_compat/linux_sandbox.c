#include "kernel/linux_compat/linux_sandbox.h"
#include "kernel/linux_compat/linux_syscall.h"
#include "kernel/linux_compat/linux_syscall_nrs.h"
#include "kernel/linux_compat/linux_errno.h"

#include <stdint.h>
#include <stddef.h>

static struct linux_sandbox_ops g_ops;
static int                      g_ops_installed;
static uint32_t                 g_persona;
static int                      g_fsuid;
static int                      g_fsgid;

void linux_sandbox_install_ops(const struct linux_sandbox_ops *ops) {
    if (!ops) {
        g_ops = (struct linux_sandbox_ops){0};
        g_ops_installed = 0;
        return;
    }
    g_ops = *ops;
    g_ops_installed = 1;
}

void linux_sandbox_reset_for_tests(void) {
    g_ops = (struct linux_sandbox_ops){0};
    g_ops_installed = 0;
    g_persona = LINUX_PER_LINUX;
    g_fsuid = 0;
    g_fsgid = 0;
}

int64_t linux_chroot(const char *path) {
    if (!path) return -LINUX_EFAULT;
    if (path[0] == '\0') return -LINUX_ENOENT;
    if (g_ops_installed && g_ops.chroot) {
        return g_ops.chroot(path);
    }
    /* Marco M1 single-root: accept any well-formed path as
     * no-op success. CAP_SYS_CHROOT is implicit at root. */
    return 0;
}

int64_t linux_personality(uint32_t persona) {
    uint32_t prev = g_persona;
    if (persona != LINUX_PERSONALITY_QUERY) {
        /* Linux: kernel accepts any persona bits, even unknown
         * ones; userland is expected to mask. We follow the
         * same liberal behaviour. */
        g_persona = persona;
    }
    return (int64_t)(uint32_t)prev;
}

int64_t linux_setfsuid(int fsuid) {
    int prev = g_fsuid;
    /* Linux: accepts any uid (including -1 which is a probe).
     * Returns previous fsuid regardless. The "did it actually
     * change?" check is done by issuing setfsuid(-1) before
     * and after. */
    if (fsuid >= 0) g_fsuid = fsuid;
    return prev;
}

int64_t linux_setfsgid(int fsgid) {
    int prev = g_fsgid;
    if (fsgid >= 0) g_fsgid = fsgid;
    return prev;
}

static int64_t sys_chroot(const struct linux_syscall_args *a) {
    return linux_chroot((const char *)(uintptr_t)a->a0);
}
static int64_t sys_personality(const struct linux_syscall_args *a) {
    return linux_personality((uint32_t)a->a0);
}
static int64_t sys_setfsuid(const struct linux_syscall_args *a) {
    return linux_setfsuid((int)a->a0);
}
static int64_t sys_setfsgid(const struct linux_syscall_args *a) {
    return linux_setfsgid((int)a->a0);
}

void linux_sandbox_register_syscalls(void) {
    (void)linux_syscall_register(LINUX_NR_chroot,      sys_chroot);
    (void)linux_syscall_register(LINUX_NR_personality, sys_personality);
    (void)linux_syscall_register(LINUX_NR_setfsuid,    sys_setfsuid);
    (void)linux_syscall_register(LINUX_NR_setfsgid,    sys_setfsgid);
}
