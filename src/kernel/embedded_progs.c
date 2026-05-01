/*
 * src/kernel/embedded_progs.c -- registry of user binaries embedded
 * into the kernel image at link time (M5 phase B.2).
 *
 * The build system (see Makefile rules `$(HELLO_BLOB_OBJ)` and
 * `$(EXECTARGET_BLOB_OBJ)`) wraps each user ELF as a `.rodata`
 * blob via objcopy, which generates the `_binary_*_elf_start/_end`
 * symbols below. This source compiles unconditionally on the
 * kernel target; the host unit-test build deliberately excludes
 * it (no kernel-link blob exists on the host) and provides its
 * own stubs in tests/stub_embedded_progs.c.
 *
 * Lookup is by exact-path string compare. The set of registered
 * paths is enumerated by `kProgs` below; adding a binary is a
 * three-step change documented in include/kernel/embedded_progs.h.
 */

#include "kernel/embedded_progs.h"
#include "kernel/embedded_hello.h"

#ifdef UNIT_TEST
/* Host-test build: the kernel-link-time blob symbols and the
 * embedded_hello accessors are not available. Tests provide their
 * own bytes via the `embedded_progs_test_*` setters below; the
 * lookup string-compare logic remains unchanged so the path
 * matching is exercised end-to-end. */
static const uint8_t *g_test_hello_data = (const uint8_t *)0;
static size_t g_test_hello_size = 0;
static const uint8_t *g_test_exectarget_data = (const uint8_t *)0;
static size_t g_test_exectarget_size = 0;
static const uint8_t *g_test_capysh_data = (const uint8_t *)0;
static size_t g_test_capysh_size = 0;
static const uint8_t *g_test_capybrowser_data = (const uint8_t *)0;
static size_t g_test_capybrowser_size = 0;

void embedded_progs_test_set_hello(const uint8_t *data, size_t size) {
    g_test_hello_data = data;
    g_test_hello_size = size;
}

void embedded_progs_test_set_exectarget(const uint8_t *data, size_t size) {
    g_test_exectarget_data = data;
    g_test_exectarget_size = size;
}

void embedded_progs_test_set_capysh(const uint8_t *data, size_t size) {
    g_test_capysh_data = data;
    g_test_capysh_size = size;
}

void embedded_progs_test_set_capybrowser(const uint8_t *data, size_t size) {
    g_test_capybrowser_data = data;
    g_test_capybrowser_size = size;
}

static const void *hello_data_local(void) { return g_test_hello_data; }
static size_t hello_size_local(void) { return g_test_hello_size; }
static const void *exectarget_data(void) { return g_test_exectarget_data; }
static size_t exectarget_size(void) { return g_test_exectarget_size; }
static const void *capysh_data(void) { return g_test_capysh_data; }
static size_t capysh_size(void) { return g_test_capysh_size; }
static const void *capybrowser_data(void) { return g_test_capybrowser_data; }
static size_t capybrowser_size(void) { return g_test_capybrowser_size; }
#else
extern const uint8_t _binary_exectarget_elf_start[];
extern const uint8_t _binary_exectarget_elf_end[];
extern const uint8_t _binary_capysh_elf_start[];
extern const uint8_t _binary_capysh_elf_end[];
extern const uint8_t _binary_capybrowser_elf_start[];
extern const uint8_t _binary_capybrowser_elf_end[];

static const void *hello_data_local(void) { return embedded_hello_data(); }
static size_t hello_size_local(void) { return embedded_hello_size(); }

static const void *exectarget_data(void) {
    const uint8_t *start;
    __asm__ volatile("lea _binary_exectarget_elf_start(%%rip), %0"
                     : "=r"(start));
    return (const void *)start;
}

static size_t exectarget_size(void) {
    const uint8_t *start;
    const uint8_t *end;
    __asm__ volatile("lea _binary_exectarget_elf_start(%%rip), %0"
                     : "=r"(start));
    __asm__ volatile("lea _binary_exectarget_elf_end(%%rip), %0"
                     : "=r"(end));
    return (size_t)(end - start);
}

static const void *capysh_data(void) {
    const uint8_t *start;
    __asm__ volatile("lea _binary_capysh_elf_start(%%rip), %0"
                     : "=r"(start));
    return (const void *)start;
}

static size_t capysh_size(void) {
    const uint8_t *start;
    const uint8_t *end;
    __asm__ volatile("lea _binary_capysh_elf_start(%%rip), %0"
                     : "=r"(start));
    __asm__ volatile("lea _binary_capysh_elf_end(%%rip), %0"
                     : "=r"(end));
    return (size_t)(end - start);
}

static const void *capybrowser_data(void) {
    const uint8_t *start;
    __asm__ volatile("lea _binary_capybrowser_elf_start(%%rip), %0"
                     : "=r"(start));
    return (const void *)start;
}

static size_t capybrowser_size(void) {
    const uint8_t *start;
    const uint8_t *end;
    __asm__ volatile("lea _binary_capybrowser_elf_start(%%rip), %0"
                     : "=r"(start));
    __asm__ volatile("lea _binary_capybrowser_elf_end(%%rip), %0"
                     : "=r"(end));
    return (size_t)(end - start);
}
#endif

static int prog_path_eq(const char *a, const char *b) {
    while (*a && *b) {
        if (*a != *b) return 0;
        a++;
        b++;
    }
    return *a == *b;
}

int embedded_progs_lookup(const char *path,
                          const uint8_t **out_data,
                          size_t *out_size) {
    if (!path || !out_data || !out_size) return -1;

    if (prog_path_eq(path, "/bin/hello")) {
        *out_data = (const uint8_t *)hello_data_local();
        *out_size = hello_size_local();
        return 0;
    }
    if (prog_path_eq(path, "/bin/exectarget")) {
        *out_data = (const uint8_t *)exectarget_data();
        *out_size = exectarget_size();
        return 0;
    }
    if (prog_path_eq(path, "/bin/capysh")) {
        *out_data = (const uint8_t *)capysh_data();
        *out_size = capysh_size();
        return 0;
    }
    if (prog_path_eq(path, "/bin/capybrowser")) {
        *out_data = (const uint8_t *)capybrowser_data();
        *out_size = capybrowser_size();
        return 0;
    }
    return -1;
}
