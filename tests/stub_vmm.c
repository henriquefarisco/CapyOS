/*
 * Host-side stub for the virtual memory manager and the ELF loader.
 *
 * The real src/memory/vmm.c uses x86_64 inline asm (movq cr3, ...) that
 * cannot be linked into the host unit-test binary. The kernel modules
 * exercised by the test suite (currently just src/kernel/process.c)
 * need only the create/destroy address-space pair from VMM and a stub
 * for the ELF loader so process.c links cleanly. We provide both here
 * with a tiny calloc/free implementation. Tests never dereference the
 * address-space contents and never call process_exec; they just need
 * non-NULL handles that round-trip through process_create / wait and
 * a symbol resolution for elf_load_from_file.
 *
 * If a future test starts touching mapping/RSS or actual ELF loading,
 * extend this stub (and never the production file).
 */
#include "memory/vmm.h"

#include <stddef.h>
#include <stdlib.h>

struct vmm_address_space *vmm_create_address_space(void) {
    /* Allocate enough storage so the caller can safely zero-init or
     * read back the refcount field; the size of the real struct is
     * defined in the public header. */
    struct vmm_address_space *as =
        (struct vmm_address_space *)calloc(1, sizeof(struct vmm_address_space));
    if (!as) return NULL;
    as->refcount = 1;
    return as;
}

/* M4 phase 7c: process_fork now calls vmm_clone_address_space.
 * Host tests do not exercise the actual page-table walk; they just
 * need a non-NULL clone that round-trips through destroy. The stub
 * therefore mirrors create_address_space (fresh empty AS) so that
 * the existing test_process_iter / test_process_destroy contracts
 * keep passing. The real CoW behaviour is locked elsewhere by
 * test_pmm_refcount and test_vmm_cow. */
struct vmm_address_space *vmm_clone_address_space(
    const struct vmm_address_space *src) {
    (void)src;
    return vmm_create_address_space();
}

void vmm_destroy_address_space(struct vmm_address_space *as) {
    if (!as) return;
    if (as->refcount > 0) {
        as->refcount--;
        if (as->refcount > 0) return;
    }
    /* Phase 7b: keep parity with the real vmm.c teardown - clear the
     * anonymous-region registry before freeing the AS so a test that
     * registered regions does not leak the kmalloc-backed nodes. The
     * helper itself is safe on a NULL list. */
    vmm_clear_anon_regions(as);
    free(as);
}

/* process.c references elf_load_from_file via an extern declaration in
 * process_exec. None of the unit tests call process_exec, but the
 * linker still resolves the symbol; provide a stub that always fails
 * loudly enough to make accidental use detectable in CI. */
struct process;
int elf_load_from_file(struct process *proc, const char *path) {
    (void)proc;
    (void)path;
    return -1;
}

/* klog persistence calls pit_ticks() to timestamp entries. The real PIT
 * driver is x86_64-only; the host test simply returns a monotonically
 * increasing counter so timestamps remain ordered without needing the
 * platform timer. */
#include <stdint.h>

uint64_t pit_ticks(void) {
    static uint64_t fake_ticks = 0;
    return ++fake_ticks;
}

/* M4 phase 8f.1: tss.c references this symbol from interrupts.c. The
 * real interrupts.c is not linked into the host unit-test binary
 * (it does cli/sti and lgdt), so we provide a no-op stub that lets
 * tss_layout tests link cleanly. */
void x64_gdt_write_tss_descriptor(uint64_t low_bytes, uint64_t high_bytes) {
    (void)low_bytes;
    (void)high_bytes;
}

/* M4 phase 8f.4: user_task_init.c stores &x64_user_first_dispatch in
 * t->context.rip so context_switch can later jump into the synthetic
 * iretq path. The host build only cares about the SYMBOL ADDRESS;
 * nothing actually calls this in the test binary. */
void x64_user_first_dispatch(void) {
    /* Intentionally empty: host tests inspect t->context.rip ==
     * &x64_user_first_dispatch but never invoke the trampoline. */
}

/* M5 phase B.3: process_exec_replace calls vmm_switch_address_space
 * to reload CR3 onto the new AS before destroying the old. The host
 * build has no CR3 to write; record the most recent switch target
 * so a future test can assert the call happened in order, and
 * silently no-op otherwise. */
const struct vmm_address_space *g_stub_vmm_last_switch = NULL;
unsigned g_stub_vmm_switch_calls = 0;

void vmm_switch_address_space(struct vmm_address_space *as) {
    g_stub_vmm_last_switch = as;
    g_stub_vmm_switch_calls++;
}
