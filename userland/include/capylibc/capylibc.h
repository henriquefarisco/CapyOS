#ifndef CAPYLIBC_CAPYLIBC_H
#define CAPYLIBC_CAPYLIBC_H

/* CapyOS minimal C library - public API.
 *
 * This is the C surface that user binaries (under userland/bin/...)
 * link against to invoke kernel services. The library is static
 * (libcapylibc.a), built with the same x86_64 toolchain as the kernel
 * but linked into Ring 3 ELF executables. The CapyOS syscall ABI is
 * an exact subset of the SysV x86_64 syscall ABI:
 *
 *   - %rax holds the syscall number (one of `SYS_*` from
 *     include/kernel/syscall_numbers.h)
 *   - %rdi, %rsi, %rdx, %r10, %r8, %r9 hold up to 6 arguments
 *     (arg index 3 lives in %r10, NOT %rcx, because the SYSCALL
 *     instruction itself clobbers %rcx)
 *   - The 64-bit return value comes back in %rax
 *   - %rcx and %r11 are clobbered by SYSCALL/SYSRET
 *
 * The actual reshuffling from the C calling convention into this
 * register layout is done by per-syscall stubs in
 * userland/lib/capylibc/syscall_stubs.S. tests/test_capylibc_abi.c
 * statically asserts that the syscall numbers used by capylibc
 * agree with the kernel's syscall_numbers.h. */

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Process control. */
void capy_exit(int status) __attribute__((noreturn));
int capy_getpid(void);
int capy_getppid(void);

/* M5 phase A.4: split the calling process into two via SYS_FORK.
 *
 * Returns the child's PID (>0) in the parent and 0 in the child.
 * Returns -1 on failure (no process slot, OOM, kernel-thread caller).
 *
 * The child inherits a CoW clone of the parent's address space (M4
 * phase 7c): both branches share writable pages as RO+COW until the
 * next write triggers a page fault that materialises a private copy.
 * Both branches resume at the instruction immediately after the call
 * to `capy_fork` with identical callee-saved register state; only
 * RAX (the return value) differs. */
int capy_fork(void);

/* M5 phase B.5: replace the calling process's address space with
 * the image resolved from `path` against the in-kernel embedded
 * binaries registry, then jump to its entry point.
 *
 * Returns -1 on failure (NULL path, registry miss, ELF validation
 * failure, OOM). Does NOT return on success: the kernel rewrites
 * the SYSCALL return frame so sysret lands at the new image's
 * `_start` with a fresh user RSP. From the C calling convention's
 * point of view the stub is `noreturn` on the success branch, but
 * we deliberately don't tag it as such so failure can still return
 * a value the caller can branch on.
 *
 * `argv` is reserved for a future phase (argv-on-stack packing);
 * pass NULL today. */
int capy_exec(const char *path, const char **argv);

/* M5 phase C.3: block until the child process `pid` exits; write
 * its exit code through `status` (NULL = ignore) and return the
 * reaped pid. Returns -1 if `pid` does not name a valid process
 * slot or there is no current process. The caller must be the
 * parent of `pid`; cross-tree wait is intentionally not modelled. */
int capy_wait(unsigned int pid, int *status);

/* M5 phase D: create a unidirectional kernel pipe. On success
 * `fds[0]` receives the read end and `fds[1]` the write end, both
 * inheritable across `capy_fork`. Returns 0 on success, -1 on
 * failure (NULL fds, kernel pipe table full, FD table full).
 *
 * Read semantics on `fds[0]`: blocks until at least 1 byte is
 * available; returns 0 on EOF (write end closed and buffer drained).
 * Write semantics on `fds[1]`: blocks until at least 1 byte fits;
 * returns -1 on broken pipe (read end closed). */
int capy_pipe(int fds[2]);

/* F4 seção c parte 2/2 (2026-05-08): close a userland file
 * descriptor. Dispatches inside the kernel by `FD_TYPE_*`:
 * VFS files go through `vfs_close`, pipe ends through
 * `pipe_close_read` / `pipe_close_write`, sockets through the
 * registered net close hook. Returns 0 on success, -1 on failure
 * (out-of-range fd, slot already free). */
int capy_close(int fd);

/* I/O. The kernel currently honours fd 0 (stdin) / 1 (stdout) / 2
 * (stderr) as the only valid file descriptors for a fresh user
 * process; anything else returns -1 until the VFS is wired up to
 * user space. */
long capy_write(int fd, const void *buf, size_t len);
long capy_read(int fd, void *buf, size_t len);

/* Scheduling cooperation. */
void capy_yield(void);
void capy_sleep(unsigned long ticks);

/* Time. Returns ticks since boot; the unit follows
 * `apic_timer_ticks()` (100 Hz today). */
long capy_time(void);

/* F4 seção c (2026-05-08) -- userland socket family.
 *
 * Thin wrappers around `SYS_SOCKET`..`SYS_RECV` (28..34). The
 * kernel-side handlers live in `src/kernel/syscall_net.c` and are
 * dispatched through an injectable backend (default = the in-kernel
 * `socket_*` family in `src/net/services/socket.c`). All entries
 * return `-1` either when an argument is invalid or when the
 * backend has not been installed yet (e.g. host unit-test harness
 * without a fake registered).
 *
 * A future libcapy-net layer (F4 seção d) will translate the `-1`
 * return into a Linux-style errno; today the wrappers are just
 * pass-throughs.
 *
 * Forward-declarations for `struct sockaddr_in` are mirrored from
 * `include/net/socket.h` so user binaries don't need to pull in a
 * kernel header. The on-the-wire layout MUST match `struct
 * sockaddr_in` in `include/net/socket.h` byte-for-byte; a host
 * regression test pins this in F4 seção d. */
struct capy_sockaddr_in {
  uint16_t sin_family;
  uint16_t sin_port;
  uint32_t sin_addr;
  uint8_t  sin_zero[8];
};

#define CAPY_AF_INET     2
#define CAPY_SOCK_STREAM 1
#define CAPY_SOCK_DGRAM  2

int  capy_socket(int domain, int type, int protocol);
int  capy_bind(int fd, const struct capy_sockaddr_in *addr,
               unsigned int addrlen);
int  capy_listen(int fd, int backlog);
int  capy_accept(int fd, struct capy_sockaddr_in *addr,
                 unsigned int *addrlen);
int  capy_connect(int fd, const struct capy_sockaddr_in *addr,
                  unsigned int addrlen);
long capy_send(int fd, const void *buf, size_t len, int flags);
long capy_recv(int fd, void *buf, size_t len, int flags);

/* F4 seção c parte 3/3 (2026-05-08) -- hostname → IPv4 lookup
 * via the kernel-side DNS cache. Returns 0 on a cache hit and
 * writes the resolved IP host-order into `*out_ip`; returns -1
 * on cache miss / NULL args / non-zero `flags` / no installed
 * resolver backend.
 *
 * Callers should pass `flags == 0`; the slot is reserved so a
 * future iteration can carry "blocking-allowed / non-blocking
 * only" without breaking ABI. The kernel does NOT auto-promote a
 * miss to an active DNS query in this iteration -- the cache is
 * seeded by DHCP discovery + (future) libcapy-net DNS client. */
int capy_dns_resolve(const char *name, uint32_t *out_ip, int flags);

/* Etapa 5 / Slice 5.1: fill `buf` with up to 256 bytes of CSPRNG
 * entropy from the kernel (backed by the audited in-tree csprng).
 * Returns the number of bytes written (0..min(len, 256)), or -1 on a
 * NULL buffer with len > 0 / non-zero `flags`. `flags` is reserved and
 * must be 0. Callers needing more than the per-call cap loop. This is
 * the userland entropy source libcapy-tls uses to seed BearSSL's
 * DRBG (Etapa 5 prerequisite). */
long capy_getrandom(void *buf, size_t len, unsigned int flags);

/* Etapa 5: wall-clock seconds since the Unix epoch, from the kernel RTC.
 * Distinct from capy_time(), which returns APIC ticks since boot. This is
 * real calendar time, required by libcapy-tls to evaluate X.509
 * certificate validity (notBefore/notAfter) in ring 3. */
long capy_clock_realtime(void);

/* Etapa 6 / Slice 6.7: active session UI language code (CAPY_SESSION_LANG_*
 * from kernel/syscall_numbers.h: 0=pt-BR, 1=en, 2=es). Lets a ring-3 app
 * (CapyBrowse Text) localize its user-facing diagnostics to the logged-in
 * user's language instead of a hardcoded base. No session -> pt-BR (the
 * selection default). */
long capy_get_session_lang(void);

#ifdef __cplusplus
}
#endif

#endif /* CAPYLIBC_CAPYLIBC_H */
