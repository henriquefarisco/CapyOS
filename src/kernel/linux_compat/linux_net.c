#include "kernel/linux_compat/linux_net.h"
#include "kernel/linux_compat/linux_errno.h"
#include "kernel/linux_compat/linux_syscall.h"
#include "kernel/linux_compat/linux_syscall_nrs.h"

#include <stdint.h>
#include <stddef.h>

static struct linux_net_ops g_ops;

void linux_net_install_ops(const struct linux_net_ops *ops) {
    if (ops) g_ops = *ops;
    else g_ops = (struct linux_net_ops){0};
}

void linux_net_reset_for_tests(void) {
    g_ops = (struct linux_net_ops){0};
}

int64_t linux_accept4(int sockfd, uint64_t addr_ptr,
                      uint64_t addrlen_ptr, uint32_t flags) {
    /* Linux: sockfd < 0 -> -EBADF. */
    if (sockfd < 0) return -LINUX_EBADF;
    /* Reject unknown flag bits (Linux 2.6.28+). */
    if (flags & ~LINUX_ACCEPT4_KNOWN_FLAGS) return -LINUX_EINVAL;
    /* If addr_ptr is non-null, addrlen_ptr must be non-null too. */
    if (addr_ptr != 0 && addrlen_ptr == 0) return -LINUX_EFAULT;

    if (!g_ops.accept4) return -LINUX_ENOSYS;
    int rc = g_ops.accept4(sockfd, (void *)(uintptr_t)addr_ptr,
                           (uint32_t *)(uintptr_t)addrlen_ptr, flags);
    if (rc < 0) return -LINUX_ENOTSOCK;  /* socket layer rejected */
    return (int64_t)rc;
}

int64_t linux_recvmmsg(int sockfd, uint64_t msgvec_ptr,
                       uint32_t vlen, uint32_t flags,
                       uint64_t timeout_ptr) {
    if (sockfd < 0) return -LINUX_EBADF;
    if (vlen == 0) return 0;
    if (vlen > LINUX_MMSG_MAX_VLEN) vlen = LINUX_MMSG_MAX_VLEN;
    if (msgvec_ptr == 0) return -LINUX_EFAULT;
    /* Linux kernel accepts a wider mask but our shim only knows
     * a subset. Bits outside the known mask -> -EINVAL. Once
     * sockets land we widen this. */
    if (flags & ~LINUX_MMSG_KNOWN_FLAGS) return -LINUX_EINVAL;

    if (!g_ops.recvmmsg) return -LINUX_ENOSYS;
    int rc = g_ops.recvmmsg(sockfd, (void *)(uintptr_t)msgvec_ptr,
                            vlen, flags, (void *)(uintptr_t)timeout_ptr);
    if (rc < 0) return -LINUX_ENOTSOCK;
    return (int64_t)rc;
}

int64_t linux_sendmmsg(int sockfd, uint64_t msgvec_ptr,
                       uint32_t vlen, uint32_t flags) {
    if (sockfd < 0) return -LINUX_EBADF;
    if (vlen == 0) return 0;
    if (vlen > LINUX_MMSG_MAX_VLEN) vlen = LINUX_MMSG_MAX_VLEN;
    if (msgvec_ptr == 0) return -LINUX_EFAULT;
    if (flags & ~LINUX_MMSG_KNOWN_FLAGS) return -LINUX_EINVAL;

    if (!g_ops.sendmmsg) return -LINUX_ENOSYS;
    int rc = g_ops.sendmmsg(sockfd, (void *)(uintptr_t)msgvec_ptr,
                            vlen, flags);
    if (rc < 0) return -LINUX_ENOTSOCK;
    return (int64_t)rc;
}

/* ---------- Syscall adapters ---------- */

static int64_t sys_accept4(const struct linux_syscall_args *a) {
    return linux_accept4((int)a->a0, a->a1, a->a2, (uint32_t)a->a3);
}

static int64_t sys_recvmmsg(const struct linux_syscall_args *a) {
    return linux_recvmmsg((int)a->a0, a->a1, (uint32_t)a->a2,
                          (uint32_t)a->a3, a->a4);
}

static int64_t sys_sendmmsg(const struct linux_syscall_args *a) {
    return linux_sendmmsg((int)a->a0, a->a1, (uint32_t)a->a2,
                          (uint32_t)a->a3);
}

void linux_net_register_syscalls(void) {
    (void)linux_syscall_register(LINUX_NR_accept4,  sys_accept4);
    (void)linux_syscall_register(LINUX_NR_recvmmsg, sys_recvmmsg);
    (void)linux_syscall_register(LINUX_NR_sendmmsg, sys_sendmmsg);
}
