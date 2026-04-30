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
#ifdef CAPYOS_HELLO_BUSY
/* M4 phase 8f.3: marker emitted in a loop so the smoke can prove
 * that an APIC tick fired from ring 3 does NOT crash the kernel
 * (TSS / RSP0 path works) and that iretq correctly returns to user
 * mode after the tick is serviced. The bracketed prefix matches the
 * convention used by the kernel-mode preemption demo (phase 8e).
 *
 * M4 phase 8f.5: when the kernel spawns TWO copies of this binary
 * via kernel_boot_run_two_busy_users, each copy receives a distinct
 * `rank` value as its first main() argument (rank 0 -> [busyU0],
 * rank 1 -> [busyU1]). The smoke harness asserts BOTH markers
 * appear at least N times to prove ring-3 preemption swaps actually
 * resume each task. Single-task builds (phase 8f.3) see rank=0 and
 * emit only [busyU0]. */
static const char k_busy_marker_0[] = "[busyU0]\n";
static const char k_busy_marker_1[] = "[busyU1]\n";
#endif

int main(int rank) {
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
#elif defined(CAPYOS_HELLO_BUSY)
    /* M4 phase 8f.3 / 8f.5: ring-3 preemption smoke body.
     *
     * Loops forever, emitting a marker every N busy iterations.
     * The smoke harness asserts the marker appears at least
     * BUSY_MIN times within the wall-clock window. With APIC
     * armed at 100Hz and a multi-second timeout the user task is
     * guaranteed to take many ticks; if any of them crashed the
     * kernel (TSS missing, RSP0 wrong, iretq mis-staged) the
     * marker would simply stop appearing.
     *
     * The `rank` argument is non-zero only when the kernel spawned
     * two copies of this binary; rank 0 emits [busyU0] and rank 1
     * emits [busyU1]. */
    const char *marker =
        (rank == 0) ? k_busy_marker_0 : k_busy_marker_1;
    size_t marker_len =
        (rank == 0) ? sizeof(k_busy_marker_0) - 1u
                    : sizeof(k_busy_marker_1) - 1u;
    for (;;) {
        for (volatile uint64_t spin = 0; spin < 0x80000ULL; ++spin) {
            __asm__ volatile("pause");
        }
        capy_write(1, marker, marker_len);
    }
    /* Unreachable: the loop is intentionally infinite so the smoke
     * has an unlimited window of opportunity to observe the marker.
     * The kernel-side scheduler will eventually preempt this task
     * out via the APIC tick and back via iretq, exactly the path
     * the smoke is validating. */
    return 0;
#else
    (void)rank;
    /* sizeof - 1 strips the implicit NUL terminator that the kernel
     * console does not need to emit. */
    capy_write(1, k_msg, sizeof(k_msg) - 1);
    return 0;
#endif
}
