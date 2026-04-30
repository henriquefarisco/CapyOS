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
