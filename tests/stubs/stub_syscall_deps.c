/*
 * tests/stub_syscall_deps.c (2026-05-02)
 *
 * Host-side stubs for symbols referenced by `src/kernel/syscall.c`
 * but not exercised by `tests/test_syscall_pipe_priority.c`. The
 * test only drives `sys_read` and `sys_write`, but the whole TU
 * gets linked, so the linker still needs every external symbol
 * that ANY handler in the same TU references.
 *
 * Each stub is intentionally conservative -- it returns the
 * "operation not supported / not found" code that makes sense for
 * the symbol's contract. If a future test exercises one of these
 * paths, the stub should be replaced with a real fake.
 */

#include "fs/vfs.h"
#include <stddef.h>
#include <stdint.h>

/* sys_open. Returns NULL so SYS_OPEN paths never succeed in tests. */
struct file *vfs_open(const char *path, uint32_t flags) {
    (void)path;
    (void)flags;
    return NULL;
}

/* sys_unlink. */
int vfs_unlink(const char *path) {
    (void)path;
    return -1;
}

/* sys_rmdir. */
int vfs_rmdir(const char *path) {
    (void)path;
    return -1;
}

/* sys_time. The host build cannot read the real APIC; return 0. */
uint64_t apic_timer_ticks(void) {
    return 0u;
}

/* syscall_init_msr lives in arch-specific assembly (`syscall_msr.S`).
 * The host build never enables MSRs; provide an empty shim so the
 * linker is satisfied. The real syscall_init() in syscall.c calls
 * this at the very end and the test does NOT invoke syscall_init(). */
void syscall_init_msr(void) {
    /* no-op on host */
}
