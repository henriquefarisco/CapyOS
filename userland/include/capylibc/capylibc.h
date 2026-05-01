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

#ifdef __cplusplus
}
#endif

#endif /* CAPYLIBC_CAPYLIBC_H */
