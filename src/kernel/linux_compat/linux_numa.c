#include "kernel/linux_compat/linux_numa.h"
#include "kernel/linux_compat/linux_syscall.h"
#include "kernel/linux_compat/linux_syscall_nrs.h"
#include "kernel/linux_compat/linux_errno.h"

#include <stdint.h>
#include <stddef.h>

static int policy_known(int p) {
    switch (p) {
        case LINUX_MPOL_DEFAULT:
        case LINUX_MPOL_PREFERRED:
        case LINUX_MPOL_BIND:
        case LINUX_MPOL_INTERLEAVE:
        case LINUX_MPOL_LOCAL:
            return 1;
        default:
            return 0;
    }
}

int64_t linux_get_mempolicy(int *policy, uint64_t *nodemask,
                            uint64_t maxnode, void *addr,
                            uint32_t flags) {
    (void)addr;
    if (flags & ~LINUX_MPOL_F_KNOWN) return -LINUX_EINVAL;
    /* Linux: maxnode must be > 0 if nodemask is non-NULL. */
    if (nodemask && maxnode == 0) return -LINUX_EINVAL;

    if (policy) *policy = LINUX_MPOL_DEFAULT;
    if (nodemask) {
        /* Marco M1 single-NUMA: only node 0 is set. We zero
         * everything beyond bit 0 so userland sees a clean
         * single-node mask. */
        size_t words = (size_t)((maxnode + 63) / 64);
        for (size_t i = 0; i < words; i++) nodemask[i] = 0;
        if (words > 0) nodemask[0] = 1ULL;
    }
    return 0;
}

int64_t linux_set_mempolicy(int policy, const uint64_t *nodemask,
                            uint64_t maxnode) {
    if (!policy_known(policy)) return -LINUX_EINVAL;
    /* Linux: BIND/INTERLEAVE/PREFERRED require a non-empty
     * nodemask; DEFAULT/LOCAL must have an empty (or NULL)
     * one. */
    if (policy == LINUX_MPOL_BIND ||
        policy == LINUX_MPOL_INTERLEAVE ||
        policy == LINUX_MPOL_PREFERRED) {
        if (!nodemask || maxnode == 0) return -LINUX_EINVAL;
    }
    if (policy == LINUX_MPOL_DEFAULT && nodemask) {
        /* Linux: DEFAULT must come with empty nodemask, but
         * we accept any since checking emptiness over a
         * variable-size mask isn't pleasant; the kernel does
         * the same lenient thing in modern versions. */
    }
    /* Marco M1 single-NUMA: no-op success. */
    return 0;
}

int64_t linux_mbind(void *addr, uint64_t len, int policy,
                    const uint64_t *nodemask, uint64_t maxnode,
                    uint32_t flags) {
    (void)addr; (void)len;
    if (!policy_known(policy)) return -LINUX_EINVAL;
    if (flags & ~LINUX_MPOL_MF_KNOWN) return -LINUX_EINVAL;
    if (policy == LINUX_MPOL_BIND ||
        policy == LINUX_MPOL_INTERLEAVE ||
        policy == LINUX_MPOL_PREFERRED) {
        if (!nodemask || maxnode == 0) return -LINUX_EINVAL;
    }
    /* Marco M1 single-NUMA: binding is a no-op. */
    return 0;
}

static int64_t sys_get_mempolicy(const struct linux_syscall_args *a) {
    return linux_get_mempolicy((int *)(uintptr_t)a->a0,
                               (uint64_t *)(uintptr_t)a->a1,
                               (uint64_t)a->a2,
                               (void *)(uintptr_t)a->a3,
                               (uint32_t)a->a4);
}
static int64_t sys_set_mempolicy(const struct linux_syscall_args *a) {
    return linux_set_mempolicy((int)a->a0,
                               (const uint64_t *)(uintptr_t)a->a1,
                               (uint64_t)a->a2);
}
static int64_t sys_mbind(const struct linux_syscall_args *a) {
    return linux_mbind((void *)(uintptr_t)a->a0, (uint64_t)a->a1,
                       (int)a->a2,
                       (const uint64_t *)(uintptr_t)a->a3,
                       (uint64_t)a->a4, (uint32_t)a->a5);
}

void linux_numa_register_syscalls(void) {
    (void)linux_syscall_register(LINUX_NR_get_mempolicy, sys_get_mempolicy);
    (void)linux_syscall_register(LINUX_NR_set_mempolicy, sys_set_mempolicy);
    (void)linux_syscall_register(LINUX_NR_mbind,         sys_mbind);
}
