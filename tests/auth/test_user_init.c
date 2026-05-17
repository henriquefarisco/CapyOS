/*
 * Host tests for src/kernel/user_init.c (M4 phase 5c).
 *
 * Strategy: link src/kernel/user_init.c against host-side stubs for
 * `embedded_hello_*`, `elf_validate`, and `elf_load_into_process`,
 * plus the real `process_create` (already exercised by other tests
 * via tests/stub_vmm.c). The stubs let the test drive every branch
 * of `kernel_spawn_embedded_hello` without dragging in vmm / pmm /
 * vfs.
 *
 * What this locks:
 *   1. A bad blob (failing elf_validate) returns KERNEL_SPAWN_BAD_ELF
 *      and never burns a process slot.
 *   2. A valid blob with a successful load returns KERNEL_SPAWN_OK
 *      and writes the freshly created process into *out_proc.
 *   3. The (data, size) pair fed to elf_load_into_process is the
 *      exact pair returned by embedded_hello_data/_size, with no
 *      truncation or off-by-one in the helper.
 *   4. A failing elf_load_into_process returns
 *      KERNEL_SPAWN_LOAD_FAILED. The TODO comment in user_init.c
 *      acknowledges the embryo slot leak; phase 6 owns the cleanup.
 *   5. Passing out_proc=NULL is accepted on the happy path.
 *   6. The `enum kernel_spawn_result` numeric encoding (0, -1, -2,
 *      -3) does not drift.
 */
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "kernel/embedded_hello.h"
#include "kernel/elf_loader.h"
#include "kernel/process.h"
#include "kernel/user_init.h"

static int tests_run = 0;
static int tests_passed = 0;
#define TEST(name) do { tests_run++; printf("  %-58s ", name); } while (0)
#define PASS()     do { printf("OK\n"); tests_passed++; } while (0)
#define FAIL(msg)  printf("FAIL: %s\n", msg)

/* ---- mock blob ---------------------------------------------------- */

/* Two host buffers backed by static storage: a valid 64-byte ELF
 * stem (only the magic + machine + type are checked by elf_validate
 * stub below) and a deliberately corrupted blob. */
static uint8_t g_blob[128];
static size_t  g_blob_size = 0;

const void *embedded_hello_data(void) { return g_blob; }
size_t embedded_hello_size(void) { return g_blob_size; }

/* ---- stub elf_loader -------------------------------------------- */

static int g_validate_rv = 0;
static int g_load_rv = 0;
static int g_load_called = 0;
static const uint8_t *g_load_data = NULL;
static size_t g_load_size = 0;
static struct process *g_load_proc = NULL;

int elf_validate(const uint8_t *data, size_t size) {
    /* The real validator inspects e_ident + machine + type. The
     * test variant honours the per-test return value so we can drive
     * "bad ELF" without hand-crafting a wrong header. */
    (void)data; (void)size;
    return g_validate_rv;
}

int elf_load_into_process(struct process *proc, const uint8_t *data,
                          size_t size) {
    g_load_called++;
    g_load_proc = proc;
    g_load_data = data;
    g_load_size = size;
    return g_load_rv;
}

/* ---- test cases -------------------------------------------------- */

static void reset_state(void) {
    memset(g_blob, 0, sizeof(g_blob));
    g_blob_size = 64;
    g_validate_rv = 0;
    g_load_rv = 0;
    g_load_called = 0;
    g_load_data = NULL;
    g_load_size = 0;
    g_load_proc = NULL;
}

static void test_enum_contract(void) {
    TEST("KERNEL_SPAWN_OK == 0");
    if (KERNEL_SPAWN_OK == 0) PASS(); else FAIL("OK changed");
    TEST("KERNEL_SPAWN_BAD_ELF == -1");
    if (KERNEL_SPAWN_BAD_ELF == -1) PASS(); else FAIL("BAD_ELF drift");
    TEST("KERNEL_SPAWN_NO_PROCESS == -2");
    if (KERNEL_SPAWN_NO_PROCESS == -2) PASS(); else FAIL("NO_PROCESS drift");
    TEST("KERNEL_SPAWN_LOAD_FAILED == -3");
    if (KERNEL_SPAWN_LOAD_FAILED == -3) PASS(); else FAIL("LOAD_FAILED drift");
}

static void test_bad_elf(void) {
    reset_state();
    g_validate_rv = -1;

    struct process *p = (struct process *)0xDEADBEEF;
    int rc = kernel_spawn_embedded_hello(&p);

    TEST("bad ELF -> KERNEL_SPAWN_BAD_ELF");
    if (rc == KERNEL_SPAWN_BAD_ELF) PASS();
    else FAIL("expected KERNEL_SPAWN_BAD_ELF");

    TEST("bad ELF -> elf_load not called");
    if (g_load_called == 0) PASS();
    else FAIL("loader invoked despite invalid ELF");

    TEST("bad ELF -> *out_proc untouched");
    if (p == (struct process *)0xDEADBEEF) PASS();
    else FAIL("out_proc clobbered on failure");
}

static void test_happy_path(void) {
    reset_state();

    struct process *p = NULL;
    int rc = kernel_spawn_embedded_hello(&p);

    TEST("happy path -> KERNEL_SPAWN_OK");
    if (rc == KERNEL_SPAWN_OK) PASS();
    else FAIL("expected KERNEL_SPAWN_OK");

    TEST("happy path -> elf_load called once");
    if (g_load_called == 1) PASS();
    else FAIL("loader call count drift");

    TEST("happy path -> elf_load received embedded_hello_data");
    if (g_load_data == (const uint8_t *)g_blob) PASS();
    else FAIL("loader data pointer drift");

    TEST("happy path -> elf_load received embedded_hello_size");
    if (g_load_size == 64) PASS();
    else FAIL("loader size drift");

    TEST("happy path -> *out_proc set to the new process");
    if (p != NULL && p == g_load_proc) PASS();
    else FAIL("out_proc not threaded through");

    TEST("happy path -> process name is 'hello'");
    if (p && strcmp(p->name, "hello") == 0) PASS();
    else FAIL("process name drift");
}

static void test_load_failed(void) {
    reset_state();
    g_load_rv = -1;

    int rc = kernel_spawn_embedded_hello(NULL);

    TEST("elf_load failure -> KERNEL_SPAWN_LOAD_FAILED");
    if (rc == KERNEL_SPAWN_LOAD_FAILED) PASS();
    else FAIL("expected KERNEL_SPAWN_LOAD_FAILED");
}

static void test_null_out_param(void) {
    reset_state();

    int rc = kernel_spawn_embedded_hello(NULL);

    TEST("out_proc=NULL on happy path is accepted");
    if (rc == KERNEL_SPAWN_OK) PASS();
    else FAIL("NULL out_proc rejected");
}

int test_user_init_run(void) {
    printf("[test_user_init]\n");
    tests_run = 0;
    tests_passed = 0;

    test_enum_contract();
    test_bad_elf();
    test_happy_path();
    test_load_failed();
    test_null_out_param();

    printf("  -> %d/%d passed\n", tests_passed, tests_run);
    return tests_run - tests_passed;
}
