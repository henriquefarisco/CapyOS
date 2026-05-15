#ifndef KERNEL_LINUX_COMPAT_LINUX_MMAP_H
#define KERNEL_LINUX_COMPAT_LINUX_MMAP_H

#include <stdint.h>
#include <stddef.h>

/* Linux-ABI mmap/munmap/mprotect shim (S1.2, Marco M1 subset).
 *
 * SpiderMonkey shell needs:
 *   - mmap(NULL, len, PROT_READ|PROT_WRITE|PROT_EXEC,
 *          MAP_ANON|MAP_PRIVATE, -1, 0)            -- JIT code
 *   - mmap(NULL, len, PROT_READ|PROT_WRITE,
 *          MAP_ANON|MAP_PRIVATE, -1, 0)            -- GC heap
 *   - munmap(addr, len)                            -- release
 *   - mprotect(addr, len, PROT_READ|PROT_EXEC)     -- W^X flip
 *
 * That is the entire Marco M1 surface. We do NOT support yet:
 *   - MAP_FIXED       (caller-chosen address)
 *   - MAP_FIXED_NOREPLACE
 *   - File-backed mappings (fd != -1)
 *   - MAP_SHARED      (cross-process)
 *   - MAP_HUGETLB / MAP_LOCKED / MAP_POPULATE
 *
 * All unsupported flag bits / non-NULL fd / non-zero offset
 * return -LINUX_EINVAL. The shim is built so flags can be added
 * incrementally as the VMM gains capabilities.
 *
 * Layering: pure logic + injected VMM callbacks. Production
 * wiring lives in `linux_mmap_init.c` and binds to vmm_alloc_*
 * helpers. Host tests inject deterministic stubs that hand out
 * sequential virtual addresses without touching the page tables.
 */

/* Linux flag bits (include/uapi/asm-generic/mman-common.h
 * for prot, and asm/mman.h for MAP_*). x86_64 specific values. */
#define LINUX_PROT_NONE  0x0u
#define LINUX_PROT_READ  0x1u
#define LINUX_PROT_WRITE 0x2u
#define LINUX_PROT_EXEC  0x4u

#define LINUX_PROT_KNOWN_MASK \
    (LINUX_PROT_READ | LINUX_PROT_WRITE | LINUX_PROT_EXEC)

#define LINUX_MAP_SHARED    0x01u
#define LINUX_MAP_PRIVATE   0x02u
#define LINUX_MAP_FIXED     0x10u
#define LINUX_MAP_ANONYMOUS 0x20u

/* Subset we accept today: ANON|PRIVATE plus the optional FIXED.
 * Anything else returns -EINVAL. As the VMM grows we widen this
 * mask. */
#define LINUX_MAP_SUPPORTED_FLAGS \
    (LINUX_MAP_ANONYMOUS | LINUX_MAP_PRIVATE | LINUX_MAP_FIXED)

/* madvise advice values (asm-generic/mman-common.h). */
#define LINUX_MADV_NORMAL      0
#define LINUX_MADV_RANDOM      1
#define LINUX_MADV_SEQUENTIAL  2
#define LINUX_MADV_WILLNEED    3
#define LINUX_MADV_DONTNEED    4
#define LINUX_MADV_FREE        8
#define LINUX_MADV_REMOVE      9
#define LINUX_MADV_DONTFORK    10
#define LINUX_MADV_DOFORK      11
#define LINUX_MADV_HWPOISON    100
#define LINUX_MADV_SOFT_OFFLINE 101
#define LINUX_MADV_MERGEABLE   12
#define LINUX_MADV_UNMERGEABLE 13
#define LINUX_MADV_HUGEPAGE    14
#define LINUX_MADV_NOHUGEPAGE  15
#define LINUX_MADV_DONTDUMP    16
#define LINUX_MADV_DODUMP      17
#define LINUX_MADV_WIPEONFORK  18
#define LINUX_MADV_KEEPONFORK  19
#define LINUX_MADV_COLD        20
#define LINUX_MADV_PAGEOUT     21
#define LINUX_MADV_POPULATE_READ  22
#define LINUX_MADV_POPULATE_WRITE 23

/* mremap flag bits (uapi/linux/mman.h). */
#define LINUX_MREMAP_MAYMOVE    1u
#define LINUX_MREMAP_FIXED      2u
#define LINUX_MREMAP_DONTUNMAP  4u

#define LINUX_MREMAP_KNOWN_FLAGS \
    (LINUX_MREMAP_MAYMOVE | LINUX_MREMAP_FIXED | LINUX_MREMAP_DONTUNMAP)

/* `mmap` returns this sentinel on error in the userland API
 * (cast to void*). The kernel side returns negative errno; the
 * libc trampoline does the conversion. */
#define LINUX_MAP_FAILED_ADDR ((uint64_t)(-1))

/* Page size used for length rounding (Linux x86_64). Exposed so
 * tests can build expectations without copy-pasting the constant. */
#define LINUX_MMAP_PAGE_SIZE 4096u

/* VMM callback bundle. Production binds the real VMM; tests
 * inject deterministic stubs. */
struct linux_mmap_ops {
    /* Allocate a freshly zeroed anonymous private region of
     * `pages * PAGE_SIZE` bytes whose page-protection encodes
     * `prot` (any subset of PROT_READ|WRITE|EXEC). The callback
     * returns the page-aligned virtual address on success, or
     * 0 on failure (caller maps to -ENOMEM).
     *
     * If `prot == PROT_NONE`, the region is reserved as a
     * guard (no R/W/X). */
    uint64_t (*alloc_anon)(size_t pages, uint32_t prot);

    /* Allocate at a caller-specified VA (MAP_FIXED). Returns the
     * VA on success or 0 on failure. May be NULL: shim falls
     * back to alloc_anon (ignoring the hint), which is incorrect
     * MAP_FIXED behaviour but acceptable until the VMM exposes
     * a fixed-address allocator. */
    uint64_t (*alloc_anon_at)(uint64_t addr, size_t pages, uint32_t prot);

    /* Release a previously-allocated region. `addr` is page-aligned;
     * `pages` is the number of pages. Returns 0 on success. The
     * shim accepts arbitrary `(addr, pages)` and forwards verbatim;
     * the underlying VMM is responsible for rejecting unknown
     * regions (-EINVAL on the VMM side -> -EINVAL surfaces). */
    int (*free_pages)(uint64_t addr, size_t pages);

    /* Change protection on an existing region. Returns 0 / -1. */
    int (*protect)(uint64_t addr, size_t pages, uint32_t prot);

    /* Remap. Returns the new VA, or 0 on failure. */
    uint64_t (*remap)(uint64_t addr, size_t old_pages,
                      size_t new_pages, uint32_t flags);
};

void linux_mmap_install_ops(const struct linux_mmap_ops *ops);
void linux_mmap_reset_for_tests(void);

/* mmap entry. Returns either the mapped address (cast as int64_t,
 * always >= LINUX_MMAP_PAGE_SIZE in practice because we round up
 * length) or a negative -LINUX_E*. */
int64_t linux_mmap(uint64_t addr_hint, size_t length, uint32_t prot,
                   uint32_t flags, int fd, uint64_t offset);

/* munmap entry. */
int64_t linux_munmap(uint64_t addr, size_t length);

/* mprotect entry. */
int64_t linux_mprotect(uint64_t addr, size_t length, uint32_t prot);

/* madvise(addr, length, advice). Validates advice value; the
 * actual hint is ignored (Marco M1 has no LRU/page reclaim
 * pressure for the kernel to react to). Returns 0 for known
 * advice, -EINVAL otherwise. */
int64_t linux_madvise(uint64_t addr, size_t length, int advice);

/* mremap(old_addr, old_len, new_len, flags, new_addr). Returns
 * new VA, or -ENOMEM/-EINVAL.
 *   - flags must be subset of LINUX_MREMAP_KNOWN_FLAGS.
 *   - old_addr must be page-aligned, old_len > 0.
 *   - If new_len == old_len, no-op (returns old_addr).
 *   - MREMAP_FIXED requires MREMAP_MAYMOVE.
 *   - Without MAYMOVE, expansion in place via remap callback;
 *     returns -ENOMEM if remap returns 0.
 */
int64_t linux_mremap(uint64_t old_addr, size_t old_len,
                     size_t new_len, uint32_t flags,
                     uint64_t new_addr);

void linux_mmap_register_syscalls(void);

#endif /* KERNEL_LINUX_COMPAT_LINUX_MMAP_H */
