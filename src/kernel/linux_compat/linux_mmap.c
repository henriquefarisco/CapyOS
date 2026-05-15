#include "kernel/linux_compat/linux_mmap.h"
#include "kernel/linux_compat/linux_errno.h"
#include "kernel/linux_compat/linux_syscall.h"
#include "kernel/linux_compat/linux_syscall_nrs.h"

#include <stdint.h>
#include <stddef.h>

static struct linux_mmap_ops g_ops;

void linux_mmap_install_ops(const struct linux_mmap_ops *ops) {
    if (ops) g_ops = *ops;
    else g_ops = (struct linux_mmap_ops){0};
}

void linux_mmap_reset_for_tests(void) {
    g_ops = (struct linux_mmap_ops){0};
}

/* Round `length` up to a multiple of PAGE_SIZE. Linux requires
 * length > 0; length == 0 returns -EINVAL. Returns 0 if the round
 * up overflows (length too large). */
static size_t length_to_pages(size_t length) {
    if (length == 0) return 0;
    /* Detect overflow before the +PAGE_SIZE-1 step. */
    if (length > (size_t)(SIZE_MAX - LINUX_MMAP_PAGE_SIZE + 1)) return 0;
    size_t bytes = (length + LINUX_MMAP_PAGE_SIZE - 1)
                   & ~(size_t)(LINUX_MMAP_PAGE_SIZE - 1);
    return bytes / LINUX_MMAP_PAGE_SIZE;
}

/* ---------- mmap ---------------------------------------------- */

int64_t linux_mmap(uint64_t addr_hint, size_t length, uint32_t prot,
                   uint32_t flags, int fd, uint64_t offset) {
    if (length == 0) return -LINUX_EINVAL;

    /* prot validation: only known bits. PROT_NONE (0) is valid. */
    if (prot & ~(uint32_t)LINUX_PROT_KNOWN_MASK) return -LINUX_EINVAL;

    /* Marco M1 accepts MAP_ANONYMOUS | MAP_PRIVATE [| MAP_FIXED].
     * MAP_SHARED returns -EINVAL until shm/file backing lands. */
    if (flags & ~(uint32_t)LINUX_MAP_SUPPORTED_FLAGS) return -LINUX_EINVAL;
    if ((flags & LINUX_MAP_PRIVATE) == 0)   return -LINUX_EINVAL;
    if ((flags & LINUX_MAP_ANONYMOUS) == 0) return -LINUX_EINVAL;

    /* Anonymous mappings: fd must be -1 and offset must be 0. */
    if (fd != -1)    return -LINUX_EINVAL;
    if (offset != 0) return -LINUX_EINVAL;

    size_t pages = length_to_pages(length);
    if (pages == 0) return -LINUX_EINVAL;

    /* MAP_FIXED requires a page-aligned non-zero hint and uses a
     * different allocator path. */
    if (flags & LINUX_MAP_FIXED) {
        if (addr_hint == 0) return -LINUX_EINVAL;
        if ((addr_hint & (LINUX_MMAP_PAGE_SIZE - 1)) != 0) {
            return -LINUX_EINVAL;
        }
        uint64_t va;
        if (g_ops.alloc_anon_at) {
            va = g_ops.alloc_anon_at(addr_hint, pages, prot);
        } else if (g_ops.alloc_anon) {
            /* Fallback: ignore the hint. Incorrect MAP_FIXED
             * semantics but lets userland startup paths succeed
             * until the VMM exposes a fixed-address allocator. */
            va = g_ops.alloc_anon(pages, prot);
        } else {
            return -LINUX_ENOMEM;
        }
        if (va == 0) return -LINUX_ENOMEM;
        return (int64_t)va;
    }

    /* MAP_FIXED absent: hint is advisory and we ignore it. */
    if (!g_ops.alloc_anon) return -LINUX_ENOMEM;

    uint64_t va = g_ops.alloc_anon(pages, prot);
    if (va == 0) return -LINUX_ENOMEM;
    return (int64_t)va;
}

/* ---------- munmap -------------------------------------------- */

int64_t linux_munmap(uint64_t addr, size_t length) {
    if (length == 0) return -LINUX_EINVAL;
    /* Linux requires `addr` to be page-aligned and rejects
     * misaligned values with -EINVAL. */
    if ((addr & (LINUX_MMAP_PAGE_SIZE - 1)) != 0) return -LINUX_EINVAL;

    size_t pages = length_to_pages(length);
    if (pages == 0) return -LINUX_EINVAL;

    if (!g_ops.free_pages) return -LINUX_EINVAL;

    int rc = g_ops.free_pages(addr, pages);
    if (rc != 0) return -LINUX_EINVAL;
    return 0;
}

/* ---------- mprotect ------------------------------------------ */

int64_t linux_mprotect(uint64_t addr, size_t length, uint32_t prot) {
    if (length == 0) return -LINUX_EINVAL;
    if ((addr & (LINUX_MMAP_PAGE_SIZE - 1)) != 0) return -LINUX_EINVAL;
    if (prot & ~(uint32_t)LINUX_PROT_KNOWN_MASK) return -LINUX_EINVAL;

    size_t pages = length_to_pages(length);
    if (pages == 0) return -LINUX_EINVAL;

    if (!g_ops.protect) return -LINUX_EINVAL;

    int rc = g_ops.protect(addr, pages, prot);
    if (rc != 0) return -LINUX_EINVAL;
    return 0;
}

/* ---------- madvise ------------------------------------------- */

static int is_known_advice(int advice) {
    switch (advice) {
        case LINUX_MADV_NORMAL: case LINUX_MADV_RANDOM:
        case LINUX_MADV_SEQUENTIAL: case LINUX_MADV_WILLNEED:
        case LINUX_MADV_DONTNEED: case LINUX_MADV_FREE:
        case LINUX_MADV_REMOVE: case LINUX_MADV_DONTFORK:
        case LINUX_MADV_DOFORK: case LINUX_MADV_HWPOISON:
        case LINUX_MADV_SOFT_OFFLINE: case LINUX_MADV_MERGEABLE:
        case LINUX_MADV_UNMERGEABLE: case LINUX_MADV_HUGEPAGE:
        case LINUX_MADV_NOHUGEPAGE: case LINUX_MADV_DONTDUMP:
        case LINUX_MADV_DODUMP: case LINUX_MADV_WIPEONFORK:
        case LINUX_MADV_KEEPONFORK: case LINUX_MADV_COLD:
        case LINUX_MADV_PAGEOUT: case LINUX_MADV_POPULATE_READ:
        case LINUX_MADV_POPULATE_WRITE: return 1;
    }
    return 0;
}

int64_t linux_madvise(uint64_t addr, size_t length, int advice) {
    if (length == 0) return -LINUX_EINVAL;
    if ((addr & (LINUX_MMAP_PAGE_SIZE - 1)) != 0) return -LINUX_EINVAL;
    if (!is_known_advice(advice)) return -LINUX_EINVAL;
    /* All advice are hints; the kernel may ignore them. */
    return 0;
}

/* ---------- mremap -------------------------------------------- */

int64_t linux_mremap(uint64_t old_addr, size_t old_len,
                     size_t new_len, uint32_t flags,
                     uint64_t new_addr) {
    if (old_len == 0) return -LINUX_EINVAL;
    if ((old_addr & (LINUX_MMAP_PAGE_SIZE - 1)) != 0) return -LINUX_EINVAL;
    if (flags & ~LINUX_MREMAP_KNOWN_FLAGS) return -LINUX_EINVAL;
    /* MREMAP_FIXED requires MREMAP_MAYMOVE (Linux invariant). */
    if ((flags & LINUX_MREMAP_FIXED) && !(flags & LINUX_MREMAP_MAYMOVE)) {
        return -LINUX_EINVAL;
    }
    if (flags & LINUX_MREMAP_FIXED) {
        if ((new_addr & (LINUX_MMAP_PAGE_SIZE - 1)) != 0) {
            return -LINUX_EINVAL;
        }
    }

    size_t old_pages = length_to_pages(old_len);
    size_t new_pages = length_to_pages(new_len);
    if (old_pages == 0 || new_pages == 0) return -LINUX_EINVAL;

    /* Same size and no FIXED -> Linux returns the original addr. */
    if (old_pages == new_pages && !(flags & LINUX_MREMAP_FIXED)) {
        return (int64_t)old_addr;
    }

    if (!g_ops.remap) return -LINUX_ENOSYS;
    uint64_t va = g_ops.remap(old_addr, old_pages, new_pages, flags);
    if (va == 0) return -LINUX_ENOMEM;
    return (int64_t)va;
}

/* ---------- Syscall adapters ---------------------------------- */

static int64_t sys_mmap(const struct linux_syscall_args *a) {
    return linux_mmap(a->a0, (size_t)a->a1, (uint32_t)a->a2,
                      (uint32_t)a->a3, (int)a->a4, a->a5);
}

static int64_t sys_munmap(const struct linux_syscall_args *a) {
    return linux_munmap(a->a0, (size_t)a->a1);
}

static int64_t sys_mprotect(const struct linux_syscall_args *a) {
    return linux_mprotect(a->a0, (size_t)a->a1, (uint32_t)a->a2);
}

static int64_t sys_madvise(const struct linux_syscall_args *a) {
    return linux_madvise(a->a0, (size_t)a->a1, (int)a->a2);
}

static int64_t sys_mremap(const struct linux_syscall_args *a) {
    return linux_mremap(a->a0, (size_t)a->a1, (size_t)a->a2,
                        (uint32_t)a->a3, a->a4);
}

void linux_mmap_register_syscalls(void) {
    (void)linux_syscall_register(LINUX_NR_mmap,     sys_mmap);
    (void)linux_syscall_register(LINUX_NR_munmap,   sys_munmap);
    (void)linux_syscall_register(LINUX_NR_mprotect, sys_mprotect);
    (void)linux_syscall_register(LINUX_NR_madvise,  sys_madvise);
    (void)linux_syscall_register(LINUX_NR_mremap,   sys_mremap);
}
