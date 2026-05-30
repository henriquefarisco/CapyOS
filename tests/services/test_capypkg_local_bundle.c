#include "services/capypkg_local_bundle.h"

#include <stdint.h>
#include <stdio.h>
#include <string.h>

static int g_local_bundle_failures = 0;
static int g_local_bundle_total = 0;

#define EXPECT(cond, msg)                                                    \
    do {                                                                     \
        ++g_local_bundle_total;                                              \
        if (!(cond)) {                                                       \
            ++g_local_bundle_failures;                                       \
            fprintf(stderr,                                                  \
                    "[fail] %s:%d EXPECT(%s) - %s\n",                        \
                    __FILE__, __LINE__, #cond, (msg));                       \
        }                                                                    \
    } while (0)

static void test_local_bundle_reports_available(void) {
    EXPECT(capypkg_local_bundle_available() == 1,
           "UNIT_TEST fixture must expose a local bundle");
}

static void test_local_bundle_fetches_index_text(void) {
    char buffer[512];
    size_t len = 0u;
    int rc = capypkg_local_bundle_fetch_text(CAPYPKG_LOCAL_BUNDLE_INDEX_URL,
                                             buffer, sizeof(buffer), &len);
    EXPECT(rc == 0, "index fetch must succeed");
    EXPECT(len > 0u, "index length must be reported");
    EXPECT(strstr(buffer, "name=org.capyos.test.local\n") != NULL,
           "index must contain fixture package name");
    EXPECT(strstr(buffer, "payload_url=https://local.capyos.invalid/capypkg/org.capyos.test.local-0.0.1.bin\n") != NULL,
           "index must contain local HTTPS payload URL");
}

static void test_local_bundle_fetch_text_rejects_small_buffer(void) {
    char buffer[8];
    size_t len = 99u;
    int rc = capypkg_local_bundle_fetch_text(CAPYPKG_LOCAL_BUNDLE_INDEX_URL,
                                             buffer, sizeof(buffer), &len);
    EXPECT(rc != 0, "small text buffer must be rejected");
    EXPECT(len == 99u, "failed fetch must not overwrite out_len");
}

static void test_local_bundle_fetches_payload_bytes(void) {
    uint8_t buffer[3] = {0u, 0u, 0u};
    size_t len = 0u;
    int rc = capypkg_local_bundle_fetch_bytes(
        "https://local.capyos.invalid/capypkg/org.capyos.test.local-0.0.1.bin",
        buffer, sizeof(buffer), &len);
    EXPECT(rc == 0, "payload fetch must succeed");
    EXPECT(len == 3u, "payload length must match fixture");
    EXPECT(buffer[0] == 'C' && buffer[1] == 'A' && buffer[2] == 'P',
           "payload bytes must match fixture");
}

static void test_local_bundle_rejects_unknown_url(void) {
    uint8_t buffer[8];
    size_t len = 7u;
    int rc = capypkg_local_bundle_fetch_bytes(
        "https://example.org/not-local.bin", buffer, sizeof(buffer), &len);
    EXPECT(rc != 0, "unknown URL must not resolve through local bundle");
    EXPECT(len == 7u, "unknown URL must not overwrite out_len");
}

int run_capypkg_local_bundle_tests(void) {
    test_local_bundle_reports_available();
    test_local_bundle_fetches_index_text();
    test_local_bundle_fetch_text_rejects_small_buffer();
    test_local_bundle_fetches_payload_bytes();
    test_local_bundle_rejects_unknown_url();
    fprintf(stderr,
            "capypkg_local_bundle: %d/%d tests passed (%d failures)\n",
            g_local_bundle_total - g_local_bundle_failures,
            g_local_bundle_total, g_local_bundle_failures);
    return g_local_bundle_failures == 0 ? 0 : 1;
}
