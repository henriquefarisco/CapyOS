/*
 * userland/bin/hello/main.c - the first CapyOS user-space program.
 *
 * Goal: drop into Ring 3, issue exactly one SYS_WRITE to stdout, then
 * SYS_EXIT cleanly. The whole binary is statically linked against
 * libcapylibc (crt0 + syscall stubs) and embedded into the kernel
 * image at link time (M4 phase 5c wires that step).
 *
 * The kernel side smoke (M4 phase 5e QEMU job) verifies three things
 * in one boot:
 *   1. enter_user_mode actually drops a CPL=3 task on the CPU
 *      (phase 3.5 contract).
 *   2. The SYSCALL/SYSRET path round-trips a real user syscall
 *      (phase 3 + phase 5a contracts).
 *   3. The fault classifier does NOT fire (phase 4 contract: a
 *      well-behaved user binary returns to the scheduler instead
 *      of being killed).
 *
 * Phase 5f adds a CAPYOS_HELLO_FAULT compile-time toggle that
 * deliberately segfaults after a marker write so a parallel QEMU
 * smoke (`make smoke-x64-hello-segfault`) can validate phase 4's
 * kill path end-to-end. The default build (no toggle) is unchanged.
 *
 * Style:
 *   - No globals, no allocations, no uninitialised data: the kernel
 *     loader does not yet zero a .bss segment, so we keep `hello`
 *     to .text + .rodata only.
 *   - The success message length is computed at compile time so the
 *     binary stays self-contained (no strlen, no libc).
 */
#include <capylibc/capylibc.h>

static const char k_msg[] = "hello, capyland\n";
#ifdef CAPYOS_HELLO_FAULT
static const char k_fault_marker[] = "before-fault\n";
#endif

int main(void) {
#ifdef CAPYOS_HELLO_FAULT
    /* Phase 5f smoke: verify the fault-kill path end-to-end.
     *
     * Step 1: emit a marker the QEMU harness can match. Its
     * presence proves that enter_user_mode dropped to Ring 3 and
     * the SYSCALL path round-tripped at least once before the
     * deliberate fault, distinguishing a kernel-side spawn bug
     * from a user-side fault.
     *
     * Step 2: dereference NULL. The CPU raises #PF with U=1, P=0,
     * W=1. arch_fault_classify() returns ARCH_FAULT_RECOVERABLE;
     * `vmm_handle_page_fault` (a stub today) refuses recovery; the
     * dispatcher escalates to KILL_PROCESS via process_exit(128+14).
     *
     * The smoke verifies:
     *   - "before-fault" appears (binary entered Ring 3),
     *   - "[x64] User-mode fault, killing offending process"
     *     appears (kill path triggered),
     *   - "Page Fault" appears (correct exception identified),
     *   - "panic" does NOT appear (kernel did not die).
     */
    capy_write(1, k_fault_marker, sizeof(k_fault_marker) - 1);

    volatile int *bad = (volatile int *)0;
    *bad = 0xDEADBEEF;

    /* Unreachable on a working kernel; kept so the compiler does
     * not flag a noreturn path. */
    return 1;
#else
    /* sizeof - 1 strips the implicit NUL terminator that the kernel
     * console does not need to emit. */
    capy_write(1, k_msg, sizeof(k_msg) - 1);
    return 0;
#endif
}
