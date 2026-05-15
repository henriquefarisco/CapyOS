#ifndef KERNEL_SYSCALL_NET_H
#define KERNEL_SYSCALL_NET_H

#include <stdint.h>
#include <stddef.h>

#include "kernel/syscall.h"

/* F4 seção c (2026-05-08) -- userland socket syscall handlers.
 *
 * The seven syscalls reserved as `SYS_SOCKET` (28) through `SYS_RECV`
 * (34) in `include/kernel/syscall_numbers.h` are wired here. Each
 * handler:
 *
 *   1. Resolves the calling process via `process_current()` and
 *      validates the caller-supplied process FD against the slot's
 *      `FD_TYPE_SOCKET` discriminator (so a stray fd numbered like a
 *      socket but holding a pipe can't be subverted).
 *   2. Defers the actual net-stack call to the registered
 *      `syscall_net_ops` table. Production wiring (in
 *      `syscall_net_init.c`) installs the real `socket_*` family
 *      from `src/net/services/socket.c`; host unit tests install a
 *      fake table that records calls without dragging in the
 *      whole TCP/UDP stack.
 *   3. Translates user-space pointers conservatively: the kernel
 *      runs on the caller's CR3 at syscall time so a direct deref
 *      is correct; we still copy `struct sockaddr_in` into a local
 *      to avoid TOCTOU between validation and use.
 *
 * Failure modes: `-1` for the entire family. Linux-style errno
 * granularity will follow once the userland libc layer (libcapy-net,
 * F4 seção d) settles. */

struct sockaddr_in;

struct syscall_net_ops {
  int (*sock_create)(int domain, int type, int protocol);
  int (*sock_bind)(int kernel_fd, const struct sockaddr_in *addr);
  int (*sock_listen)(int kernel_fd, int backlog);
  int (*sock_accept)(int kernel_fd, struct sockaddr_in *addr);
  int (*sock_connect)(int kernel_fd, const struct sockaddr_in *addr);
  int (*sock_send)(int kernel_fd, const void *buf, size_t len, int flags);
  int (*sock_recv)(int kernel_fd, void *buf, size_t len, int flags);
  int (*sock_close)(int kernel_fd);
  /* F4 seção c parte 3/3 (2026-05-08): hostname -> IPv4 lookup.
   * Production wires `dns_cache_lookup`. Returns 0 on hit (and
   * `*out_ip` is populated host-order), -1 on miss / NULL args.
   * NULL is permitted in the ops slot for tests that don't care
   * about resolution; in that case sys_dns_resolve returns -1. */
  int (*dns_resolve)(const char *name, uint32_t *out_ip);
};

/* Install a vtable. Passing NULL clears the registration so a fresh
 * test starts from a known "no backend" state where every call
 * returns -1. The pointer is stored as-is; callers must keep the
 * struct alive for the lifetime of the registration. */
void syscall_net_install_ops(const struct syscall_net_ops *ops);
const struct syscall_net_ops *syscall_net_get_ops(void);

/* Wire SYS_SOCKET..SYS_RECV (28..34) into the kernel syscall table.
 * Called once from `syscall_init()`. Relies on `syscall_register`
 * being available. */
void syscall_net_register_handlers(void);

/* Production-only wiring: install the real `socket_*` family from
 * `src/net/services/socket.c` as the active backend, AND register
 * `socket_close` on the process FD lifecycle. Lives in
 * `syscall_net_init.c` so unit tests linking `syscall_net.c`
 * standalone don't pick up the net-stack transitive deps. Must run
 * after `socket_system_init` and before the first `SYS_SOCKET`
 * dispatch. */
void syscall_net_install_default_ops(void);

/* Individual handlers. Exposed (non-static) so host tests can drive
 * them with a synthetic frame, matching the precedent set by
 * `sys_read`/`sys_write` in `kernel/syscall.h`. */
int64_t sys_socket(struct syscall_frame *f);
int64_t sys_bind(struct syscall_frame *f);
int64_t sys_listen(struct syscall_frame *f);
int64_t sys_accept(struct syscall_frame *f);
int64_t sys_connect(struct syscall_frame *f);
int64_t sys_send(struct syscall_frame *f);
int64_t sys_recv(struct syscall_frame *f);
int64_t sys_dns_resolve(struct syscall_frame *f);

/* Bridge used by `sys_read` / `sys_write` in `src/kernel/syscall.c`
 * when a process FD slot has `type == FD_TYPE_SOCKET`. Returns the
 * byte count on success, -1 on error or no installed backend. */
int64_t syscall_net_fd_read(int kernel_fd, void *buf, size_t len);
int64_t syscall_net_fd_write(int kernel_fd, const void *buf, size_t len);

#endif /* KERNEL_SYSCALL_NET_H */
