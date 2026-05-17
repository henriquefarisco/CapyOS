#include "kernel/linux_compat/linux_caps.h"
#include "kernel/linux_compat/linux_errno.h"

#include <stdint.h>
#include <stdio.h>

static int tests_run = 0;
static int tests_passed = 0;

#define TEST(n) do { tests_run++; printf("  %-72s ", n); } while (0)
#define PASS()  do { printf("OK\n"); tests_passed++; } while (0)
#define FAIL(m) do { printf("FAIL: %s\n", m); } while (0)

static int g_get_calls;
static int g_set_calls;
static int g_last_pid;
static int g_last_n;

static int64_t fake_get(int pid, struct linux_cap_user_data *out, int n) {
    g_get_calls++; g_last_pid = pid; g_last_n = n;
    for (int i = 0; i < n; i++) {
        out[i].effective = 0x12340000u | (uint32_t)i;
        out[i].permitted = 0x56780000u | (uint32_t)i;
        out[i].inheritable = 0;
    }
    return 0;
}
static int64_t fake_set(int pid, const struct linux_cap_user_data *in,
                        int n) {
    g_set_calls++; g_last_pid = pid; g_last_n = n; (void)in;
    return 0;
}

static void install_fake(void) {
    static const struct linux_caps_ops o = {
        .get_caps = fake_get,
        .set_caps = fake_set,
    };
    g_get_calls = g_set_calls = 0;
    g_last_pid = -1; g_last_n = -1;
    linux_caps_reset_for_tests();
    linux_caps_install_ops(&o);
}

static void t1(void) {
    linux_caps_reset_for_tests();
    TEST("capget NULL hdr -> -EFAULT");
    if (linux_capget(NULL, NULL) == -LINUX_EFAULT) PASS();
    else FAIL("");
}
static void t2(void) {
    linux_caps_reset_for_tests();
    struct linux_cap_user_header h = { .version = 0xDEADBEEF, .pid = 0 };
    TEST("capget unknown version rewrites to v3 + -EINVAL");
    int64_t rc = linux_capget(&h, NULL);
    if (rc == -LINUX_EINVAL && h.version == LINUX_CAP_VERSION_3) PASS();
    else FAIL("");
}
static void t3(void) {
    linux_caps_reset_for_tests();
    struct linux_cap_user_header h = { .version = LINUX_CAP_VERSION_3,
                                       .pid = 0 };
    TEST("capget NULL data with valid version -> -EFAULT");
    if (linux_capget(&h, NULL) == -LINUX_EFAULT) PASS();
    else FAIL("");
}
static void t4(void) {
    linux_caps_reset_for_tests();
    struct linux_cap_user_header h = { .version = LINUX_CAP_VERSION_3,
                                       .pid = -1 };
    struct linux_cap_user_data d[2] = {{0,0,0},{0,0,0}};
    TEST("capget pid<0 -> -EINVAL");
    if (linux_capget(&h, d) == -LINUX_EINVAL) PASS();
    else FAIL("");
}
static void t5(void) {
    linux_caps_reset_for_tests();
    struct linux_cap_user_header h = { .version = LINUX_CAP_VERSION_3,
                                       .pid = 0 };
    struct linux_cap_user_data d[2] = {{0,0,0},{0,0,0}};
    int64_t rc = linux_capget(&h, d);
    TEST("capget v3 self default -> all caps in both entries");
    if (rc == 0 &&
        d[0].effective == 0xFFFFFFFFu && d[0].permitted == 0xFFFFFFFFu &&
        d[0].inheritable == 0xFFFFFFFFu &&
        d[1].effective == 0xFFFFFFFFu && d[1].permitted == 0xFFFFFFFFu &&
        d[1].inheritable == 0xFFFFFFFFu) PASS();
    else FAIL("");
}
static void t6(void) {
    linux_caps_reset_for_tests();
    struct linux_cap_user_header h = { .version = LINUX_CAP_VERSION_1,
                                       .pid = 0 };
    struct linux_cap_user_data d[2] = {{0,0,0},{0,0,0}};
    int64_t rc = linux_capget(&h, d);
    TEST("capget v1 -> only first entry filled");
    if (rc == 0 &&
        d[0].effective == 0xFFFFFFFFu &&
        d[1].effective == 0 /* untouched */) PASS();
    else FAIL("");
}
static void t7(void) {
    install_fake();
    struct linux_cap_user_header h = { .version = LINUX_CAP_VERSION_3,
                                       .pid = 7 };
    struct linux_cap_user_data d[2] = {{0,0,0},{0,0,0}};
    int64_t rc = linux_capget(&h, d);
    TEST("capget delegates to provider when installed");
    if (rc == 0 && g_get_calls == 1 && g_last_pid == 7 && g_last_n == 2 &&
        d[0].effective == 0x12340000u && d[1].effective == 0x12340001u) PASS();
    else FAIL("");
}
static void t8(void) {
    linux_caps_reset_for_tests();
    TEST("capset NULL hdr -> -EFAULT");
    if (linux_capset(NULL, NULL) == -LINUX_EFAULT) PASS();
    else FAIL("");
}
static void t9(void) {
    linux_caps_reset_for_tests();
    struct linux_cap_user_header h = { .version = 0xBADF00D, .pid = 0 };
    TEST("capset unknown version -> -EINVAL + rewrite");
    int64_t rc = linux_capset(&h, NULL);
    if (rc == -LINUX_EINVAL && h.version == LINUX_CAP_VERSION_3) PASS();
    else FAIL("");
}
static void t10(void) {
    linux_caps_reset_for_tests();
    struct linux_cap_user_header h = { .version = LINUX_CAP_VERSION_3,
                                       .pid = 0 };
    TEST("capset NULL data -> -EFAULT");
    if (linux_capset(&h, NULL) == -LINUX_EFAULT) PASS();
    else FAIL("");
}
static void t11(void) {
    linux_caps_reset_for_tests();
    struct linux_cap_user_header h = { .version = LINUX_CAP_VERSION_3,
                                       .pid = 99 };
    struct linux_cap_user_data d[2] = {{0,0,0},{0,0,0}};
    TEST("capset pid != 0 -> -EPERM (Linux: only self)");
    if (linux_capset(&h, d) == -LINUX_EPERM) PASS();
    else FAIL("");
}
static void t12(void) {
    linux_caps_reset_for_tests();
    struct linux_cap_user_header h = { .version = LINUX_CAP_VERSION_3,
                                       .pid = 0 };
    struct linux_cap_user_data d[2] = {{0,0,0},{0,0,0}};
    TEST("capset self drop-all -> 0 (root has CAP_SETPCAP)");
    if (linux_capset(&h, d) == 0) PASS();
    else FAIL("");
}
static void t13(void) {
    install_fake();
    struct linux_cap_user_header h = { .version = LINUX_CAP_VERSION_3,
                                       .pid = 0 };
    struct linux_cap_user_data d[2] = {{0,0,0},{0,0,0}};
    int64_t rc = linux_capset(&h, d);
    TEST("capset delegates to provider when installed");
    if (rc == 0 && g_set_calls == 1 && g_last_n == 2) PASS();
    else FAIL("");
}
static void t14(void) {
    install_fake();
    linux_caps_install_ops(NULL);
    struct linux_cap_user_header h = { .version = LINUX_CAP_VERSION_3,
                                       .pid = 0 };
    struct linux_cap_user_data d[2] = {{0,0,0},{0,0,0}};
    int64_t r1 = linux_capget(&h, d);
    int64_t r2 = linux_capset(&h, d);
    TEST("caps install_ops(NULL) clears capability callbacks");
    if (r1 == 0 && r2 == 0 && g_get_calls == 0 && g_set_calls == 0 &&
        d[0].effective == 0xFFFFFFFFu && d[1].effective == 0xFFFFFFFFu)
        PASS();
    else FAIL("");
}
static void t15(void) {
    install_fake();
    linux_caps_reset_for_tests();
    struct linux_cap_user_header h = { .version = LINUX_CAP_VERSION_3,
                                       .pid = 0 };
    struct linux_cap_user_data d[2] = {{0,0,0},{0,0,0}};
    int64_t r1 = linux_capget(&h, d);
    int64_t r2 = linux_capset(&h, d);
    TEST("caps reset clears installed callbacks");
    if (r1 == 0 && r2 == 0 && g_get_calls == 0 && g_set_calls == 0 &&
        d[0].effective == 0xFFFFFFFFu && d[1].effective == 0xFFFFFFFFu)
        PASS();
    else FAIL("");
}

int test_linux_caps_run(void) {
    printf("[test_linux_caps]\n");
    tests_run = tests_passed = 0;
    t1(); t2(); t3(); t4(); t5(); t6(); t7();
    t8(); t9(); t10(); t11(); t12(); t13(); t14(); t15();
    printf("  -> %d/%d passed\n", tests_passed, tests_run);
    return tests_run - tests_passed;
}
