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

#endif /* KERNEL_USER_INIT_H */
