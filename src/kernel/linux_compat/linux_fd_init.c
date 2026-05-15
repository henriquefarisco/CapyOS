#include "kernel/linux_compat/linux_fd.h"

/* Boot wiring for `linux_fd` against the real kernel pipe + fd
 * primitives. Excluded from host tests.
 *
 * The dup3 path is not yet supported by `pipe.c` (the kernel
 * fd table is pipe-id encoded and per-task fd cloning lives in
 * src/kernel/process.c). For now the trampoline returns -1 so
 * dup3 surfaces -LINUX_EBADF until a full fd table lands.
 *
 * pipe2 maps directly onto `pipe_create`. The kernel pipe ids
 * are returned with the +256 offset for the write side which
 * the existing userland already understands.
 */

#if !defined(UNIT_TEST)

#include "kernel/pipe.h"

#include <stdint.h>

static int wrap_pipe_create(int fds_out[2]) {
    return pipe_create(fds_out);
}

static int wrap_dup3(int oldfd, int newfd) {
    (void)oldfd;
    (void)newfd;
    /* Not yet implemented in the kernel fd table. The shim
     * surfaces -EBADF until a real fd table lands (S5 territory). */
    return -1;
}

static void wrap_set_fd_flags(int fd, uint32_t flags) {
    (void)fd;
    (void)flags;
    /* No per-fd flag table yet. The flags that matter for Marco
     * M1 are O_CLOEXEC (we never exec from kernel except via the
     * loader which does not preserve fds anyway) and O_NONBLOCK
     * (pipes are non-blocking by default in CapyOS). Best effort. */
}

void linux_fd_init_boot(void) {
    static const struct linux_fd_ops ops = {
        .pipe_create  = wrap_pipe_create,
        .dup3         = wrap_dup3,
        .set_fd_flags = wrap_set_fd_flags,
    };
    linux_fd_install_ops(&ops);
}

#endif /* !UNIT_TEST */
