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
#ifdef CAPYOS_HELLO_EXEC
/* M5 phase B.7: SYS_EXEC end-to-end smoke body.
 *
 * Emits `[before-exec]` once via SYS_WRITE so the smoke can prove
 * the original `hello` image actually ran in ring 3, then calls
 * `capy_exec("/bin/exectarget", NULL)`. On success the kernel
 * never returns to this stub: it rewrites the syscall frame so
 * sysret lands on `exectarget`'s `_start`, which writes
 * `[exec-ok]` and exits cleanly. If capy_exec returns at all we
 * emit `[exec-failed]` so the smoke flags it as a regression. */
static const char k_before_exec_marker[] = "[before-exec]\n";
static const char k_exec_failed_marker[] = "[exec-failed]\n";
#endif
#ifdef CAPYOS_HELLO_FORK_CRASH
/* M5 phase F smoke body: validate that a user-mode fault in a
 * forked child is isolated to that child, the parent's wait()
 * observes a signal-style exit code (>= 128), and the kernel
 * survives without panic.
 *
 * Flow:
 *   parent: pid = capy_fork()
 *   child:  capy_write([child-pre-fault]); *(int*)1 = 0xDEAD; (#PF)
 *   parent: capy_wait(pid, &status);
 *           if (status >= 128) capy_write([parent-saw-crash])
 *           else                capy_write([parent-saw-clean-exit])
 *
 * The kill path in `x64_exception_dispatch` calls process_exit with
 * code 128+vector (vector 14 for #PF -> 142). The parent's wait()
 * unblocks because process_exit flipped the child's PROC_STATE to
 * ZOMBIE.
 *
 * Markers:
 *   [child-pre-fault]        proves the child reached ring 3.
 *   [parent-saw-crash]       proves end-to-end isolation.
 *   [parent-saw-clean-exit]  regression: no fault was taken.
 *   [fc-fork-fail]           capy_fork failed; smoke setup error. */
static const char k_fc_child_pre_fault[]  = "[child-pre-fault]\n";
static const char k_fc_parent_crash_m[]   = "[parent-saw-crash]\n";
static const char k_fc_parent_clean_m[]   = "[parent-saw-clean-exit]\n";
static const char k_fc_fork_fail_m[]      = "[fc-fork-fail]\n";
#endif
#ifdef CAPYOS_HELLO_PIPE
/* M5 phase D smoke body: validate SYS_PIPE + fork inheritance +
 * pipe_read/pipe_write block-on-empty/full semantics.
 *
 * Flow:
 *   parent: capy_pipe(fds); pid = capy_fork()
 *   child:  capy_write(fds[1], "ping", 4); capy_exit(0);
 *   parent: capy_read(fds[0], buf, 4); compare buf == "ping";
 *           capy_wait(pid, NULL);
 *           capy_write([pipe-ok] or [pipe-bad-payload])
 *
 * Markers:
 *   [pipe-ok]            success: parent read what child wrote.
 *   [pipe-bad-payload]   parent read != "ping" (regression).
 *   [pipe-fail-create]   capy_pipe returned -1.
 *   [pipe-fail-fork]     capy_fork returned -1. */
static const char k_pipe_ok_marker[]      = "[pipe-ok]\n";
static const char k_pipe_bad_payload_m[]  = "[pipe-bad-payload]\n";
static const char k_pipe_fail_create_m[]  = "[pipe-fail-create]\n";
static const char k_pipe_fail_fork_m[]    = "[pipe-fail-fork]\n";
#endif
#ifdef CAPYOS_HELLO_FORKWAIT
/* M5 phase C.5: SYS_FORK + SYS_WAIT + SYS_EXIT end-to-end smoke.
 *
 * Flow:
 *   parent:  pid = capy_fork()
 *   child:   capy_write([child-running]); capy_exit(42);
 *   parent:  capy_wait(pid, &status); compare status==42;
 *            capy_write([parent-reaped] or [parent-bad-status]);
 *            capy_exit(0)
 *
 * The smoke harness asserts BOTH markers appear AND the
 * bad-status marker is absent. The parent's [parent-reaped]
 * appearing after [child-running] is implicit: capy_wait blocks
 * until the child becomes a zombie, so the kernel must have
 * processed SYS_EXIT (which drives process_exit -> ZOMBIE state)
 * before sys_wait can return. */
static const char k_fw_child_marker[]    = "[child-running]\n";
static const char k_fw_reaped_marker[]   = "[parent-reaped]\n";
static const char k_fw_bad_status_mark[] = "[parent-bad-status]\n";
static const char k_fw_fork_fail_mark[]  = "[fw-fork-fail]\n";
#endif
#ifdef CAPYOS_HELLO_FORK
/* M5 phase A.7: SYS_FORK + CoW end-to-end smoke body.
 *
 * The user binary calls capy_fork(); the parent gets the child PID
 * and the child gets 0. Each branch writes a distinct, bracketed
 * marker in a tight loop. The smoke harness (smoke_x64_fork_cow.py)
 * asserts BOTH markers appear at least N times in the debugcon log
 * AND that no kernel panic surfaces.
 *
 * Why this proves CoW works end-to-end:
 *   - Both branches write to local stack variables (the marker
 *     pointer / length below). With CoW: the first write from the
 *     child triggers a #PF with U=1, P=1, W=1, the kernel sees the
 *     shared frame is RO+COW with refcount > 1, allocates a private
 *     copy and flips the PTE writable. Without CoW (or with a broken
 *     decision matrix) the parent's stack would be silently shared
 *     and the smoke would either panic or scramble both markers.
 *   - The capy_fork stub spills callee-saved regs to the user stack
 *     before SYSCALL and restores them after. Both branches must pop
 *     identical values; if the child's stack page didn't CoW
 *     correctly, the values would mismatch and the smoke would
 *     observe garbled output or a fault. */
static const char k_fork_parent_marker[] = "[fork-parent]\n";
static const char k_fork_child_marker[]  = "[fork-child]\n";
static const char k_fork_fail_marker[]   = "[fork-fail]\n";
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
#elif defined(CAPYOS_HELLO_EXEC)
    (void)rank;
    capy_write(1, k_before_exec_marker,
               sizeof(k_before_exec_marker) - 1);

    /* On success the kernel rewrites our syscall return frame and
     * we never come back from this call. */
    int rc = capy_exec("/bin/exectarget", (const char **)0);

    /* Reachable only if capy_exec failed (lookup miss, ELF error,
     * OOM). The smoke matches [exec-failed] as a regression. */
    (void)rc;
    capy_write(1, k_exec_failed_marker,
               sizeof(k_exec_failed_marker) - 1);
    return 1;
#elif defined(CAPYOS_HELLO_FORK_CRASH)
    (void)rank;
    int pid = capy_fork();
    if (pid < 0) {
        capy_write(1, k_fc_fork_fail_m, sizeof(k_fc_fork_fail_m) - 1);
        return 1;
    }
    if (pid == 0) {
        /* Child: emit pre-fault marker, then dereference a NULL-ish
         * address. The kernel's fault classifier routes user-mode
         * #PF -> KILL_PROCESS and process_exit(128+14). The child
         * never returns from this store. */
        capy_write(1, k_fc_child_pre_fault,
                   sizeof(k_fc_child_pre_fault) - 1);
        volatile int *bad = (volatile int *)0x1;
        *bad = 0xDEADu;
        /* Unreachable. */
        capy_exit(0);
    }
    /* Parent: blocks until the child becomes a zombie. status
     * carries the exit code written by the kill path
     * (128 + vector). On x86_64, vector 14 is #PF, so we expect
     * status == 142. We accept any value >= 128 as "died by
     * signal" to keep the assertion robust against future
     * vectoring changes (e.g. if NULL-deref starts surfacing as
     * #GP on some configurations). */
    int status = 0;
    capy_wait((unsigned int)pid, &status);
    if (status >= 128) {
        capy_write(1, k_fc_parent_crash_m,
                   sizeof(k_fc_parent_crash_m) - 1);
    } else {
        capy_write(1, k_fc_parent_clean_m,
                   sizeof(k_fc_parent_clean_m) - 1);
    }
    return 0;
#elif defined(CAPYOS_HELLO_PIPE)
    (void)rank;
    int fds[2] = {-1, -1};
    if (capy_pipe(fds) != 0) {
        capy_write(1, k_pipe_fail_create_m,
                   sizeof(k_pipe_fail_create_m) - 1);
        return 1;
    }
    int pid = capy_fork();
    if (pid < 0) {
        capy_write(1, k_pipe_fail_fork_m,
                   sizeof(k_pipe_fail_fork_m) - 1);
        return 1;
    }
    if (pid == 0) {
        /* Child: write "ping" through the pipe and exit. */
        capy_write(fds[1], "ping", 4);
        capy_exit(0);
    }
    /* Parent: blocking read drains "ping" once the child runs. */
    char buf[4] = {0, 0, 0, 0};
    long rd = capy_read(fds[0], buf, 4);
    int payload_ok = (rd == 4 && buf[0] == 'p' && buf[1] == 'i' &&
                      buf[2] == 'n' && buf[3] == 'g');
    capy_wait((unsigned int)pid, (int *)0);
    if (payload_ok) {
        capy_write(1, k_pipe_ok_marker,
                   sizeof(k_pipe_ok_marker) - 1);
    } else {
        capy_write(1, k_pipe_bad_payload_m,
                   sizeof(k_pipe_bad_payload_m) - 1);
    }
    return 0;
#elif defined(CAPYOS_HELLO_FORKWAIT)
    (void)rank;
    int pid = capy_fork();
    if (pid < 0) {
        capy_write(1, k_fw_fork_fail_mark,
                   sizeof(k_fw_fork_fail_mark) - 1);
        return 1;
    }
    if (pid == 0) {
        /* Child: marker + exit(42). capy_exit is noreturn. */
        capy_write(1, k_fw_child_marker, sizeof(k_fw_child_marker) - 1);
        capy_exit(42);
    }
    /* Parent: block on the child, validate status, marker + exit. */
    int status = 0;
    int rc = capy_wait((unsigned int)pid, &status);
    (void)rc;
    if (status == 42) {
        capy_write(1, k_fw_reaped_marker,
                   sizeof(k_fw_reaped_marker) - 1);
    } else {
        capy_write(1, k_fw_bad_status_mark,
                   sizeof(k_fw_bad_status_mark) - 1);
    }
    return 0;
#elif defined(CAPYOS_HELLO_FORK)
    (void)rank;
    int pid = capy_fork();
    if (pid < 0) {
        capy_write(1, k_fork_fail_marker, sizeof(k_fork_fail_marker) - 1);
        return 1;
    }

    /* Both branches loop forever emitting their respective marker.
     * The marker pointer is selected from a stack-local based on the
     * fork return value, which is the canonical "child writes to its
     * own stack page" CoW trigger. */
    const char *marker;
    size_t marker_len;
    if (pid == 0) {
        marker = k_fork_child_marker;
        marker_len = sizeof(k_fork_child_marker) - 1u;
    } else {
        marker = k_fork_parent_marker;
        marker_len = sizeof(k_fork_parent_marker) - 1u;
    }
    for (;;) {
        for (volatile uint64_t spin = 0; spin < 0x80000ULL; ++spin) {
            __asm__ volatile("pause");
        }
        capy_write(1, marker, marker_len);
    }
    return 0;
#else
    (void)rank;
    /* sizeof - 1 strips the implicit NUL terminator that the kernel
     * console does not need to emit. */
    capy_write(1, k_msg, sizeof(k_msg) - 1);
    return 0;
#endif
}
