#ifndef KERNEL_LINUX_COMPAT_LINUX_NUMA_H
#define KERNEL_LINUX_COMPAT_LINUX_NUMA_H

/* Linux ABI NUMA memory-policy syscalls.
 *
 *   long get_mempolicy(int *policy, unsigned long *nodemask,
 *                       unsigned long maxnode, void *addr,
 *                       unsigned long flags);
 *   long set_mempolicy(int policy, const unsigned long *nodemask,
 *                       unsigned long maxnode);
 *   long mbind        (void *addr, unsigned long len, int policy,
 *                       const unsigned long *nodemask,
 *                       unsigned long maxnode, unsigned int flags);
 *
 * Why this matters for the Firefox port:
 *   - Firefox WebRender uses get_mempolicy on the GL command
 *     buffer to detect NUMA topology and pre-bind compositor
 *     buffers to the same node as the rendering CPU. -ENOSYS
 *     makes WebRender skip NUMA-aware allocation.
 *   - SpiderMonkey GC uses set_mempolicy(MPOL_PREFERRED) on
 *     hot heap pages on multi-socket machines.
 *   - libnuma probes mempolicy syscalls during init; -ENOSYS
 *     is gracefully handled but emits warnings on every fork.
 *
 * Marco M1 is single-NUMA (one zone). We faithfully report:
 *   - get_mempolicy: returns MPOL_DEFAULT, single-bit nodemask
 *     for node 0.
 *   - set_mempolicy: validates the policy and accepts the
 *     nodemask, no-op success.
 *   - mbind: validates and accepts; the actual binding is a
 *     no-op (single zone).
 *
 * Linux mempolicy modes: */
#define LINUX_MPOL_DEFAULT       0
#define LINUX_MPOL_PREFERRED     1
#define LINUX_MPOL_BIND          2
#define LINUX_MPOL_INTERLEAVE    3
#define LINUX_MPOL_LOCAL         4

#define LINUX_MPOL_F_NODE        (1u << 0)
#define LINUX_MPOL_F_ADDR        (1u << 1)
#define LINUX_MPOL_F_MEMS_ALLOWED (1u << 2)

#define LINUX_MPOL_F_KNOWN \
    (LINUX_MPOL_F_NODE | LINUX_MPOL_F_ADDR | \
     LINUX_MPOL_F_MEMS_ALLOWED)

/* mbind() flags (move-pages semantics). */
#define LINUX_MPOL_MF_STRICT     (1u << 0)
#define LINUX_MPOL_MF_MOVE       (1u << 1)
#define LINUX_MPOL_MF_MOVE_ALL   (1u << 2)

#define LINUX_MPOL_MF_KNOWN \
    (LINUX_MPOL_MF_STRICT | LINUX_MPOL_MF_MOVE | \
     LINUX_MPOL_MF_MOVE_ALL)

#include <stdint.h>
#include <stddef.h>

int64_t linux_get_mempolicy(int *policy, uint64_t *nodemask,
                            uint64_t maxnode, void *addr,
                            uint32_t flags);
int64_t linux_set_mempolicy(int policy, const uint64_t *nodemask,
                            uint64_t maxnode);
int64_t linux_mbind(void *addr, uint64_t len, int policy,
                    const uint64_t *nodemask, uint64_t maxnode,
                    uint32_t flags);

void linux_numa_register_syscalls(void);

#endif /* KERNEL_LINUX_COMPAT_LINUX_NUMA_H */
