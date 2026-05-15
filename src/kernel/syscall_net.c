/*
 * src/kernel/syscall_net.c (2026-05-08, F4 seção c)
 *
 * Userland socket syscall handlers.
 *
 * The seven `SYS_SOCKET` family entries (numbers 28..34, fixed in
 * `include/kernel/syscall_numbers.h`) are wired here. Each one is a
 * thin glue layer between:
 *
 *   - the SYSCALL ABI register frame (`struct syscall_frame`),
 *   - the per-process FD table (`process_fd_alloc`/`process_fd_free`,
 *     with `FD_TYPE_SOCKET` slots backed by a kernel socket fd
 *     stored in `private_data`),
 *   - and an injectable net backend vtable (`struct syscall_net_ops`)
 *     so host unit tests can swap in a fake without dragging the
 *     whole TCP/UDP stack into the test binary.
 *
 * Production boot wiring (`syscall_net_init.c`) installs the real
 * `socket_*` family from `src/net/services/socket.c`. Tests install
 * a recording fake. With no backend installed, every syscall in
 * this family returns -1 deterministically.
 *
 * The handlers do NOT model Linux-style errnos yet; F4 seção d
 * (userland libcapy-net wrapper) is the layer where errno will be
 * synthesised from the `-1` returns.
 */

#include "kernel/syscall_net.h"
#include "kernel/process.h"
#include "net/socket.h"

#include <stddef.h>
#include <stdint.h>

static const struct syscall_net_ops *g_net_ops = NULL;

void syscall_net_install_ops(const struct syscall_net_ops *ops) {
  g_net_ops = ops;
}

const struct syscall_net_ops *syscall_net_get_ops(void) {
  return g_net_ops;
}

/* Resolve the process slot for a userland-visible `fd`. Returns the
 * embedded kernel socket fd (>= 0) when the slot is a valid
 * FD_TYPE_SOCKET, -1 otherwise (no current process, fd out of
 * range, slot type mismatch, malformed private_data). */
static int resolve_socket_fd(int fd) {
  struct process *proc = process_current();
  if (!proc) return -1;
  if (fd < 0 || fd >= PROCESS_FD_MAX) return -1;
  struct file_descriptor *slot = &proc->fds[fd];
  if (slot->type != FD_TYPE_SOCKET) return -1;
  int kernel_fd = (int)(intptr_t)slot->private_data;
  if (kernel_fd < 0) return -1;
  return kernel_fd;
}

/* Copy a user-supplied `struct sockaddr_in` into a local. The kernel
 * runs on the caller's CR3 at syscall entry so a direct deref is
 * sound; the local copy guards against the user mutating the buffer
 * between validation and use. */
static int copy_sockaddr_in(const struct sockaddr_in *user_addr,
                            struct sockaddr_in *out) {
  if (!user_addr || !out) return -1;
  *out = *user_addr;
  return 0;
}

/* sys_socket(domain, type, protocol)
 *
 *   rdi = domain (AF_INET only)
 *   rsi = type   (SOCK_STREAM | SOCK_DGRAM)
 *   rdx = protocol (0 = default)
 *
 * Allocates a kernel socket via the registered backend, then a
 * process FD slot of type FD_TYPE_SOCKET pointing at it. On any
 * failure the partial state is rolled back (kernel socket closed
 * if the FD-table allocation failed). */
int64_t sys_socket(struct syscall_frame *f) {
  int domain = (int)f->rdi;
  int type = (int)f->rsi;
  int protocol = (int)f->rdx;

  if (!g_net_ops || !g_net_ops->sock_create) return -1;
  if (domain != AF_INET) return -1;
  if (type != SOCK_STREAM && type != SOCK_DGRAM) return -1;

  struct process *proc = process_current();
  if (!proc) return -1;

  int kernel_fd = g_net_ops->sock_create(domain, type, protocol);
  if (kernel_fd < 0) return -1;

  int proc_fd = process_fd_alloc(proc);
  if (proc_fd < 0) {
    if (g_net_ops->sock_close) (void)g_net_ops->sock_close(kernel_fd);
    return -1;
  }

  proc->fds[proc_fd].type = FD_TYPE_SOCKET;
  proc->fds[proc_fd].flags = 0;
  proc->fds[proc_fd].private_data = (void *)(intptr_t)kernel_fd;
  proc->fds[proc_fd].offset = 0;
  return (int64_t)proc_fd;
}

/* sys_bind(fd, addr, addrlen)
 *
 *   rdi = fd
 *   rsi = const struct sockaddr_in *addr
 *   rdx = addrlen (must be >= sizeof(struct sockaddr_in))
 *
 * Returns 0 on success, -1 on failure. */
int64_t sys_bind(struct syscall_frame *f) {
  int fd = (int)f->rdi;
  const struct sockaddr_in *user_addr =
      (const struct sockaddr_in *)f->rsi;
  uint32_t addrlen = (uint32_t)f->rdx;

  if (!g_net_ops || !g_net_ops->sock_bind) return -1;
  if (!user_addr) return -1;
  if (addrlen < sizeof(struct sockaddr_in)) return -1;

  int kernel_fd = resolve_socket_fd(fd);
  if (kernel_fd < 0) return -1;

  struct sockaddr_in local;
  if (copy_sockaddr_in(user_addr, &local) != 0) return -1;
  if (local.sin_family != AF_INET) return -1;

  return (int64_t)g_net_ops->sock_bind(kernel_fd, &local);
}

/* sys_listen(fd, backlog)
 *
 *   rdi = fd
 *   rsi = backlog (>= 0; clamped by backend)
 *
 * Returns 0 on success, -1 on failure. */
int64_t sys_listen(struct syscall_frame *f) {
  int fd = (int)f->rdi;
  int backlog = (int)f->rsi;

  if (!g_net_ops || !g_net_ops->sock_listen) return -1;
  if (backlog < 0) return -1;

  int kernel_fd = resolve_socket_fd(fd);
  if (kernel_fd < 0) return -1;

  return (int64_t)g_net_ops->sock_listen(kernel_fd, backlog);
}

/* sys_accept(fd, addr, addrlen)
 *
 *   rdi = fd
 *   rsi = struct sockaddr_in *addr  (NULL = caller does not care)
 *   rdx = uint32_t *addrlen         (in: cap; out: actual)
 *
 * Returns the new userland fd on success, -1 on failure. The
 * underlying socket layer currently does not support accept (the
 * production `socket_accept` in `src/net/services/socket.c` returns
 * -1 unconditionally), so this handler exists for completeness and
 * to lock the ABI before the listening server work in F4 seção d. */
int64_t sys_accept(struct syscall_frame *f) {
  int fd = (int)f->rdi;
  struct sockaddr_in *user_addr = (struct sockaddr_in *)f->rsi;
  uint32_t *user_addrlen = (uint32_t *)f->rdx;

  if (!g_net_ops || !g_net_ops->sock_accept) return -1;
  if (user_addr && !user_addrlen) return -1;
  if (user_addrlen && *user_addrlen < sizeof(struct sockaddr_in)) return -1;

  int kernel_fd = resolve_socket_fd(fd);
  if (kernel_fd < 0) return -1;

  struct sockaddr_in scratch;
  int new_kernel_fd = g_net_ops->sock_accept(kernel_fd,
                                              user_addr ? &scratch : NULL);
  if (new_kernel_fd < 0) return -1;

  struct process *proc = process_current();
  if (!proc) {
    if (g_net_ops->sock_close) (void)g_net_ops->sock_close(new_kernel_fd);
    return -1;
  }

  int proc_fd = process_fd_alloc(proc);
  if (proc_fd < 0) {
    if (g_net_ops->sock_close) (void)g_net_ops->sock_close(new_kernel_fd);
    return -1;
  }
  proc->fds[proc_fd].type = FD_TYPE_SOCKET;
  proc->fds[proc_fd].flags = 0;
  proc->fds[proc_fd].private_data = (void *)(intptr_t)new_kernel_fd;
  proc->fds[proc_fd].offset = 0;

  if (user_addr) *user_addr = scratch;
  if (user_addrlen) *user_addrlen = (uint32_t)sizeof(struct sockaddr_in);
  return (int64_t)proc_fd;
}

/* sys_connect(fd, addr, addrlen)
 *
 *   rdi = fd
 *   rsi = const struct sockaddr_in *addr
 *   rdx = addrlen
 *
 * Returns 0 on success, -1 on failure. The backend is expected to
 * block until the underlying TCP handshake completes (or fail
 * after the configured timeout); UDP returns immediately. */
int64_t sys_connect(struct syscall_frame *f) {
  int fd = (int)f->rdi;
  const struct sockaddr_in *user_addr =
      (const struct sockaddr_in *)f->rsi;
  uint32_t addrlen = (uint32_t)f->rdx;

  if (!g_net_ops || !g_net_ops->sock_connect) return -1;
  if (!user_addr) return -1;
  if (addrlen < sizeof(struct sockaddr_in)) return -1;

  int kernel_fd = resolve_socket_fd(fd);
  if (kernel_fd < 0) return -1;

  struct sockaddr_in local;
  if (copy_sockaddr_in(user_addr, &local) != 0) return -1;
  if (local.sin_family != AF_INET) return -1;

  return (int64_t)g_net_ops->sock_connect(kernel_fd, &local);
}

/* sys_send(fd, buf, len, flags)
 *
 *   rdi = fd
 *   rsi = const void *buf
 *   rdx = size_t len
 *   r10 = int flags  (passed through from SYSCALL ABI)
 *
 * Note: SYSCALL ABI puts arg index 3 in r10, but the kernel
 * `struct syscall_frame` already mirrors r10 into the rcx slot
 * (see `syscall_entry.S`). For clarity we read flags from rcx so
 * the frame layout matches every other 4-arg syscall. */
int64_t sys_send(struct syscall_frame *f) {
  int fd = (int)f->rdi;
  const void *buf = (const void *)f->rsi;
  size_t len = (size_t)f->rdx;
  int flags = (int)f->rcx;

  if (!g_net_ops || !g_net_ops->sock_send) return -1;
  if (!buf && len > 0) return -1;

  int kernel_fd = resolve_socket_fd(fd);
  if (kernel_fd < 0) return -1;

  return (int64_t)g_net_ops->sock_send(kernel_fd, buf, len, flags);
}

/* sys_recv(fd, buf, len, flags)
 *
 *   rdi = fd
 *   rsi = void *buf
 *   rdx = size_t len
 *   r10 = int flags  (mirrored into rcx by syscall_entry.S)
 *
 * Returns the number of bytes received, 0 on a clean shutdown, or
 * -1 on error. */
int64_t sys_recv(struct syscall_frame *f) {
  int fd = (int)f->rdi;
  void *buf = (void *)f->rsi;
  size_t len = (size_t)f->rdx;
  int flags = (int)f->rcx;

  if (!g_net_ops || !g_net_ops->sock_recv) return -1;
  if (!buf && len > 0) return -1;

  int kernel_fd = resolve_socket_fd(fd);
  if (kernel_fd < 0) return -1;

  return (int64_t)g_net_ops->sock_recv(kernel_fd, buf, len, flags);
}

/* sys_dns_resolve(name, out_ip, flags)
 *
 *   rdi = const char *name        (NUL-terminated DNS name)
 *   rsi = uint32_t *out_ip        (host-order IPv4 written on hit)
 *   rdx = int flags               (reserved; must be 0)
 *
 * Returns 0 on a cache hit (and *out_ip is populated), -1 on miss /
 * invalid args / no installed resolver backend / non-zero flags.
 * The kernel deliberately does NOT auto-promote a miss to an
 * active DNS query in this iteration -- the cache must already be
 * seeded (DHCP discovery / future resolver service / `dns_cache_
 * insert` from a libcapy-net DNS client). The active resolver is a
 * follow-up. The flags slot is reserved so a future iteration can
 * carry the "blocking-allowed / non-blocking-only" hint without
 * breaking ABI. */
int64_t sys_dns_resolve(struct syscall_frame *f) {
  const char *name = (const char *)f->rdi;
  uint32_t *out_ip = (uint32_t *)f->rsi;
  int flags = (int)f->rdx;

  if (!g_net_ops || !g_net_ops->dns_resolve) return -1;
  if (!name || !out_ip) return -1;
  if (flags != 0) return -1;

  return (int64_t)g_net_ops->dns_resolve(name, out_ip);
}

int64_t syscall_net_fd_read(int kernel_fd, void *buf, size_t len) {
  if (!g_net_ops || !g_net_ops->sock_recv) return -1;
  if (kernel_fd < 0) return -1;
  if (!buf && len > 0) return -1;
  return (int64_t)g_net_ops->sock_recv(kernel_fd, buf, len, 0);
}

int64_t syscall_net_fd_write(int kernel_fd, const void *buf, size_t len) {
  if (!g_net_ops || !g_net_ops->sock_send) return -1;
  if (kernel_fd < 0) return -1;
  if (!buf && len > 0) return -1;
  return (int64_t)g_net_ops->sock_send(kernel_fd, buf, len, 0);
}

void syscall_net_register_handlers(void) {
  syscall_register(SYS_SOCKET,      sys_socket);
  syscall_register(SYS_BIND,        sys_bind);
  syscall_register(SYS_LISTEN,      sys_listen);
  syscall_register(SYS_ACCEPT,      sys_accept);
  syscall_register(SYS_CONNECT,     sys_connect);
  syscall_register(SYS_SEND,        sys_send);
  syscall_register(SYS_RECV,        sys_recv);
  syscall_register(SYS_DNS_RESOLVE, sys_dns_resolve);
}
