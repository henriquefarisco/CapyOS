#include "kernel/linux_compat/linux_io_uring.h"
#include "kernel/linux_compat/linux_syscall.h"
#include "kernel/linux_compat/linux_syscall_nrs.h"
#include "kernel/linux_compat/linux_errno.h"

#include <stdint.h>
#include <stddef.h>

static int is_power_of_two(uint32_t v) {
    return v > 0 && (v & (v - 1)) == 0;
}

int64_t linux_io_uring_setup(uint32_t entries, void *params) {
    if (entries == 0) return -LINUX_EINVAL;
    if (entries > LINUX_IORING_MAX_ENTRIES) return -LINUX_EINVAL;
    if (!is_power_of_two(entries)) return -LINUX_EINVAL;
    /* Linux: params must be a valid pointer; the kernel writes
     * into the sq/cq size fields. */
    if (!params) return -LINUX_EFAULT;
    /* Marco M1: no async I/O backend. Report -ENOSYS so userland
     * (musl, liburing-based code, Firefox necko) falls back to
     * epoll+read/write. */
    return -LINUX_ENOSYS;
}

int64_t linux_io_uring_enter(int fd, uint32_t to_submit,
                             uint32_t min_complete,
                             uint32_t flags, const void *sig,
                             size_t sigsz) {
    if (fd < 0) return -LINUX_EBADF;
    if (flags & ~LINUX_IORING_ENTER_KNOWN) return -LINUX_EINVAL;
    /* sig must be NULL or sigsz must be 8 (kernel-mask size on
     * x86_64). Linux uses sigsz for an old-vs-new ABI tell. */
    if (sig && sigsz != LINUX_IORING_SIGSET_SIZE) {
        return -LINUX_EINVAL;
    }
    (void)to_submit; (void)min_complete;
    /* Marco M1: no ring fd ever exists, but the validation has
     * already happened so userland sees a clean -ENOSYS path. */
    return -LINUX_ENOSYS;
}

int64_t linux_io_uring_register(int fd, uint32_t opcode, void *arg,
                                uint32_t nr_args) {
    if (fd < 0) return -LINUX_EBADF;
    if (opcode >= LINUX_IORING_REGISTER_OPCODE_MAX) return -LINUX_EINVAL;
    /* Linux: BUFFERS/FILES with nr_args > 0 require non-NULL arg. */
    if (nr_args > 0 && !arg) return -LINUX_EFAULT;
    return -LINUX_ENOSYS;
}

static int64_t sys_setup(const struct linux_syscall_args *a) {
    return linux_io_uring_setup((uint32_t)a->a0,
                                (void *)(uintptr_t)a->a1);
}
static int64_t sys_enter(const struct linux_syscall_args *a) {
    return linux_io_uring_enter((int)a->a0, (uint32_t)a->a1,
                                (uint32_t)a->a2, (uint32_t)a->a3,
                                (const void *)(uintptr_t)a->a4,
                                (size_t)a->a5);
}
static int64_t sys_register(const struct linux_syscall_args *a) {
    return linux_io_uring_register((int)a->a0, (uint32_t)a->a1,
                                   (void *)(uintptr_t)a->a2,
                                   (uint32_t)a->a3);
}

void linux_io_uring_register_syscalls(void) {
    (void)linux_syscall_register(LINUX_NR_io_uring_setup,    sys_setup);
    (void)linux_syscall_register(LINUX_NR_io_uring_enter,    sys_enter);
    (void)linux_syscall_register(LINUX_NR_io_uring_register, sys_register);
}
