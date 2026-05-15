#include "kernel/linux_compat/linux_namespace.h"
#include "kernel/linux_compat/linux_syscall.h"
#include "kernel/linux_compat/linux_syscall_nrs.h"
#include "kernel/linux_compat/linux_errno.h"

#include <stdint.h>
#include <stddef.h>

int64_t linux_unshare(int flags) {
    if ((unsigned)flags & ~(unsigned)LINUX_UNSHARE_KNOWN_FLAGS) {
        return -LINUX_EINVAL;
    }
    /* Linux invariants:
     *   - CLONE_THREAD requires CLONE_VM + CLONE_SIGHAND.
     *   - CLONE_SIGHAND requires CLONE_VM.
     * (Same as clone(2).) */
    if ((flags & LINUX_CLONE_THREAD) &&
        !((flags & LINUX_CLONE_VM) && (flags & LINUX_CLONE_SIGHAND))) {
        return -LINUX_EINVAL;
    }
    if ((flags & LINUX_CLONE_SIGHAND) && !(flags & LINUX_CLONE_VM)) {
        return -LINUX_EINVAL;
    }
    /* Marco M1 single-task / single-namespace: accept the
     * request as no-op success. Per-task namespace state lands
     * with task tables. */
    return 0;
}

static int strcmp_short(const char *a, const char *b) {
    while (*a && *b && *a == *b) { a++; b++; }
    return (int)(unsigned char)*a - (int)(unsigned char)*b;
}

static int fstype_supported(const char *t) {
    /* Marco M1: tmpfs, proc, devpts (musl pseudo-tty mount),
     * sysfs (firefox capability probe), none (bind mount). */
    if (!strcmp_short(t, "tmpfs"))  return 1;
    if (!strcmp_short(t, "proc"))   return 1;
    if (!strcmp_short(t, "devpts")) return 1;
    if (!strcmp_short(t, "sysfs"))  return 1;
    if (!strcmp_short(t, "none"))   return 1;
    return 0;
}

int64_t linux_mount(const char *source, const char *target,
                    const char *fstype, uint64_t flags,
                    const void *data) {
    (void)data; /* Linux: data is fs-specific options blob. */
    if (!target) return -LINUX_EFAULT;
    if (target[0] == '\0') return -LINUX_ENOENT;
    if (flags & ~(uint64_t)LINUX_MS_KNOWN_FLAGS) return -LINUX_EINVAL;

    /* Linux: BIND/MOVE/REMOUNT bypass fstype lookup. */
    if (flags & (LINUX_MS_BIND | LINUX_MS_MOVE | LINUX_MS_REMOUNT)) {
        if (flags & LINUX_MS_BIND) {
            if (!source) return -LINUX_EFAULT;
            if (source[0] == '\0') return -LINUX_ENOENT;
        }
        return 0;
    }

    if (!fstype) return -LINUX_EFAULT;
    if (!fstype_supported(fstype)) return -LINUX_ENODEV;

    /* "none" requires source to be NULL or empty per convention. */
    /* Most filesystems require a source string; we don't enforce
     * (Linux defers to the fs driver). */
    return 0;
}

int64_t linux_umount2(const char *target, int flags) {
    if (!target) return -LINUX_EFAULT;
    if (target[0] == '\0') return -LINUX_ENOENT;
    if ((unsigned)flags & ~(unsigned)LINUX_UMOUNT_KNOWN_FLAGS) {
        return -LINUX_EINVAL;
    }
    /* Marco M1 has no mount table to remove from; accept. */
    return 0;
}

static int64_t sys_unshare(const struct linux_syscall_args *a) {
    return linux_unshare((int)a->a0);
}
static int64_t sys_mount(const struct linux_syscall_args *a) {
    return linux_mount((const char *)(uintptr_t)a->a0,
                       (const char *)(uintptr_t)a->a1,
                       (const char *)(uintptr_t)a->a2,
                       (uint64_t)a->a3,
                       (const void *)(uintptr_t)a->a4);
}
static int64_t sys_umount2(const struct linux_syscall_args *a) {
    return linux_umount2((const char *)(uintptr_t)a->a0, (int)a->a1);
}

void linux_namespace_register_syscalls(void) {
    (void)linux_syscall_register(LINUX_NR_unshare, sys_unshare);
    (void)linux_syscall_register(LINUX_NR_mount,   sys_mount);
    (void)linux_syscall_register(LINUX_NR_umount2, sys_umount2);
}
