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
