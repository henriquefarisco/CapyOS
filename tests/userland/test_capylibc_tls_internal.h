/*
 * tests/userland/test_capylibc_tls_internal.h
 *
 * Shared scaffolding for the test_capylibc_tls family of host-side
 * test translation units carved out of the historical single-file
 * `tests/test_capylibc_tls.c` (1324 LOC) at the 2026-05-15 monolith
 * refactor.
 *
 * Owns:
 *   - TEST/PASS/FAIL macros wired to module-private counters.
 *   - `tests_run` / `tests_passed` extern declarations and macro
 *     aliases so the verbatim test bodies copied from the historical
 *     monolith continue to compile without rewrites.
 *   - `fake_ctx()` shared helper for I/O-path tests.
 *   - Companion entry declarations for the trust- and backend-tier
 *     test groups, invoked by the main entry `test_capylibc_tls_run`.
 *
 * NOT owned here: the linker symbols for `userland/lib/capylibc-tls`,
 * the `capy_tls_*` public API, or the internal lib header.
 */
#ifndef TESTS_USERLAND_TEST_CAPYLIBC_TLS_INTERNAL_H
#define TESTS_USERLAND_TEST_CAPYLIBC_TLS_INTERNAL_H

#include <stdint.h>
#include <stdio.h>

#include "capylibc-tls/capy_tls.h"
#include "../../userland/lib/capylibc-tls/capy_tls_internal.h"

/* Shared run/pass counters. Storage lives in test_capylibc_tls.c. */
extern int test_capylibc_tls_runs;
extern int test_capylibc_tls_passes;

/* Macro aliases keep historical test bodies verbatim across files. */
#define tests_run    test_capylibc_tls_runs
#define tests_passed test_capylibc_tls_passes

#define TEST(label)                                                 \
  do {                                                              \
    test_capylibc_tls_runs++;                                       \
    printf("    %-68s", label);                                     \
  } while (0)

#define PASS()                                                      \
  do {                                                              \
    test_capylibc_tls_passes++;                                     \
    printf(" OK\n");                                                \
  } while (0)

#define FAIL(why)                                                   \
  do {                                                              \
    printf(" FAIL: %s\n", why);                                     \
  } while (0)

/* Returns a non-null sentinel pointer used by I/O fail-closed
 * coverage that only needs a `capy_tls_context *` opaque handle to
 * exercise the public API's NULL-vs-non-NULL gates. */
struct capy_tls_context *test_capylibc_tls_fake_ctx(void);
#define fake_ctx test_capylibc_tls_fake_ctx

/* Companion entries (defined in the split test files). */
void test_capylibc_tls_trust_cases(void);
void test_capylibc_tls_backend_cases(void);

#endif /* TESTS_USERLAND_TEST_CAPYLIBC_TLS_INTERNAL_H */
