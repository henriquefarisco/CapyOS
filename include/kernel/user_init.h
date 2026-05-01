#ifndef KERNEL_USER_INIT_H
#define KERNEL_USER_INIT_H

#include <stddef.h>

struct process;

/* Result codes for `kernel_spawn_embedded_hello`. Encoded as
 * negative integers so the caller can distinguish failure modes
 * via simple comparisons. */
enum kernel_spawn_result {
  KERNEL_SPAWN_OK             = 0,
  KERNEL_SPAWN_BAD_ELF        = -1,
  KERNEL_SPAWN_NO_PROCESS     = -2,
  KERNEL_SPAWN_LOAD_FAILED    = -3,
};

/* Creates a new `process` named "hello", validates the embedded
 * `hello.elf` blob (see `kernel/embedded_hello.h`), and loads its
 * segments into the process address space via
 * `elf_load_into_process`. After this call the caller MAY invoke
 * `process_enter_user_mode(*out_proc)` to drop into Ring 3.
 *
 * On any failure the process slot may be left in PROC_STATE_EMBRYO
 * with no segments mapped; phase 6 (process_destroy / wait) will
 * own the cleanup contract. The host tests for phase 5c lock only
 * the success path and the input-validation failures; hardening
 * the partial-failure path is deferred to phase 6.
 *
 * Returns one of `enum kernel_spawn_result`. On `KERNEL_SPAWN_OK`,
 * `*out_proc` (if non-NULL) is set to the freshly created process.
 * On any non-zero return, `*out_proc` is left untouched. */
int kernel_spawn_embedded_hello(struct process **out_proc);

/* Convenience wrapper for the boot path (M4 phase 5d): calls
 * `kernel_spawn_embedded_hello` and, on success, calls
 * `process_enter_user_mode(p)` which is `noreturn`.
 *
 * This function therefore returns ONLY when the spawn fails. The
 * return value is the negative `enum kernel_spawn_result` produced
 * by the spawn helper, so the caller can decide whether to log,
 * panic, or fall through to the kernel shell. On success, the
 * function does not return and the boot CPU enters Ring 3.
 *
 * The kernel_main wiring guards the call with `#ifdef
 * CAPYOS_BOOT_RUN_HELLO`; default builds skip it so the regular
 * shell stays reachable. The QEMU smoke for phase 5d defines that
 * macro on the cross-compiler command line. */
int kernel_boot_run_embedded_hello(void);

/* M4 phase 8f.5: spawn TWO copies of the embedded hello binary,
 * arm each via the synthetic IRET frame builder so they are
 * dispatched through the scheduler (NOT via the boot iretq), and
 * iretq into the FIRST one to bootstrap ring 3. The second is
 * naturally entered by the scheduler at the first quantum
 * exhaustion via `x64_user_first_dispatch`.
 *
 * Each copy receives a distinct `rank` value (0 or 1) in RAX,
 * which capylibc's crt0 forwards to main() so the BUSY arm of
 * hello emits a per-rank marker ([busyU0] or [busyU1]). The
 * `smoke-x64-preemptive-user-2task` harness asserts BOTH markers
 * appear at least N times in the debugcon log -- the canonical
 * end-to-end proof of ring-3 preemption.
 *
 * On allocation/load failure of either copy this function
 * destroys whatever was built and returns the failing
 * `enum kernel_spawn_result` so the caller can fall back to the
 * shell. On success the function does NOT return: ring 3 is
 * entered for the first task and the scheduler takes over. */
int kernel_boot_run_two_busy_users(void);

/* M5 phase E.5: spawn the embedded `/bin/capysh` interactive shell
 * as the boot init process and drop into ring 3.
 *
 * Behaviour mirrors `kernel_boot_run_embedded_hello`: validates the
 * blob, builds a fresh process named "capysh", loads the ELF into
 * its address space, then `process_enter_user_mode`s into it. The
 * function returns only on failure; on success it is `noreturn`.
 *
 * Resolution path: looks `/bin/capysh` up against the
 * embedded_progs registry rather than calling
 * `embedded_hello_data()` directly, so the same code path picks up
 * any future shell binary swapped into the registry. */
int kernel_boot_run_capysh(void);

#endif /* KERNEL_USER_INIT_H */
