/*
 * Host tests for linux_mmap (S1.2 mmap/munmap/mprotect).
 *
 * Bunch of contracts to lock:
 *   - flag whitelist (only ANON|PRIVATE today)
 *   - prot whitelist (only R/W/E)
 *   - length rounded up to page size, 0 -> -EINVAL
 *   - addr alignment
 *   - fd != -1 / offset != 0 rejected
 *   - VMM callback failure -> -ENOMEM
 */

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

#include "kernel/linux_compat/linux_mmap.h"
#include "kernel/linux_compat/linux_errno.h"

static int tests_run, tests_passed;

#define TEST(name) do { tests_run++; printf("  %-74s ", name); } while (0)
#define PASS() do { printf("OK\n"); tests_passed++; } while (0)
#define FAIL(m) do { printf("FAIL: %s\n", m); } while (0)

/* Fake VMM. */
static uint64_t g_alloc_next_va = 0x100000000000ull;
static int g_alloc_calls;
static int g_alloc_at_calls;
static int g_remap_calls;
static int g_free_calls;
static int g_protect_calls;
static int g_alloc_should_fail = 0;
static int g_alloc_at_should_fail = 0;
static int g_remap_should_fail = 0;
static int g_free_should_fail = 0;
static int g_protect_should_fail = 0;
static size_t g_last_alloc_pages;
static uint32_t g_last_alloc_prot;
static uint64_t g_last_alloc_at_addr;
static uint64_t g_last_remap_addr;
static size_t   g_last_remap_old;
static size_t   g_last_remap_new;
static uint64_t g_last_free_addr;
static size_t   g_last_free_pages;
static uint32_t g_last_protect_prot;

static uint64_t fake_alloc_anon(size_t pages, uint32_t prot) {
    g_alloc_calls++;
    g_last_alloc_pages = pages;
    g_last_alloc_prot = prot;
    if (g_alloc_should_fail) return 0;
    uint64_t va = g_alloc_next_va;
    g_alloc_next_va += pages * LINUX_MMAP_PAGE_SIZE;
    return va;
}

static int fake_free_pages(uint64_t addr, size_t pages) {
    g_free_calls++;
    g_last_free_addr = addr;
    g_last_free_pages = pages;
    return g_free_should_fail ? -1 : 0;
}

static int fake_protect(uint64_t addr, size_t pages, uint32_t prot) {
    (void)addr; (void)pages;
    g_protect_calls++;
    g_last_protect_prot = prot;
    return g_protect_should_fail ? -1 : 0;
}

static uint64_t fake_alloc_anon_at(uint64_t addr, size_t pages, uint32_t prot) {
    (void)pages; (void)prot;
    g_alloc_at_calls++;
    g_last_alloc_at_addr = addr;
    if (g_alloc_at_should_fail) return 0;
    return addr;  /* honour the requested addr */
}

static uint64_t fake_remap(uint64_t addr, size_t old_pages,
                           size_t new_pages, uint32_t flags) {
    (void)flags;
    g_remap_calls++;
    g_last_remap_addr = addr;
    g_last_remap_old  = old_pages;
    g_last_remap_new  = new_pages;
    if (g_remap_should_fail) return 0;
    /* Return original addr if shrinking; new addr if growing. */
    return new_pages <= old_pages ? addr : (addr + 0x100000ull);
}

static void install_fake_ops(void) {
    linux_mmap_reset_for_tests();
    g_alloc_next_va = 0x100000000000ull;
    g_alloc_calls = g_alloc_at_calls = g_remap_calls = 0;
    g_free_calls = g_protect_calls = 0;
    g_alloc_should_fail = g_alloc_at_should_fail = g_remap_should_fail = 0;
    g_free_should_fail = g_protect_should_fail = 0;
    g_last_alloc_pages = 0;
    g_last_alloc_prot = 0;
    g_last_alloc_at_addr = 0;
    g_last_remap_addr = 0; g_last_remap_old = 0; g_last_remap_new = 0;
    g_last_free_addr = 0;
    g_last_free_pages = 0;
    g_last_protect_prot = 0;
    static const struct linux_mmap_ops ops = {
        .alloc_anon    = fake_alloc_anon,
        .alloc_anon_at = fake_alloc_anon_at,
        .free_pages    = fake_free_pages,
        .protect       = fake_protect,
        .remap         = fake_remap,
    };
    linux_mmap_install_ops(&ops);
}

#define MAP_PA (LINUX_MAP_PRIVATE | LINUX_MAP_ANONYMOUS)

/* -------- mmap -------- */

static void t_mmap_basic_anon(void) {
    install_fake_ops();
    int64_t r = linux_mmap(0, 4096, LINUX_PROT_READ|LINUX_PROT_WRITE,
                           MAP_PA, -1, 0);
    TEST("mmap: ANON|PRIVATE 1 page R/W returns aligned VA from VMM");
    if (r > 0 && g_alloc_calls == 1 && g_last_alloc_pages == 1 &&
        g_last_alloc_prot == (LINUX_PROT_READ|LINUX_PROT_WRITE)) PASS();
    else FAIL("basic alloc wrong");
}

static void t_mmap_rounds_up_length(void) {
    install_fake_ops();
    /* 4097 bytes -> 2 pages. */
    linux_mmap(0, 4097, LINUX_PROT_READ, MAP_PA, -1, 0);
    TEST("mmap: length is rounded up to multiple of PAGE_SIZE");
    if (g_last_alloc_pages == 2) PASS();
    else FAIL("length round-up wrong");
}

static void t_mmap_zero_length(void) {
    install_fake_ops();
    int64_t r = linux_mmap(0, 0, LINUX_PROT_READ, MAP_PA, -1, 0);
    TEST("mmap: length=0 -> -EINVAL");
    if (r == -LINUX_EINVAL && g_alloc_calls == 0) PASS();
    else FAIL("zero-length not rejected");
}

static void t_mmap_unknown_prot(void) {
    install_fake_ops();
    int64_t r = linux_mmap(0, 4096, 0x100u, MAP_PA, -1, 0);
    TEST("mmap: unknown prot bit -> -EINVAL");
    if (r == -LINUX_EINVAL && g_alloc_calls == 0) PASS();
    else FAIL("unknown prot not rejected");
}

static void t_mmap_prot_none_ok(void) {
    install_fake_ops();
    int64_t r = linux_mmap(0, 4096, LINUX_PROT_NONE, MAP_PA, -1, 0);
    TEST("mmap: PROT_NONE accepted (guard pages)");
    if (r > 0) PASS();
    else FAIL("PROT_NONE not accepted");
}

static void t_mmap_unknown_map_flag(void) {
    install_fake_ops();
    /* Bit 0x800 (MAP_GROWSDOWN) is outside our supported mask. */
    int64_t r = linux_mmap(0, 4096, LINUX_PROT_READ,
                           MAP_PA | 0x800u, -1, 0);
    TEST("mmap: unknown MAP_* bit (e.g. MAP_GROWSDOWN) -> -EINVAL");
    if (r == -LINUX_EINVAL && g_alloc_calls == 0) PASS();
    else FAIL("unknown flag not rejected");
}

static void t_mmap_map_shared_rejected(void) {
    install_fake_ops();
    int64_t r = linux_mmap(0, 4096, LINUX_PROT_READ,
                           LINUX_MAP_ANONYMOUS | LINUX_MAP_SHARED, -1, 0);
    TEST("mmap: MAP_SHARED -> -EINVAL (Marco M1 only PRIVATE)");
    if (r == -LINUX_EINVAL) PASS();
    else FAIL("MAP_SHARED not rejected");
}

static void t_mmap_missing_anon(void) {
    install_fake_ops();
    int64_t r = linux_mmap(0, 4096, LINUX_PROT_READ,
                           LINUX_MAP_PRIVATE, -1, 0);
    TEST("mmap: missing MAP_ANONYMOUS -> -EINVAL (no file backing yet)");
    if (r == -LINUX_EINVAL) PASS();
    else FAIL("non-anon not rejected");
}

static void t_mmap_missing_private(void) {
    install_fake_ops();
    int64_t r = linux_mmap(0, 4096, LINUX_PROT_READ,
                           LINUX_MAP_ANONYMOUS, -1, 0);
    TEST("mmap: missing MAP_PRIVATE -> -EINVAL");
    if (r == -LINUX_EINVAL) PASS();
    else FAIL("missing PRIVATE not rejected");
}

static void t_mmap_fd_must_be_neg1(void) {
    install_fake_ops();
    int64_t r = linux_mmap(0, 4096, LINUX_PROT_READ, MAP_PA, 5, 0);
    TEST("mmap: ANON with fd != -1 -> -EINVAL");
    if (r == -LINUX_EINVAL) PASS();
    else FAIL("non--1 fd not rejected");
}

static void t_mmap_offset_must_be_zero(void) {
    install_fake_ops();
    int64_t r = linux_mmap(0, 4096, LINUX_PROT_READ, MAP_PA, -1, 4096);
    TEST("mmap: ANON with offset != 0 -> -EINVAL");
    if (r == -LINUX_EINVAL) PASS();
    else FAIL("non-zero offset not rejected");
}

static void t_mmap_alloc_failure(void) {
    install_fake_ops();
    g_alloc_should_fail = 1;
    int64_t r = linux_mmap(0, 4096, LINUX_PROT_READ, MAP_PA, -1, 0);
    TEST("mmap: VMM alloc failure -> -ENOMEM");
    if (r == -LINUX_ENOMEM) PASS();
    else FAIL("alloc failure not surfaced");
}

static void t_mmap_no_ops(void) {
    linux_mmap_reset_for_tests();
    int64_t r = linux_mmap(0, 4096, LINUX_PROT_READ, MAP_PA, -1, 0);
    TEST("mmap: no ops installed -> -ENOMEM");
    if (r == -LINUX_ENOMEM) PASS();
    else FAIL("no-ops surface wrong");
}

static void t_mmap_jit_pattern(void) {
    /* SpiderMonkey JIT pattern: PROT_R|W|EXEC. Lock that we accept. */
    install_fake_ops();
    int64_t r = linux_mmap(0, 65536,
                           LINUX_PROT_READ|LINUX_PROT_WRITE|LINUX_PROT_EXEC,
                           MAP_PA, -1, 0);
    TEST("mmap: PROT_READ|WRITE|EXEC + ANON|PRIVATE (SpiderMonkey JIT) accepted");
    if (r > 0 && g_last_alloc_pages == 16 &&
        g_last_alloc_prot == (LINUX_PROT_READ|LINUX_PROT_WRITE|LINUX_PROT_EXEC))
        PASS();
    else FAIL("JIT pattern wrong");
}

static void t_install_null_clears_mmap_alloc_ops(void) {
    install_fake_ops();
    linux_mmap_install_ops(NULL);
    int64_t r1 = linux_mmap(0, 4096, LINUX_PROT_READ, MAP_PA, -1, 0);
    int64_t r2 = linux_mmap(0x200000000000ull, 4096, LINUX_PROT_READ,
                            MAP_PA | LINUX_MAP_FIXED, -1, 0);
    TEST("mmap install_ops(NULL) clears allocation callbacks");
    if (r1 == -LINUX_ENOMEM && r2 == -LINUX_ENOMEM &&
        g_alloc_calls == 0 && g_alloc_at_calls == 0) PASS();
    else FAIL("allocation callbacks not cleared");
}

/* -------- munmap -------- */

static void t_munmap_basic(void) {
    install_fake_ops();
    int64_t r = linux_munmap(0x100000000000ull, 8192);
    TEST("munmap: aligned addr + length=2 pages calls free_pages(2)");
    if (r == 0 && g_free_calls == 1 && g_last_free_pages == 2) PASS();
    else FAIL("basic munmap wrong");
}

static void t_munmap_misaligned(void) {
    install_fake_ops();
    int64_t r = linux_munmap(0x100000000001ull, 4096);
    TEST("munmap: misaligned addr -> -EINVAL");
    if (r == -LINUX_EINVAL && g_free_calls == 0) PASS();
    else FAIL("misaligned not rejected");
}

static void t_munmap_zero_length(void) {
    install_fake_ops();
    int64_t r = linux_munmap(0x100000000000ull, 0);
    TEST("munmap: length=0 -> -EINVAL");
    if (r == -LINUX_EINVAL) PASS();
    else FAIL("zero length not rejected");
}

static void t_munmap_callback_fails(void) {
    install_fake_ops();
    g_free_should_fail = 1;
    int64_t r = linux_munmap(0x100000000000ull, 4096);
    TEST("munmap: VMM rejects -> -EINVAL");
    if (r == -LINUX_EINVAL) PASS();
    else FAIL("callback failure not surfaced");
}

static void t_install_null_clears_release_protect_ops(void) {
    install_fake_ops();
    linux_mmap_install_ops(NULL);
    int64_t r1 = linux_munmap(0x100000000000ull, 4096);
    int64_t r2 = linux_mprotect(0x100000000000ull, 4096, LINUX_PROT_READ);
    TEST("mmap install_ops(NULL) clears munmap/mprotect callbacks");
    if (r1 == -LINUX_EINVAL && r2 == -LINUX_EINVAL &&
        g_free_calls == 0 && g_protect_calls == 0) PASS();
    else FAIL("release/protect callbacks not cleared");
}

/* -------- mprotect -------- */

static void t_mprotect_basic(void) {
    install_fake_ops();
    int64_t r = linux_mprotect(0x100000000000ull, 4096,
                               LINUX_PROT_READ|LINUX_PROT_EXEC);
    TEST("mprotect: aligned addr + R|EXEC calls protect with same prot");
    if (r == 0 && g_protect_calls == 1 &&
        g_last_protect_prot == (LINUX_PROT_READ|LINUX_PROT_EXEC)) PASS();
    else FAIL("basic mprotect wrong");
}

static void t_mprotect_unknown_prot(void) {
    install_fake_ops();
    int64_t r = linux_mprotect(0x100000000000ull, 4096, 0x80u);
    TEST("mprotect: unknown prot bit -> -EINVAL");
    if (r == -LINUX_EINVAL && g_protect_calls == 0) PASS();
    else FAIL("unknown prot not rejected");
}

static void t_mprotect_misaligned(void) {
    install_fake_ops();
    int64_t r = linux_mprotect(0x100000000005ull, 4096, LINUX_PROT_READ);
    TEST("mprotect: misaligned addr -> -EINVAL");
    if (r == -LINUX_EINVAL && g_protect_calls == 0) PASS();
    else FAIL("misaligned not rejected");
}

/* -------- MAP_FIXED -------- */

static void t_mmap_fixed_basic(void) {
    install_fake_ops();
    uint64_t hint = 0x200000000000ull;
    int64_t r = linux_mmap(hint, 4096, LINUX_PROT_READ|LINUX_PROT_WRITE,
                           MAP_PA | LINUX_MAP_FIXED, -1, 0);
    TEST("mmap MAP_FIXED: uses alloc_anon_at, returns requested addr");
    if (r == (int64_t)hint && g_alloc_at_calls == 1 &&
        g_last_alloc_at_addr == hint && g_alloc_calls == 0) PASS();
    else FAIL("FIXED path wrong");
}

static void t_mmap_fixed_zero_addr(void) {
    install_fake_ops();
    int64_t r = linux_mmap(0, 4096, LINUX_PROT_READ,
                           MAP_PA | LINUX_MAP_FIXED, -1, 0);
    TEST("mmap MAP_FIXED: addr=0 -> -EINVAL");
    if (r == -LINUX_EINVAL) PASS();
    else FAIL("zero addr accepted");
}

static void t_mmap_fixed_misaligned(void) {
    install_fake_ops();
    int64_t r = linux_mmap(0x100000000001ull, 4096, LINUX_PROT_READ,
                           MAP_PA | LINUX_MAP_FIXED, -1, 0);
    TEST("mmap MAP_FIXED: misaligned addr -> -EINVAL");
    if (r == -LINUX_EINVAL) PASS();
    else FAIL("misaligned accepted");
}

static void t_mmap_no_fixed_ignores_hint(void) {
    install_fake_ops();
    /* Hint is advisory without MAP_FIXED -- alloc_anon path used. */
    uint64_t hint = 0x200000000000ull;
    int64_t r = linux_mmap(hint, 4096, LINUX_PROT_READ, MAP_PA, -1, 0);
    TEST("mmap without MAP_FIXED: alloc_anon called, hint ignored");
    if (r > 0 && g_alloc_calls == 1 && g_alloc_at_calls == 0) PASS();
    else FAIL("hint not ignored");
}

/* -------- madvise -------- */

static void t_madvise_known_advice(void) {
    install_fake_ops();
    int64_t r1 = linux_madvise(0x1000, 4096, LINUX_MADV_DONTNEED);
    int64_t r2 = linux_madvise(0x1000, 4096, LINUX_MADV_WILLNEED);
    int64_t r3 = linux_madvise(0x1000, 4096, LINUX_MADV_FREE);
    int64_t r4 = linux_madvise(0x1000, 4096, LINUX_MADV_HUGEPAGE);
    TEST("madvise: known advice values return 0");
    if (r1 == 0 && r2 == 0 && r3 == 0 && r4 == 0) PASS();
    else FAIL("known advice rejected");
}

static void t_madvise_unknown(void) {
    int64_t r = linux_madvise(0x1000, 4096, 999);
    TEST("madvise: unknown advice -> -EINVAL");
    if (r == -LINUX_EINVAL) PASS();
    else FAIL("unknown advice accepted");
}

static void t_madvise_misaligned(void) {
    int64_t r = linux_madvise(0x1003, 4096, LINUX_MADV_DONTNEED);
    TEST("madvise: misaligned addr -> -EINVAL");
    if (r == -LINUX_EINVAL) PASS();
    else FAIL("misaligned accepted");
}

static void t_madvise_zero_length(void) {
    int64_t r = linux_madvise(0x1000, 0, LINUX_MADV_DONTNEED);
    TEST("madvise: length=0 -> -EINVAL");
    if (r == -LINUX_EINVAL) PASS();
    else FAIL("zero length accepted");
}

/* -------- mremap -------- */

static void t_mremap_same_size_noop(void) {
    install_fake_ops();
    uint64_t addr = 0x100000000000ull;
    int64_t r = linux_mremap(addr, 4096, 4096, 0, 0);
    TEST("mremap: same size returns original addr (no callback)");
    if (r == (int64_t)addr && g_remap_calls == 0) PASS();
    else FAIL("same-size optimisation wrong");
}

static void t_mremap_grow_calls_remap(void) {
    install_fake_ops();
    uint64_t addr = 0x100000000000ull;
    int64_t r = linux_mremap(addr, 4096, 8192, LINUX_MREMAP_MAYMOVE, 0);
    TEST("mremap: grow path calls remap callback (1->2 pages)");
    if (r > 0 && g_remap_calls == 1 &&
        g_last_remap_old == 1 && g_last_remap_new == 2) PASS();
    else FAIL("grow path wrong");
}

static void t_mremap_shrink(void) {
    install_fake_ops();
    uint64_t addr = 0x100000000000ull;
    int64_t r = linux_mremap(addr, 8192, 4096, 0, 0);
    TEST("mremap: shrink path calls remap (2->1 pages)");
    if (r > 0 && g_last_remap_old == 2 && g_last_remap_new == 1) PASS();
    else FAIL("shrink path wrong");
}

static void t_mremap_fixed_requires_maymove(void) {
    install_fake_ops();
    int64_t r = linux_mremap(0x100000000000ull, 4096, 8192,
                             LINUX_MREMAP_FIXED, 0x200000000000ull);
    TEST("mremap: MREMAP_FIXED without MAYMOVE -> -EINVAL");
    if (r == -LINUX_EINVAL) PASS();
    else FAIL("invariant not enforced");
}

static void t_mremap_misaligned(void) {
    int64_t r = linux_mremap(0x1003, 4096, 8192, 0, 0);
    TEST("mremap: misaligned old_addr -> -EINVAL");
    if (r == -LINUX_EINVAL) PASS();
    else FAIL("misaligned accepted");
}

static void t_mremap_unknown_flag(void) {
    int64_t r = linux_mremap(0x1000, 4096, 8192, 0x100u, 0);
    TEST("mremap: unknown flag -> -EINVAL");
    if (r == -LINUX_EINVAL) PASS();
    else FAIL("unknown flag accepted");
}

static void t_install_null_clears_mremap_ops(void) {
    install_fake_ops();
    linux_mmap_install_ops(NULL);
    int64_t r = linux_mremap(0x100000000000ull, 4096, 8192,
                             LINUX_MREMAP_MAYMOVE, 0);
    TEST("mmap install_ops(NULL) clears mremap callback");
    if (r == -LINUX_ENOSYS && g_remap_calls == 0) PASS();
    else FAIL("mremap callback not cleared");
}

static void t_reset_clears_mmap_callbacks(void) {
    install_fake_ops();
    linux_mmap_reset_for_tests();
    int64_t r1 = linux_mmap(0, 4096, LINUX_PROT_READ, MAP_PA, -1, 0);
    int64_t r2 = linux_mremap(0x100000000000ull, 4096, 8192,
                              LINUX_MREMAP_MAYMOVE, 0);
    TEST("mmap reset clears installed callbacks");
    if (r1 == -LINUX_ENOMEM && r2 == -LINUX_ENOSYS &&
        g_alloc_calls == 0 && g_remap_calls == 0) PASS();
    else FAIL("reset callbacks not cleared");
}

int test_linux_mmap_run(void) {
    printf("[test_linux_mmap]\n");
    tests_run = tests_passed = 0;

    t_mmap_basic_anon();
    t_mmap_rounds_up_length();
    t_mmap_zero_length();
    t_mmap_unknown_prot();
    t_mmap_prot_none_ok();
    t_mmap_unknown_map_flag();
    t_mmap_map_shared_rejected();
    t_mmap_missing_anon();
    t_mmap_missing_private();
    t_mmap_fd_must_be_neg1();
    t_mmap_offset_must_be_zero();
    t_mmap_alloc_failure();
    t_mmap_no_ops();
    t_mmap_jit_pattern();
    t_install_null_clears_mmap_alloc_ops();

    t_munmap_basic();
    t_munmap_misaligned();
    t_munmap_zero_length();
    t_munmap_callback_fails();
    t_install_null_clears_release_protect_ops();

    t_mprotect_basic();
    t_mprotect_unknown_prot();
    t_mprotect_misaligned();

    t_mmap_fixed_basic();
    t_mmap_fixed_zero_addr();
    t_mmap_fixed_misaligned();
    t_mmap_no_fixed_ignores_hint();

    t_madvise_known_advice();
    t_madvise_unknown();
    t_madvise_misaligned();
    t_madvise_zero_length();

    t_mremap_same_size_noop();
    t_mremap_grow_calls_remap();
    t_mremap_shrink();
    t_mremap_fixed_requires_maymove();
    t_mremap_misaligned();
    t_mremap_unknown_flag();
    t_install_null_clears_mremap_ops();
    t_reset_clears_mmap_callbacks();

    printf("  -> %d/%d passed\n", tests_passed, tests_run);
    return tests_run - tests_passed;
}
