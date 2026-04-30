/*
 * Glue between the embedded `hello.elf` blob and the existing
 * process / ELF loader machinery. Phase 5c builds the helper but
 * does NOT call it from `kernel_main`; phase 5d will wire the boot
 * flow and add a QEMU smoke that asserts "hello, capyland" reaches
 * the debug console.
 *
 * The control flow is intentionally linear so the host test
 * (tests/test_user_init.c) can lock each branch via stubs.
 *
 *  1. Read the (data, size) pair from `embedded_hello_*`.
 *  2. Pre-validate via `elf_validate` so a corrupted blob fails
 *     with `KERNEL_SPAWN_BAD_ELF` before we burn a process slot.
 *  3. `process_create("hello", 0, 0)` allocates the address space.
 *  4. `elf_load_into_process` maps the segments and primes the
 *     main thread's RIP/RSP. On any failure we return
 *     `KERNEL_SPAWN_LOAD_FAILED`; phase 6 will reclaim the slot.
 *  5. The caller decides when to call `process_enter_user_mode`.
 */
#include "kernel/user_init.h"
#include "kernel/process.h"
#include "kernel/elf_loader.h"
#include "kernel/embedded_hello.h"

int kernel_spawn_embedded_hello(struct process **out_proc) {
  const uint8_t *data = (const uint8_t *)embedded_hello_data();
  size_t size = embedded_hello_size();

  if (elf_validate(data, size) != 0) return KERNEL_SPAWN_BAD_ELF;

  struct process *p = process_create("hello", 0, 0);
  if (!p) return KERNEL_SPAWN_NO_PROCESS;

  if (elf_load_into_process(p, data, size) != 0) {
    /* M4 phase 6 closes the embryo-slot leak documented in the
     * original phase 5c TODO: tear down the half-built process so
     * the table slot, address space, and (eventual) main thread
     * are reclaimed before we return to the boot path. */
    process_destroy(p);
    return KERNEL_SPAWN_LOAD_FAILED;
  }

  if (out_proc) *out_proc = p;
  return KERNEL_SPAWN_OK;
}

int kernel_boot_run_embedded_hello(void) {
  struct process *p = NULL;
  int rc = kernel_spawn_embedded_hello(&p);
  if (rc != KERNEL_SPAWN_OK || !p) {
    /* Caller logs and falls through to the kernel shell. */
    return rc;
  }
  /* `process_enter_user_mode` is __attribute__((noreturn)) on the
   * success path: it iretq's into Ring 3 and execution does not
   * come back to this function. The defensive `return -1` below
   * exists only so the compiler does not assume a fall-through
   * path when the implementation is stubbed out under UNIT_TEST or
   * if a future refactor accidentally drops the noreturn. */
  process_enter_user_mode(p);
  return -1;
}
