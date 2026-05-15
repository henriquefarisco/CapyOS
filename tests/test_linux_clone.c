/* Host tests for linux_clone (S1.4). */

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

#include "kernel/linux_compat/linux_clone.h"
#include "kernel/linux_compat/linux_errno.h"

static int tests_run, tests_passed;

#define TEST(name) do { tests_run++; printf("  %-74s ", name); } while (0)
#define PASS() do { printf("OK\n"); tests_passed++; } while (0)
#define FAIL(m) do { printf("FAIL: %s\n", m); } while (0)

static void t_clone_pthread_pattern_enosys(void) {
    int64_t r = linux_clone(LINUX_CLONE_PTHREAD_FLAGS, 0x10000ull, 0, 0, 0);
    TEST("clone: musl pthread flag pattern -> -ENOSYS (recognised)");
    if (r == -LINUX_ENOSYS) PASS(); else FAIL("expected ENOSYS");
}

static void t_clone_unknown_flag_einval(void) {
    /* Bit 31 (CLONE_IO) is technically known but we strip it from
     * KNOWN_FLAGS -- pick a higher unused bit. Linux up to 5.x
     * uses up to bit 31; we use bit 33 (well outside u32). */
    uint64_t bad = LINUX_CLONE_VM | (1ull << 33);
    int64_t r = linux_clone(bad, 0, 0, 0, 0);
    TEST("clone: unknown flag bit -> -EINVAL");
    if (r == -LINUX_EINVAL) PASS(); else FAIL("unknown flag accepted");
}

static void t_clone_thread_without_sighand(void) {
    /* CLONE_THREAD requires CLONE_SIGHAND. */
    uint64_t bad = LINUX_CLONE_VM | LINUX_CLONE_THREAD;
    int64_t r = linux_clone(bad, 0, 0, 0, 0);
    TEST("clone: CLONE_THREAD without CLONE_SIGHAND -> -EINVAL");
    if (r == -LINUX_EINVAL) PASS(); else FAIL("invariant not enforced");
}

static void t_clone_sighand_without_vm(void) {
    /* CLONE_SIGHAND requires CLONE_VM. */
    uint64_t bad = LINUX_CLONE_SIGHAND;
    int64_t r = linux_clone(bad, 0, 0, 0, 0);
    TEST("clone: CLONE_SIGHAND without CLONE_VM -> -EINVAL");
    if (r == -LINUX_EINVAL) PASS(); else FAIL("invariant not enforced");
}

static void t_clone_csignal_in_low_bits(void) {
    /* Low byte = SIGCHLD (17). Should be ignored as a signal mask. */
    int64_t r = linux_clone(LINUX_CLONE_VM | LINUX_CLONE_SIGHAND |
                            LINUX_CLONE_THREAD | 17, 0, 0, 0, 0);
    TEST("clone: low byte (CSIGNAL=SIGCHLD) ignored, recognised pattern");
    if (r == -LINUX_ENOSYS) PASS(); else FAIL("CSIGNAL not stripped");
}

static void t_clone3_basic_size(void) {
    struct linux_clone_args args = { .flags = LINUX_CLONE_PTHREAD_FLAGS };
    int64_t r = linux_clone3((uint64_t)(uintptr_t)&args,
                             LINUX_CLONE_ARGS_SIZE_VER0);
    TEST("clone3: VER0 size accepted -> -ENOSYS");
    if (r == -LINUX_ENOSYS) PASS(); else FAIL("VER0 not accepted");
}

static void t_clone3_other_sizes(void) {
    struct linux_clone_args args = { .flags = LINUX_CLONE_PTHREAD_FLAGS };
    int64_t r1 = linux_clone3((uint64_t)(uintptr_t)&args,
                              LINUX_CLONE_ARGS_SIZE_VER1);
    int64_t r2 = linux_clone3((uint64_t)(uintptr_t)&args,
                              LINUX_CLONE_ARGS_SIZE_VER2);
    TEST("clone3: VER1 (80) and VER2 (88) sizes accepted -> -ENOSYS");
    if (r1 == -LINUX_ENOSYS && r2 == -LINUX_ENOSYS) PASS();
    else FAIL("size not accepted");
}

static void t_clone3_bad_size(void) {
    struct linux_clone_args args = { .flags = LINUX_CLONE_PTHREAD_FLAGS };
    int64_t r = linux_clone3((uint64_t)(uintptr_t)&args, 100);
    TEST("clone3: unknown size -> -EINVAL");
    if (r == -LINUX_EINVAL) PASS(); else FAIL("unknown size accepted");
}

static void t_clone3_null_args(void) {
    int64_t r = linux_clone3(0, LINUX_CLONE_ARGS_SIZE_VER0);
    TEST("clone3: NULL args -> -EFAULT");
    if (r == -LINUX_EFAULT) PASS(); else FAIL("NULL not rejected");
}

static void t_fork_enosys(void) {
    int64_t r = linux_fork();
    TEST("fork: -ENOSYS (no AS clone yet)");
    if (r == -LINUX_ENOSYS) PASS(); else FAIL("expected ENOSYS");
}

static void t_vfork_enosys(void) {
    int64_t r = linux_vfork();
    TEST("vfork: -ENOSYS");
    if (r == -LINUX_ENOSYS) PASS(); else FAIL("expected ENOSYS");
}

int test_linux_clone_run(void) {
    printf("[test_linux_clone]\n");
    tests_run = tests_passed = 0;

    t_clone_pthread_pattern_enosys();
    t_clone_unknown_flag_einval();
    t_clone_thread_without_sighand();
    t_clone_sighand_without_vm();
    t_clone_csignal_in_low_bits();

    t_clone3_basic_size();
    t_clone3_other_sizes();
    t_clone3_bad_size();
    t_clone3_null_args();

    t_fork_enosys();
    t_vfork_enosys();

    printf("  -> %d/%d passed\n", tests_passed, tests_run);
    return tests_run - tests_passed;
}
