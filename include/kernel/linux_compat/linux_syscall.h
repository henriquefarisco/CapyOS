#ifndef KERNEL_LINUX_COMPAT_LINUX_SYSCALL_H
#define KERNEL_LINUX_COMPAT_LINUX_SYSCALL_H

#include <stdint.h>
#include <stddef.h>
#include "kernel/linux_compat/linux_syscall_nrs.h"

/* Linux-ABI syscall dispatcher (S1.1).
 *
 * This is the entry point that the future Linux-mode trap handler
 * will call after decoding `rax = syscall number`. It is a pure
 * table-driven dispatch: the table is populated incrementally as
 * each S1.x task lands, and unimplemented slots return
 * `-LINUX_ENOSYS` so userland sees the canonical Linux error code.
 *
 * Layering:
 *   - linux_syscall_init()       called once from kernel_main, after
 *                                linux_clock_init_boot. Idempotent.
 *                                Each S1.x module registers handlers
 *                                in its own `_init` callback.
 *   - linux_syscall_register()   called by S1.x init code at boot.
 *                                Refuses to overwrite an installed
 *                                handler (returns -1) so two modules
 *                                competing for the same NR fail
 *                                loudly at boot rather than silently.
 *   - linux_syscall_dispatch()   the hot path. Bounds-check + lookup
 *                                + indirect call. Defaults to
 *                                -ENOSYS.
 *
 * Argument convention: x86_64 Linux uses
 *   rdi, rsi, rdx, r10, r8, r9
 * for the 6 syscall arguments. Callers pre-pack the args into a
 * `linux_syscall_args` struct so the dispatcher does not have to know
 * the trap frame layout. Each handler unpacks the fields it cares
 * about. Return convention: int64_t -- negative is `-errno`,
 * non-negative is success.
 *
 * Test hooks: `linux_syscall_reset_for_tests()` clears the table so
 * unit tests can install scenario-specific handlers without
 * cross-test contamination.
 */

struct linux_syscall_args {
    uint64_t a0;  /* rdi */
    uint64_t a1;  /* rsi */
    uint64_t a2;  /* rdx */
    uint64_t a3;  /* r10 */
    uint64_t a4;  /* r8  */
    uint64_t a5;  /* r9  */
};

typedef int64_t (*linux_syscall_fn)(const struct linux_syscall_args *args);

/* Initialise the dispatcher. Calls each registered S1.x module's
 * `_init` hook (declared as weak by this module, defined by the
 * module if compiled in, otherwise no-op). Idempotent. */
void linux_syscall_init(void);

/* Install a handler for `nr`. Returns 0 on success, -1 if `nr` is
 * out of range or already has a handler. */
int linux_syscall_register(uint32_t nr, linux_syscall_fn handler);

/* Look up a handler. Returns the registered function pointer, or
 * NULL if `nr` is out of range or unregistered. Test-friendly. */
linux_syscall_fn linux_syscall_lookup(uint32_t nr);

/* Number of registered handlers (test-friendly observability). */
size_t linux_syscall_registered_count(void);

/* Hot path: dispatch a Linux syscall. Returns the handler's return
 * value, or -LINUX_ENOSYS for unregistered/unknown NRs. NULL `args`
 * yields -LINUX_EFAULT. */
int64_t linux_syscall_dispatch(uint32_t nr,
                               const struct linux_syscall_args *args);

/* Test-only: clear all registrations and reset init flag. */
void linux_syscall_reset_for_tests(void);

#endif /* KERNEL_LINUX_COMPAT_LINUX_SYSCALL_H */
