/*
 * Host tests for the M5 phase B.2 embedded-progs registry.
 *
 * The registry maps canonical paths (`/bin/hello`,
 * `/bin/exectarget`) to a `(data, size)` pair sourced from
 * objcopy-produced `.rodata` blobs in the kernel build. On the
 * host build (UNIT_TEST), the source file (src/kernel/embedded_progs.c)
 * exposes two test setters that let us substitute the blob bytes
 * with cheap test fixtures so the path-matching logic is exercised
 * exactly as in production.
 *
 * The tests cover:
 *   - Hit on /bin/hello returns the registered (data, size).
 *   - Hit on /bin/exectarget returns its own (data, size), distinct
 *     from /bin/hello's.
 *   - Miss on an unrecognised path returns -1 and leaves out args
 *     untouched.
 *   - NULL path / NULL out pointers are rejected with -1.
 *   - Path matching is exact: prefix or suffix differences miss.
 */

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

#include "kernel/embedded_progs.h"

extern void embedded_progs_test_set_hello(const uint8_t *data,
                                          size_t size);
extern void embedded_progs_test_set_exectarget(const uint8_t *data,
                                               size_t size);
extern void embedded_progs_test_set_capybrowser(const uint8_t *data,
                                                size_t size);

static int tests_run = 0;
static int tests_passed = 0;

#define TEST(name)                                                         \
    do {                                                                   \
        tests_run++;                                                       \
        printf("  %-58s ", name);                                          \
    } while (0)
#define PASS()                                                             \
    do { printf("OK\n"); tests_passed++; } while (0)
#define FAIL(msg) do { printf("FAIL: %s\n", msg); } while (0)

static const uint8_t k_hello_blob[] = {0x7F, 'E', 'L', 'F', 0xAA};
static const uint8_t k_exectarget_blob[] = {0x7F, 'E', 'L', 'F', 0xBB,
                                            0xCC, 0xDD};
static const uint8_t k_capybrowser_blob[] = {0x7F, 'E', 'L', 'F',
                                             0xEE, 0xFF, 0x11};

static void install_test_blobs(void) {
    embedded_progs_test_set_hello(k_hello_blob, sizeof(k_hello_blob));
    embedded_progs_test_set_exectarget(k_exectarget_blob,
                                       sizeof(k_exectarget_blob));
    embedded_progs_test_set_capybrowser(k_capybrowser_blob,
                                        sizeof(k_capybrowser_blob));
}

static void test_hit_hello(void) {
    install_test_blobs();
    const uint8_t *data = NULL;
    size_t size = 0;
    int rc = embedded_progs_lookup("/bin/hello", &data, &size);

    TEST("/bin/hello: lookup returns 0");
    if (rc == 0) PASS(); else FAIL("non-zero rc");

    TEST("/bin/hello: data pointer matches hello blob");
    if (data == k_hello_blob) PASS();
    else FAIL("data pointer drift");

    TEST("/bin/hello: size matches hello blob length");
    if (size == sizeof(k_hello_blob)) PASS();
    else FAIL("size mismatch");
}

static void test_hit_exectarget(void) {
    install_test_blobs();
    const uint8_t *data = NULL;
    size_t size = 0;
    int rc = embedded_progs_lookup("/bin/exectarget", &data, &size);

    TEST("/bin/exectarget: lookup returns 0");
    if (rc == 0) PASS(); else FAIL("non-zero rc");

    TEST("/bin/exectarget: data pointer matches exectarget blob");
    if (data == k_exectarget_blob) PASS();
    else FAIL("data pointer drift");

    TEST("/bin/exectarget: size matches exectarget blob length");
    if (size == sizeof(k_exectarget_blob)) PASS();
    else FAIL("size mismatch");

    TEST("/bin/exectarget: registered blob distinct from /bin/hello's");
    if (data != k_hello_blob) PASS();
    else FAIL("collision with hello blob");
}

static void test_miss_unknown_path(void) {
    install_test_blobs();
    const uint8_t *data = (const uint8_t *)0xDEADBEEF;
    size_t size = 0xCAFE;
    int rc = embedded_progs_lookup("/bin/nonexistent", &data, &size);

    TEST("unknown path: lookup returns -1");
    if (rc == -1) PASS(); else FAIL("expected -1 on miss");

    TEST("unknown path: out_data left untouched");
    if (data == (const uint8_t *)0xDEADBEEF) PASS();
    else FAIL("data overwritten on miss");

    TEST("unknown path: out_size left untouched");
    if (size == 0xCAFE) PASS();
    else FAIL("size overwritten on miss");
}

static void test_null_inputs(void) {
    const uint8_t *data = NULL;
    size_t size = 0;

    TEST("NULL path returns -1");
    if (embedded_progs_lookup(NULL, &data, &size) == -1) PASS();
    else FAIL("NULL path accepted");

    TEST("NULL out_data returns -1");
    if (embedded_progs_lookup("/bin/hello", NULL, &size) == -1) PASS();
    else FAIL("NULL out_data accepted");

    TEST("NULL out_size returns -1");
    if (embedded_progs_lookup("/bin/hello", &data, NULL) == -1) PASS();
    else FAIL("NULL out_size accepted");
}

static void test_path_matching_exact(void) {
    install_test_blobs();
    const uint8_t *data = NULL;
    size_t size = 0;

    TEST("prefix-only ('/bin/hell') misses");
    if (embedded_progs_lookup("/bin/hell", &data, &size) == -1) PASS();
    else FAIL("prefix matched");

    TEST("suffix-extended ('/bin/hello-extra') misses");
    if (embedded_progs_lookup("/bin/hello-extra", &data, &size) == -1)
        PASS();
    else FAIL("suffix-extended matched");

    TEST("empty path misses");
    if (embedded_progs_lookup("", &data, &size) == -1) PASS();
    else FAIL("empty path accepted");
}

static void test_hit_capybrowser(void) {
    install_test_blobs();
    const uint8_t *data = NULL;
    size_t size = 0;
    int rc = embedded_progs_lookup("/bin/capybrowser", &data, &size);

    TEST("/bin/capybrowser: lookup returns 0");
    if (rc == 0) PASS(); else FAIL("non-zero rc");

    TEST("/bin/capybrowser: data pointer matches capybrowser blob");
    if (data == k_capybrowser_blob) PASS();
    else FAIL("data pointer drift");

    TEST("/bin/capybrowser: size matches capybrowser blob length");
    if (size == sizeof(k_capybrowser_blob)) PASS();
    else FAIL("size mismatch");

    TEST("/bin/capybrowser: distinct from /bin/hello and /bin/exectarget");
    if (data != k_hello_blob && data != k_exectarget_blob) PASS();
    else FAIL("capybrowser collides with another blob");
}

int test_embedded_progs_run(void) {
    printf("[test_embedded_progs]\n");
    tests_run = 0;
    tests_passed = 0;
    test_hit_hello();
    test_hit_exectarget();
    test_hit_capybrowser();
    test_miss_unknown_path();
    test_null_inputs();
    test_path_matching_exact();
    printf("  -> %d/%d passed\n", tests_passed, tests_run);
    return tests_run - tests_passed;
}
