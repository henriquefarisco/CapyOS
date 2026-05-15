#include "kernel/linux_compat/linux_net.h"

/* Boot wiring for `linux_net`. Excluded from host tests.
 *
 * BSD sockets are not yet exposed by `src/net/`. For Marco M1
 * the trampoline leaves ops NULL so all 3 syscalls return
 * -ENOSYS. When the socket layer lands the wrappers below get
 * a real implementation; userland behavior is unchanged
 * (musl/Chromium IPC tolerate ENOSYS by falling back to the
 * legacy accept/recvmsg/sendmsg paths).
 */

#if !defined(UNIT_TEST)

void linux_net_init_boot(void) {
    /* No socket layer yet -> install no ops. accept4/recvmmsg/
     * sendmmsg surface as -ENOSYS via the fallthrough in
     * linux_net.c. Validation (flags, fd >= 0, addrlen_ptr) is
     * still performed before the ENOSYS to lock the contract
     * userland code expects. */
    linux_net_install_ops(NULL);
}

#endif /* !UNIT_TEST */
