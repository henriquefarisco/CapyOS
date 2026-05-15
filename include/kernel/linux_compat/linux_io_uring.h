#ifndef KERNEL_LINUX_COMPAT_LINUX_IO_URING_H
#define KERNEL_LINUX_COMPAT_LINUX_IO_URING_H

/* Linux ABI io_uring high-performance async I/O syscalls.
 *
 *   int io_uring_setup   (uint32_t entries, struct io_uring_params *p);
 *   int io_uring_enter   (int fd, uint32_t to_submit,
 *                          uint32_t min_complete, uint32_t flags,
 *                          const void *sig, size_t sigsz);
 *   int io_uring_register(int fd, uint32_t opcode,
 *                          void *arg, uint32_t nr_args);
 *
 * Why this matters for the Firefox port:
 *   - Firefox HTTP/3 stack (necko) probes io_uring at startup
 *     to opt into kernel-side completion-driven I/O on Linux
 *     5.5+. -ENOSYS makes it fall back to epoll+nonblocking
 *     read/write (the existing path). This is the correct
 *     behaviour for Marco M1 -- we report -ENOSYS until the
 *     async I/O backend lands.
 *   - SpiderMonkey's `IOUringJobBackend` (experimental) probes
 *     io_uring_setup; -ENOSYS forces it onto the worker-thread
 *     fallback.
 *   - liburing's `io_uring_queue_init` issues all three
 *     syscalls; we accept structurally and report -ENOSYS so
 *     userland's "is this supported?" path is deterministic.
 *
 * Linux semantics:
 *   - io_uring_setup: returns a fd >= 0 on success; entries
 *     must be a power of two between 1 and IORING_MAX_ENTRIES
 *     (4096); params are output (kernel fills sq/cq sizes).
 *   - io_uring_enter: submits sqes and optionally waits for
 *     cqes; flags control the wait semantics.
 *   - io_uring_register: registers buffers / files / eventfds
 *     for fast lookup by ring fd.
 *
 * Marco M1: report -ENOSYS for all three. Userland glibc/musl/
 * Firefox detect the absence and use epoll+read/write. */

#include <stdint.h>
#include <stddef.h>

#define LINUX_IORING_MAX_ENTRIES   4096

/* io_uring_setup flags. */
#define LINUX_IORING_SETUP_IOPOLL    (1u << 0)
#define LINUX_IORING_SETUP_SQPOLL    (1u << 1)
#define LINUX_IORING_SETUP_SQ_AFF    (1u << 2)
#define LINUX_IORING_SETUP_CQSIZE    (1u << 3)
#define LINUX_IORING_SETUP_CLAMP     (1u << 4)
#define LINUX_IORING_SETUP_ATTACH_WQ (1u << 5)

#define LINUX_IORING_SETUP_KNOWN \
    (LINUX_IORING_SETUP_IOPOLL | LINUX_IORING_SETUP_SQPOLL | \
     LINUX_IORING_SETUP_SQ_AFF | LINUX_IORING_SETUP_CQSIZE | \
     LINUX_IORING_SETUP_CLAMP | LINUX_IORING_SETUP_ATTACH_WQ)

/* io_uring_enter flags. */
#define LINUX_IORING_ENTER_GETEVENTS    (1u << 0)
#define LINUX_IORING_ENTER_SQ_WAKEUP    (1u << 1)
#define LINUX_IORING_ENTER_SQ_WAIT      (1u << 2)
#define LINUX_IORING_ENTER_EXT_ARG      (1u << 3)
#define LINUX_IORING_ENTER_REGISTERED_RING (1u << 4)

#define LINUX_IORING_ENTER_KNOWN \
    (LINUX_IORING_ENTER_GETEVENTS | LINUX_IORING_ENTER_SQ_WAKEUP | \
     LINUX_IORING_ENTER_SQ_WAIT | LINUX_IORING_ENTER_EXT_ARG | \
     LINUX_IORING_ENTER_REGISTERED_RING)

/* io_uring_register opcodes (subset). */
#define LINUX_IORING_REGISTER_BUFFERS       0
#define LINUX_IORING_UNREGISTER_BUFFERS     1
#define LINUX_IORING_REGISTER_FILES         2
#define LINUX_IORING_UNREGISTER_FILES       3
#define LINUX_IORING_REGISTER_EVENTFD       4
#define LINUX_IORING_UNREGISTER_EVENTFD     5
#define LINUX_IORING_REGISTER_FILES_UPDATE  6
#define LINUX_IORING_REGISTER_OPCODE_MAX    32

/* sigset size; sig parameter must be either 0 or sigsz==8 (mask only). */
#define LINUX_IORING_SIGSET_SIZE 8

int64_t linux_io_uring_setup   (uint32_t entries, void *params);
int64_t linux_io_uring_enter   (int fd, uint32_t to_submit,
                                uint32_t min_complete,
                                uint32_t flags, const void *sig,
                                size_t sigsz);
int64_t linux_io_uring_register(int fd, uint32_t opcode, void *arg,
                                uint32_t nr_args);

void linux_io_uring_register_syscalls(void);

#endif /* KERNEL_LINUX_COMPAT_LINUX_IO_URING_H */
