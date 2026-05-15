#ifndef KERNEL_LINUX_COMPAT_LINUX_NET_H
#define KERNEL_LINUX_COMPAT_LINUX_NET_H

#include <stdint.h>
#include <stddef.h>

/* Linux-ABI networking shim (S1.14).
 *
 * `accept4`, `recvmmsg`, `sendmmsg` -- Linux extensions over the
 * legacy `accept`/`recvmsg`/`sendmsg`. These add atomic flag
 * setting (O_CLOEXEC, O_NONBLOCK) and batched message I/O.
 *
 * Marco M1 status: CapyOS does not yet expose Linux BSD sockets
 * (we have a non-BSD network stack in `src/net/`). These shims
 * therefore validate the flag/parameter shape and return
 * -ENOSYS for the operation itself. The validation lives in
 * production code so when sockets land, only the fallthrough
 * line changes.
 */

/* accept4 flag bits (sys/socket.h on Linux). */
#define LINUX_SOCK_NONBLOCK 0x000800u
#define LINUX_SOCK_CLOEXEC  0x080000u
#define LINUX_ACCEPT4_KNOWN_FLAGS \
    (LINUX_SOCK_NONBLOCK | LINUX_SOCK_CLOEXEC)

/* recvmmsg / sendmmsg flag bits we recognise. The kernel Linux
 * accepts a wider mask; for Marco M1 we recognise the common
 * ones and pass through. */
#define LINUX_MSG_DONTWAIT  0x40u
#define LINUX_MSG_WAITFORONE 0x10000u
#define LINUX_MMSG_KNOWN_FLAGS \
    (LINUX_MSG_DONTWAIT | LINUX_MSG_WAITFORONE)

/* Cap on the vlen argument to recvmmsg/sendmmsg. Linux uses
 * UIO_MAXIOV (1024). */
#define LINUX_MMSG_MAX_VLEN 1024u

struct linux_net_ops {
    /* Place-holder for when sockets land. accept4 forwards
     * (sockfd, addr, addrlen, flags) to the socket layer. */
    int (*accept4)(int sockfd, void *addr, uint32_t *addrlen,
                   uint32_t flags);
    /* recvmmsg/sendmmsg forward the array. */
    int (*recvmmsg)(int sockfd, void *msgvec, uint32_t vlen,
                    uint32_t flags, void *timeout);
    int (*sendmmsg)(int sockfd, void *msgvec, uint32_t vlen,
                    uint32_t flags);
};

void linux_net_install_ops(const struct linux_net_ops *ops);
void linux_net_reset_for_tests(void);

int64_t linux_accept4(int sockfd, uint64_t addr_ptr,
                      uint64_t addrlen_ptr, uint32_t flags);

int64_t linux_recvmmsg(int sockfd, uint64_t msgvec_ptr,
                       uint32_t vlen, uint32_t flags,
                       uint64_t timeout_ptr);

int64_t linux_sendmmsg(int sockfd, uint64_t msgvec_ptr,
                       uint32_t vlen, uint32_t flags);

void linux_net_register_syscalls(void);

#endif /* KERNEL_LINUX_COMPAT_LINUX_NET_H */
