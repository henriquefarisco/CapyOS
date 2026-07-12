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

static const struct vmm_address_space *g_stub_vmm_active_as = NULL;
static uint64_t g_stub_vmm_next_user_cr3 = 0x2000u;

void stub_vmm_set_active_address_space(const struct vmm_address_space *as) {
    g_stub_vmm_active_as = as;
}

int vmm_address_space_is_active(const struct vmm_address_space *as) {
    return as != NULL && as == g_stub_vmm_active_as;
}

struct vmm_address_space *vmm_create_address_space(void) {
    /* Allocate enough storage so the caller can safely zero-init or
     * read back the refcount field; the size of the real struct is
     * defined in the public header. */
    struct vmm_address_space *as =
        (struct vmm_address_space *)calloc(1, sizeof(struct vmm_address_space));
    if (!as) return NULL;
    as->refcount = 1;
    as->pml4_phys = g_stub_vmm_next_user_cr3;
    g_stub_vmm_next_user_cr3 += 0x1000u;
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
    if (vmm_address_space_is_active(as)) return;
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

/* 2026-05-02: process_fd_free now calls vfs_close on FD_TYPE_VFS to
 * release the underlying file. Host tests never populate that FD
 * type (proc->fds[i].type stays 0 = FREE), so vfs_close is never
 * actually invoked from tests. We still need a symbol for the
 * linker; stub with a no-op that loudly logs if it ever runs. */
struct file;
void vfs_close(struct file *file) {
    (void)file;
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

/* alpha.310: pmm_init now calls this to skip firmware read-only (reserved)
 * RAM frames (real impl walks the CR3 page tables, x86_64-only). Host tests
 * link the real pmm.c but not the real vmm.c; there is no firmware identity
 * map in the host process, so report every frame usable (1). */
int vmm_identity_is_writable(uint64_t phys) {
    (void)phys;
    return 1;
}

/* alpha.311: elf_load wraps its segment writes in enter/leave to run on the
 * kernel's own page tables. Host tests have no CR3; provide no-op symbols. */
uint64_t vmm_enter_kernel_tables(void) { return 0; }
void vmm_leave_kernel_tables(uint64_t prev_cr3) { (void)prev_cr3; }
uint64_t vmm_kernel_pml4_phys(void) { return 0x1000u; }
